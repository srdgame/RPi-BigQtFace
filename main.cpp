#include "Face.h"
#include "Logger.h"
#include "GPIOAdapter.h"
#include <QApplication>
#include <QSqlDatabase>
int main(int argc, char *argv[])
{
	QApplication a(argc, argv);
    //Prepare SQLite Database
    QSqlDatabase defaultDatabase=QSqlDatabase::addDatabase("QSQLITE");
    defaultDatabase.setDatabaseName(qApp->applicationDirPath()+"/Face.db");
    defaultDatabase.open();
    //Instance EventServer
    EventServer::instance();
    //Instance Logger
    Logger::instance();
    //Instance GPIOAdapter
    GPIOAdapter::instance();
    //Instance Face
	Face w;
	w.show();
	return a.exec();
}
