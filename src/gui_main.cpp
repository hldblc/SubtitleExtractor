#include "MainWindow.h"

#include <QApplication>
#include <QStyleFactory>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    QApplication::setApplicationName("Subtitle Extractor");
    QApplication::setOrganizationName("Halit");
    QApplication::setApplicationVersion("1.0");
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    subext::MainWindow window;
    window.show();

    return app.exec();
}
