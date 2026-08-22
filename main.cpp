// Minimal shell — the smallest thing that proves CMake + Qt6 + CLion are wired
// correctly on this machine. Opens a window with a menu bar and a placeholder
// central widget, and quits cleanly. No map logic yet.
//
// Once this builds and runs, the full MainWindow (cell list, open dialog,
// viewport dock) is incremental. Building the whole shell before confirming the
// toolchain would mean debugging 40 errors at once across the one boundary this
// project can't verify in CI.
#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QStatusBar>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("PZMapMaker");

    QMainWindow window;
    window.setWindowTitle("PZMapMaker");
    window.resize(1200, 800);

    // File menu with a working Quit, so the menu-bar wiring is proven.
    QMenu* fileMenu = window.menuBar()->addMenu("&File");
    QAction* openAct = fileMenu->addAction("&Open Map…");
    openAct->setShortcut(QKeySequence::Open);
    fileMenu->addSeparator();
    QAction* quitAct = fileMenu->addAction("&Quit");
    quitAct->setShortcut(QKeySequence::Quit);
    QObject::connect(quitAct, &QAction::triggered, &app, &QApplication::quit);

    auto* placeholder = new QLabel("PZMapMaker — toolchain OK.\nOpen a map to begin.");
    placeholder->setAlignment(Qt::AlignCenter);
    window.setCentralWidget(placeholder);

    window.statusBar()->showMessage("Ready");

    window.show();
    return app.exec();
}
