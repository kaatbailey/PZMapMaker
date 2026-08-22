#include "mainwindow.hpp"

#include <QAction>
#include <QDebug>
#include <QDockWidget>
#include <QFileDialog>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QString>
#include <QToolBar>

#include <exception>

namespace pzmm {

using pzformat::CellCoord;
using pzformat::MapProject;

namespace {
// Qt::UserRole payload: pack a CellCoord into two ints on the item.
constexpr int kRoleX = Qt::UserRole + 1;
constexpr int kRoleY = Qt::UserRole + 2;

// Temporary diagnostic: proves openMap() is actually being invoked.
void qDebugProbe() {
    qDebug() << "[pzmm] openMap() invoked";
}
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("PZMapMaker");
    resize(1200, 800);

    placeholder_ = new QLabel("PZMapMaker\nFile → Open Map… to begin.");
    placeholder_->setAlignment(Qt::AlignCenter);
    setCentralWidget(placeholder_);

    buildMenus();
    buildDockPanels();
    statusBar()->showMessage("Ready");
}

void MainWindow::buildMenus() {
    // KDE can hoist the menu bar into the global top-of-screen bar, or hide it
    // entirely. Force it to stay in the window so File → Open is always
    // clickable, and back it up with a toolbar button below.
    menuBar()->setNativeMenuBar(false);

    QMenu* fileMenu = menuBar()->addMenu("&File");

    QAction* openAct = fileMenu->addAction("&Open Map…");
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::openMap);

    fileMenu->addSeparator();

    QAction* saveAct = fileMenu->addAction("&Save Cell");
    saveAct->setShortcut(QKeySequence::Save);
    connect(saveAct, &QAction::triggered, this, &MainWindow::saveCurrent);

    QAction* saveAllAct = fileMenu->addAction("Save &All");
    connect(saveAllAct, &QAction::triggered, this, &MainWindow::saveAll);

    fileMenu->addSeparator();

    QAction* quitAct = fileMenu->addAction("&Quit");
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

    // Always-visible toolbar, independent of the menu bar. This button is the
    // reliable way to open a map regardless of KDE menu behaviour.
    QToolBar* bar = addToolBar("Main");
    bar->setMovable(false);
    bar->addAction(openAct);
    bar->addAction(saveAct);
}

void MainWindow::buildDockPanels() {
    auto* dock = new QDockWidget("Cells", this);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    cellList_ = new QListWidget(dock);
    connect(cellList_, &QListWidget::itemActivated, this, &MainWindow::onCellActivated);
    // itemActivated is enter/double-click; also load on single click for ease.
    connect(cellList_, &QListWidget::itemClicked, this, &MainWindow::onCellActivated);

    dock->setWidget(cellList_);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
}

void MainWindow::openMap() {
    qDebugProbe();
    setStatus("Open Map clicked…");

    if (!confirmDiscardIfDirty()) return;

    // Use Qt's own dialog rather than the native KDE portal: on Wayland the
    // portal can fail to appear silently. DontUseNativeDialog forces the
    // in-process Qt widget dialog, which always shows.
    const QString dir = QFileDialog::getExistingDirectory(
        this, "Open Project Zomboid map directory", QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog);
    if (dir.isEmpty()) {
        setStatus("Open cancelled");
        return;
    }

    try {
        project_.emplace(MapProject::open(dir.toStdString(), tiles_));
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Cannot open map",
            QString("That directory has no cells.\n\n%1\n\n"
                    "A PZ map directory contains files like 35_35.lotheader and "
                    "world_35_35.lotpack.").arg(e.what()));
        return;
    }

    currentCell_.reset();
    populateCellList();
    setStatus(QString("Opened %1 — %2 cells")
                  .arg(dir)
                  .arg(project_->cells().size()));
}

void MainWindow::populateCellList() {
    cellList_->clear();
    if (!project_) return;
    for (const auto& c : project_->cells()) {
        auto* item = new QListWidgetItem(QString::fromStdString(c.name()));
        item->setData(kRoleX, c.x);
        item->setData(kRoleY, c.y);
        cellList_->addItem(item);
    }
}

void MainWindow::onCellActivated(QListWidgetItem* item) {
    if (!project_ || item == nullptr) return;
    const CellCoord c{item->data(kRoleX).toInt(), item->data(kRoleY).toInt()};

    try {
        pzformat::LoadedCell& lc = project_->load(c);
        currentCell_ = c;
        const long nonEmpty = lc.data->nonEmptySquares();
        const auto rooms = lc.data->header().rooms.size();
        placeholder_->setText(
            QString("Cell %1\n%2 rooms · %3 non-empty squares · levels %4..%5")
                .arg(QString::fromStdString(c.name()))
                .arg(rooms)
                .arg(nonEmpty)
                .arg(lc.data->minLevel())
                .arg(lc.data->maxLevel()));
        setStatus(QString("Loaded %1 · %2 resident · %3 dirty")
                      .arg(QString::fromStdString(c.name()))
                      .arg(project_->residentCount())
                      .arg(project_->dirtyCells().size()));
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Cannot load cell", e.what());
    }
}

void MainWindow::saveCurrent() {
    if (!project_ || !currentCell_) { setStatus("No cell selected"); return; }
    try {
        project_->save(*currentCell_);
        setStatus(QString("Saved %1").arg(QString::fromStdString(currentCell_->name())));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Save failed", e.what());
    }
}

void MainWindow::saveAll() {
    if (!project_) return;
    try {
        const int n = project_->saveAll();
        setStatus(QString("Saved %1 cell(s)").arg(n));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Save failed", e.what());
    }
}

bool MainWindow::confirmDiscardIfDirty() {
    if (!project_ || !project_->anyDirty()) return true;
    const auto r = QMessageBox::question(
        this, "Unsaved changes",
        "There are unsaved edits. Discard them and open a different map?",
        QMessageBox::Discard | QMessageBox::Cancel);
    return r == QMessageBox::Discard;
}

void MainWindow::setStatus(const QString& msg) {
    statusBar()->showMessage(msg);
}

} // namespace pzmm
