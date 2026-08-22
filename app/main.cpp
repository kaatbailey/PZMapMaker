#include "mainwindow.hpp"

#include <QApplication>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("PZMapMaker");
    QApplication::setOrganizationName("PZMapMaker");

    pzmm::MainWindow window;
    window.show();
    return app.exec();
}