#include "qtester.h"
#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QTester w;
    w.show();
    return app.exec();
}
