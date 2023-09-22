#include "qtester.h"
#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication a(argc, argv);
    QTester w;
    w.show();
    return a.exec();
}
