#include "mainwindow.hpp"

#include "mapview.hpp"

#include <QAction>
#include <QCloseEvent>
#include <QDockWidget>
#include <QDir>
#include <QFileDialog>
#include <QFont>
#include <QKeySequence>
#include <QLabel>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <exception>
#include <string>
#include <vector>

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

    // Central area is a stack: the placeholder label until a map is open, then
    // the GL viewport. MapView is a QOpenGLWidget — the QOpenGLWindow-in-
    // createWindowContainer path rendered nothing on this Wayland setup (the
    // container surface and GL surface diverged). QOpenGLWidget composits
    // correctly here; see STATE for the measured copy-path note.
    stack_ = new QStackedWidget(this);

    placeholder_ = new QLabel("PZMapMaker\nFile → Open Map… to begin.");
    placeholder_->setAlignment(Qt::AlignCenter);
    stack_->addWidget(placeholder_);            // index 0

    view_ = new MapView();
    // paintGL runs after onCellActivated returns, so pass timing arrives via
    // signal rather than synchronously. Append it to the status line.
    connect(view_, &MapView::timingReady, this, [this](const PassTiming& t) {
        statusBar()->showMessage(
            QString("Draw: opaque %1 inst %2ms · translucent %3 inst %4ms · total %5ms")
                .arg(t.opaqueInstances)
                .arg(t.opaqueMs, 0, 'f', 2)
                .arg(t.translucentInstances)
                .arg(t.translucentMs, 0, 'f', 2)
                .arg(t.totalMs(), 0, 'f', 2));
    });

    // Level selector (Model B): a spinbox in the status bar shows all tiles at
    // or below the chosen z. Two-way synced: keys ([ ] and digits) in the view
    // update the box, and the box updates the view.
    levelLabel_ = new QLabel("Level:");
    levelSpin_ = new QSpinBox();
    levelSpin_->setRange(0, 7);
    levelSpin_->setToolTip("Show tiles at or below this level. Keys: [ ] or 0-7 in the view.");
    statusBar()->addPermanentWidget(levelLabel_);
    statusBar()->addPermanentWidget(levelSpin_);
    connect(levelSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int z) { if (view_) view_->setMaxLevel(z); });
    connect(view_, &MapView::maxLevelChanged, this, [this](int z) {
        // Reflect view-side changes (keys, or reset on cell load) without
        // re-triggering the view via the spinbox's own signal. Also keep the
        // range matched to the loaded cell's actual level span.
        QSignalBlocker block(levelSpin_);
        levelSpin_->setRange(view_->cellMinLevel(), view_->cellMaxLevel());
        levelSpin_->setValue(z);
    });
    view_->setMinimumSize(320, 240);
    view_->setFocusPolicy(Qt::StrongFocus);
    stack_->addWidget(view_);                    // index 1

    stack_->setCurrentIndex(0);
    setCentralWidget(stack_);

    buildMenus();
    buildDockPanels();
    statusBar()->showMessage("Ready");

    // Auto-load the texturepacks indexed last session, so sprites just work
    // without re-picking the folder every launch.
    const QString savedTex = QSettings().value("texturepacksDir").toString();
    if (!savedTex.isEmpty() && QDir(savedTex).exists())
        loadTexturepacks(savedTex, /*fromUser=*/false);
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

    QAction* texAct = fileMenu->addAction("Set &Texturepacks…");
    connect(texAct, &QAction::triggered, this, &MainWindow::setTexturepacks);

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

        // Hand the cell to the viewport. It runs the dense-cell census (the C3
        // step-1 measurement) and, later, renders. Switch the central stack
        // from the placeholder to the GL surface on first load.
        view_->setCell(*lc.data);
        stack_->setCurrentIndex(1);

        // If the texturepacks are indexed, build this cell's sprite layers (in
        // tileNames order so layer index == instance layer) and hand them over
        // so real art draws. If not indexed yet, the view shows placeholder
        // tints until Set Texturepacks is used.
        if (atlas_.ready()) {
            const auto& names = lc.data->header().tileNames;
            std::vector<std::string> want(names.begin(), names.end());
            view_->setSprites(atlas_.buildLayers(want));
        }

        const long nonEmpty = lc.data->nonEmptySquares();
        const auto rooms = lc.data->header().rooms.size();
        const CellCensus& cen = view_->lastCensus();
        setStatus(QString("Cell %1 · %2 rooms · %3 non-empty · levels %4..%5 · "
                          "%6 instances · %7 resident · %8 dirty")
                      .arg(QString::fromStdString(c.name()))
                      .arg(rooms)
                      .arg(nonEmpty)
                      .arg(lc.data->minLevel())
                      .arg(lc.data->maxLevel())
                      .arg(cen.instances)
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

void MainWindow::setTexturepacks() {
    // Default to the saved path, else the common Steam Linux location.
    QString start = QSettings().value("texturepacksDir").toString();
    if (start.isEmpty() || !QDir(start).exists()) {
        const QString guess = QDir::homePath() +
            "/.local/share/Steam/steamapps/common/ProjectZomboid/projectzomboid/media/texturepacks";
        if (QDir(guess).exists()) start = guess;
    }
    const QString dir = QFileDialog::getExistingDirectory(
        this, "Select PZ texturepacks folder (contains .pack files)", start);
    if (dir.isEmpty()) return;
    loadTexturepacks(dir, /*fromUser=*/true);
}

void MainWindow::loadTexturepacks(const QString& dir, bool fromUser) {
    try {
        const std::size_t n = atlas_.indexDir(dir.toStdString());
        QSettings().setValue("texturepacksDir", dir);   // persist for next launch
        setStatus(QString("Indexed %1 sprites from texturepacks").arg(n));
        // If a cell is already loaded, build its layers now.
        if (project_ && currentCell_) {
            pzformat::LoadedCell& lc = project_->load(*currentCell_);
            const auto& names = lc.data->header().tileNames;
            std::vector<std::string> want(names.begin(), names.end());
            auto layers = atlas_.buildLayers(want);
            int found = 0;
            for (const auto& L : layers) if (L.found) ++found;
            view_->setSprites(std::move(layers));
            setStatus(QString("Sprites: %1/%2 resolved for %3 (%4 missing)")
                          .arg(found).arg(want.size())
                          .arg(QString::fromStdString(currentCell_->name()))
                          .arg(atlas_.lastMissing()));
        }
    } catch (const std::exception& e) {
        if (fromUser) QMessageBox::warning(this, "Texturepacks", e.what());
        // On startup (fromUser=false) a stale saved path just fails quietly.
    }
}

void MainWindow::setStatus(const QString& msg) {
    statusBar()->showMessage(msg);
}

} // namespace pzmm
