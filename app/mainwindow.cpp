#include "mainwindow.hpp"

#include <QAction>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFont>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QToolBar>
#include <QVBoxLayout>

#include <exception>

namespace pzmm {

using pzformat::CellCoord;
using pzformat::MapProject;

namespace {
// Qt::UserRole payload: pack a CellCoord into two ints on the item.
constexpr int kRoleX = Qt::UserRole + 1;
constexpr int kRoleY = Qt::UserRole + 2;

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

    recentMenu_ = fileMenu->addMenu("Open &Recent");
    rebuildRecentMenu();

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

    // Go-to-cell shortcut: Ctrl+G focuses the search box.
    QAction* gotoAct = fileMenu->addAction("&Go to Cell…");
    gotoAct->setShortcut(QKeySequence("Ctrl+G"));
    connect(gotoAct, &QAction::triggered, this, [this] {
        if (cellSearch_) { cellSearch_->setFocus(); cellSearch_->selectAll(); }
    });

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

    // Container widget: search box above cell list.
    auto* container = new QWidget(dock);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    cellSearch_ = new QLineEdit(container);
    cellSearch_->setPlaceholderText("Search cells (e.g. 35_35)…");
    cellSearch_->setClearButtonEnabled(true);
    connect(cellSearch_, &QLineEdit::textChanged, this, &MainWindow::filterCells);
    connect(cellSearch_, &QLineEdit::returnPressed, this, &MainWindow::jumpToFirstMatch);
    layout->addWidget(cellSearch_);

    cellList_ = new QListWidget(container);
    connect(cellList_, &QListWidget::itemActivated, this, &MainWindow::onCellActivated);
    // itemActivated is enter/double-click; also load on single click for ease.
    connect(cellList_, &QListWidget::itemClicked, this, &MainWindow::onCellActivated);
    layout->addWidget(cellList_);

    dock->setWidget(container);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
}

void MainWindow::openMap() {
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
    openMapDir(dir);
}

void MainWindow::openRecent() {
    auto* action = qobject_cast<QAction*>(sender());
    if (!action) return;
    if (!confirmDiscardIfDirty()) return;
    openMapDir(action->data().toString());
}

void MainWindow::openMapDir(const QString& dir) {
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
    rememberRecent(dir);
    setStatus(QString("Opened %1 — %2 cells")
                  .arg(dir)
                  .arg(project_->cells().size()));
}

// --- recent maps (QSettings-backed) ---

QStringList MainWindow::recentPaths() const {
    QSettings s;
    return s.value("recentMaps").toStringList();
}

void MainWindow::rememberRecent(const QString& dir) {
    QStringList paths = recentPaths();
    paths.removeAll(dir);       // de-dup, move to front
    paths.prepend(dir);
    while (paths.size() > kMaxRecent) paths.removeLast();
    QSettings().setValue("recentMaps", paths);
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu() {
    if (!recentMenu_) return;
    recentMenu_->clear();
    const QStringList paths = recentPaths();
    if (paths.isEmpty()) {
        QAction* none = recentMenu_->addAction("(no recent maps)");
        none->setEnabled(false);
        return;
    }
    for (const QString& p : paths) {
        QAction* act = recentMenu_->addAction(p);
        act->setData(p);
        connect(act, &QAction::triggered, this, &MainWindow::openRecent);
    }
    recentMenu_->addSeparator();
    QAction* clear = recentMenu_->addAction("Clear Recent");
    connect(clear, &QAction::triggered, this, [this] {
        QSettings().remove("recentMaps");
        rebuildRecentMenu();
    });
}

void MainWindow::populateCellList() {
    cellList_->clear();
    if (cellSearch_) cellSearch_->clear();
    if (!project_) return;
    for (const auto& c : project_->cells()) {
        auto* item = new QListWidgetItem(QString::fromStdString(c.name()));
        item->setData(kRoleX, c.x);
        item->setData(kRoleY, c.y);
        cellList_->addItem(item);
    }
    refreshDirtyMarkers();
}

void MainWindow::filterCells(const QString& text) {
    // Hide items whose base name doesn't contain the search substring.
    for (int i = 0; i < cellList_->count(); ++i) {
        auto* item = cellList_->item(i);
        const CellCoord c{item->data(kRoleX).toInt(), item->data(kRoleY).toInt()};
        const QString name = QString::fromStdString(c.name());
        item->setHidden(!name.contains(text, Qt::CaseInsensitive));
    }
}

void MainWindow::jumpToFirstMatch() {
    // Enter in the search box: load the first visible (non-hidden) cell.
    for (int i = 0; i < cellList_->count(); ++i) {
        auto* item = cellList_->item(i);
        if (!item->isHidden()) {
            cellList_->setCurrentItem(item);
            cellList_->scrollToItem(item);
            onCellActivated(item);
            cellList_->setFocus();   // move focus to list so arrow keys work after jump
            return;
        }
    }
    // If we get here: either no map loaded, or search text matches nothing.
    if (!project_) {
        QMessageBox::information(this, "No map open",
            "Open a map first (File → Open Map…), then use the search box to find a cell.");
    } else {
        setStatus(QString("No cell matches \"%1\"").arg(cellSearch_->text()));
    }
}

void MainWindow::refreshDirtyMarkers() {
    if (!project_) return;
    const auto dirty = project_->dirtyCells();
    // Build a set for fast lookup.
    std::map<std::string, bool> dirtySet;
    for (const auto& c : dirty) dirtySet[c.name()] = true;

    for (int i = 0; i < cellList_->count(); ++i) {
        auto* item = cellList_->item(i);
        const CellCoord c{item->data(kRoleX).toInt(), item->data(kRoleY).toInt()};
        const bool isDirty = dirtySet.count(c.name()) > 0;
        // Display text: "X_Y" or "X_Y *"
        const QString base = QString::fromStdString(c.name());
        item->setText(isDirty ? base + " *" : base);
        // Bold font for dirty cells.
        QFont f = item->font();
        f.setBold(isDirty);
        item->setFont(f);
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
        refreshDirtyMarkers();
        setStatus(QString("Saved %1").arg(QString::fromStdString(currentCell_->name())));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Save failed", e.what());
    }
}

void MainWindow::saveAll() {
    if (!project_) return;
    try {
        const int n = project_->saveAll();
        refreshDirtyMarkers();
        setStatus(QString("Saved %1 cell(s)").arg(n));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Save failed", e.what());
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (confirmDiscardIfDirty())
        event->accept();
    else
        event->ignore();
}

bool MainWindow::confirmDiscardIfDirty() {
    if (!project_ || !project_->anyDirty()) return true;
    const auto r = QMessageBox::question(
        this, "Unsaved changes",
        "There are unsaved edits. Discard them and continue?",
        QMessageBox::Discard | QMessageBox::Cancel);
    return r == QMessageBox::Discard;
}

void MainWindow::setStatus(const QString& msg) {
    statusBar()->showMessage(msg);
}

} // namespace pzmm
