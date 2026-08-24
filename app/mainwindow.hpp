// The C2 application shell. Drives the MapProject working-store model:
// open a map directory, list its cells in a dock panel, and load a cell on
// click. No viewport yet — this proves the model drives the UI end to end.
//
// The library (pzformat) stays Qt-free; this class is the boundary where Qt
// meets it. MainWindow owns the MapProject and the borrowed TileIndex.
#pragma once

#include "mapproject.hpp"
#include "tileindex.hpp"
#include "spriteatlas.hpp"

#include <QMainWindow>
#include <QStringList>

#include <memory>
#include <optional>

class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QLabel;
class QSpinBox;
class QCloseEvent;
class QStackedWidget;

namespace pzmm {

class MapView;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void openMap();
    void openRecent();
    void onCellActivated(QListWidgetItem* item);
    void saveCurrent();
    void saveAll();
    void filterCells(const QString& text);
    void jumpToFirstMatch();
    void setTexturepacks();
    void loadTexturepacks(const QString& dir, bool fromUser);

private:
    void buildMenus();
    void buildDockPanels();
    void populateCellList();
    void refreshDirtyMarkers();
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
    SpriteAtlas atlas_;   // sprite loader for the viewport (step 4)
    std::optional<pzformat::MapProject> project_;
    std::optional<pzformat::CellCoord> currentCell_;

    // UI
    QStackedWidget* stack_ = nullptr; // placeholder (0) / viewport (1)
    QLineEdit* cellSearch_ = nullptr;
    QListWidget* cellList_ = nullptr;
    QLabel* placeholder_ = nullptr;   // shown before a map is open
    MapView* view_ = nullptr;         // GL viewport; central widget once created
    QLabel* levelLabel_ = nullptr;
    QSpinBox* levelSpin_ = nullptr;   // level selector (Model B)
};

} // namespace pzmm
