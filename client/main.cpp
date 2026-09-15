#include "mainwindow.h"

#include <QApplication>
#include <QDir>
#include<QStandardPaths>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QCoreApplication::setOrganizationName("QtLink");
    QCoreApplication::setApplicationName("QtLink");

    //获取QtLink的标准可写应用数据目录
    const QString appDataPath=QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    QDir directory;
    if(!directory.mkpath(appDataPath)){
        return -1;
    }
    const QString databasePath=QDir(appDataPath).filePath("qtlink.db");
    qDebug() << "Database path:"
             << databasePath;
    MainWindow w(databasePath);
    w.show();
    return QApplication::exec();
}
