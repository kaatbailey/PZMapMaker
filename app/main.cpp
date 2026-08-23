#include "mainwindow.hpp"

#include <QApplication>
#include <QSurfaceFormat>

int main(int argc, char** argv) {
    // Set the default GL format BEFORE QApplication so any context Qt creates on
    // this Wayland session is 4.5 core — matching QOpenGLFunctions_4_5_Core in
    // MapView. MapView also sets this per-window, but the default is the belt to
    // that suspenders: some Wayland setups honour the per-window request only if
    // the default is already compatible.
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setVersion(4, 5);
    fmt.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    QApplication::setApplicationName("PZMapMaker");
    QApplication::setOrganizationName("PZMapMaker");

    pzmm::MainWindow window;
    window.show();
    return app.exec();
}