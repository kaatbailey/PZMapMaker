// The C2 application shell. Drives the MapProject working-store model:
// open a map directory, list its cells in a dock panel, and load a cell on
// click. No viewport yet — this proves the model drives the UI end to end.
//
// The library (pzformat) stays Qt-free; this class is the boundary where Qt
// meets it. MainWindow owns the MapProject and the borrowed TileIndex.
#pragma once

#include "mapproject.hpp"
#include "tileindex.hpp"

#include <QMainWindow>
#include <QStringList>

#include <memory>
#include <optional>

class QListWidget;
class QListWidgetItem;
class QLabel;

namespace pzmm {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void openMap();
    void openRecent();
    void onCellActivated(QListWidgetItem* item);
    void saveCurrent();
    void saveAll();

private:
    void buildMenus();
    void buildDockPanels();
    void populateCellList();
    void setStatus(const QString& msg);
    bool confirmDiscardIfDirty();

    // Recent maps, persisted via QSettings under ~/.config/PZMapMaker.
    void openMapDir(const QString& dir);      // shared open path
    void rememberRecent(const QString& dir);
    void rebuildRecentMenu();
    QStringList recentPaths() const;

    QMenu* recentMenu_ = nullptr;
    static constexpr int kMaxRecent = 10;

    // Model side (Qt-free). TileIndex is loaded lazily from the PZ media dir the
    // first time a map is opened; for now it may be empty (classification-
    // dependent editor ops just won't classify, which is fine for browsing).
    pzformat::TileIndex tiles_;
    std::optional<pzformat::MapProject> project_;
    std::optional<pzformat::CellCoord> currentCell_;

    // UI
    QListWidget* cellList_ = nullptr;
    QLabel* placeholder_ = nullptr;
};

} // namespace pzmm
