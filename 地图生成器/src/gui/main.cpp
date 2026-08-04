#include <QApplication>
#include <QFile>
#include <QIcon>
#include "mainwindow.h"

/* 从 Qt 资源加载 QSS 样式表;失败时回退到无样式(控件仍可用) */
static QString loadStylesheet()
{
    QFile f(QStringLiteral(":/style.qss"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll());
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    app.setStyleSheet(loadStylesheet());
    app.setWindowIcon(QIcon(QStringLiteral(":/images/world-generator-icon.png")));

    MainWindow w;
    w.show();
    return app.exec();
}
