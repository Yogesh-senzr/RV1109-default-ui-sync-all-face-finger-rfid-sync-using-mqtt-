#include "ShrinkFaceImageThread.h"
#ifdef Q_OS_LINUX
#include "PCIcore/Watchdog.h"
#include "SettingFuncFrms/SysSetupFrms/StorageCapacityFrm.h"
#include<unistd.h>
#endif
#include "Application/FaceApp.h"
#include "FaceMainFrm.h"

#include <QSqlDriver>
#include <QProcess>
#include <QDebug>
#include <QDateTime>
#include <QStorageInfo>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QDate>  
#include <QSqlError> 

static inline int queryRowCount(QSqlQuery &query)
{
    int initialPos = query.at();
    // Very strange but for no records .at() returns -2
    int pos = 0;
    if (query.last()) {
        pos = query.at() + 1;
    }
    else {
        pos = 0;
    }
    // Important to restore initial pos
    query.seek(initialPos);
    return pos;
}


ShrinkFaceImageThread::ShrinkFaceImageThread(QObject *parent)
    : QThread(parent)
{
    this->start();
}

ShrinkFaceImageThread::~ShrinkFaceImageThread()
{
    this->requestInterruption();
    this->pauseCond.wakeOne();

    this->quit();
    this->wait();
}
void ShrinkFaceImageThread::doShrinkFaceImage()
{
	char cmdline[256];
	QString maxUuid;

	int shrinkCount=0;
	shrinkCount = StorageCapacityFrm::GetInstance()->getCountTotal()*0.1;  //总记录数的 10%
	
	//查询出符合删除条件的记录,并取出最大日期 

#ifdef Q_OS_LINUX		
	    QSqlQuery query(QSqlDatabase::database("isc_ir_arcsoft_face"));

		query.prepare(QString("select max(time)as maxuuid from ( select * from identifyrecord order by time limit %1)").arg(shrinkCount));
		query.exec();
		
		while(query.next()) {
			QString result = query.value(0).toString();
			maxUuid = query.value(0).toString();
			//qDebug()<<"maxUuid: "<<maxUuid<<";shrinkCount="<<shrinkCount<<",sql="<<QString("select max(time)as maxuuid from ( select * from identifyrecord order by time limit %1)").arg(shrinkCount);			
		}		
#endif 		

	if (maxUuid.isEmpty() )  return;



	//批量删除记录
	//QString sql = " sqlite3  /mnt/user/facedb/isc_ir_arcsoft_face.db 'delete from identifyrecord where  time<='"+ maxUuid+"'  ;";
	//QString sql = " sqlite3  /mnt/user/facedb/isc_ir_arcsoft_face.db 'delete from identifyrecord where  time<="""""+ maxUuid+""""" '  ;";
	sprintf(cmdline,"sqlite3  /mnt/user/facedb/isc_ir_arcsoft_face.db 'delete from identifyrecord where  time<=\"%s\" '  ;",maxUuid.toStdString().data());	  	  
	qDebug()<<"sql: "<<cmdline;

	system(cmdline);
	
	
	//转换日期模式
	QDateTime time = QDateTime::fromString(maxUuid,"yyyy/MM/dd hh:mm:ss");	
	maxUuid = time.toString("yyyy-MM-dd hh:mm:ss");
	//批量删除图片
#if 0
/mnt/user/face_crop_image/2021-12-08/Full_202112080129845_10.jpg
/mnt/user/face_crop_image/2021-12-08/Full_202112080138124_252.jpg
#endif 	
    //参考语句        find /mnt/user/face_crop_image/ -name *.jpg | grep Full | awk -F'_' ' {if (substr($3,7,10)""substr($4,9,2) ":" substr($4,11,2) ":" substr($4,13,2)>="2021-12-08 01:28:40") {print $0 }} '
	sprintf(cmdline,"find /mnt/user/face_crop_image/ -name *.jpg | grep Full | awk -F'_' ' {if (substr($3,7,10)\"\"substr($4,9,2) \":\" substr($4,11,2) \":\" substr($4,13,2)>=\"%s\") {print $0 }} ' | xargs rm -rf ",maxUuid.toUtf8().data());	  	 

	qDebug()<<"cmdline Full: "<<cmdline;
	
	system(cmdline);
#if 0
/mnt/user/face_crop_image/2021-12-08/202112080137431_182.jpg
/mnt/user/face_crop_image/2021-12-08/202112080129959_11.jpg
/mnt/user/face_crop_image/2021-12-08/202112080137202_182.jpg
#endif 	
	//此句 是 tid_
	//sprintf(cmdline,"find /mnt/user/face_crop_image/ -name *.jpg  | awk -F'_' ' {if ($7\"\"substr($8,1,2) \":\" substr($8,3,2) \":\" substr($8,5,2)>=\"%s\") {print $0 }} ' | xargs rm -rf ",maxUuid.toUtf8().data());	  	 

	// 参考语句: find /mnt/user/face_crop_image/ -name *.jpg  | awk -F'_' ' {if (substr($3,7,10)""substr($3,26,2) ":" substr($3,28,2) ":" substr($3,30,2)>="2021-12-08 01:28:40") {print $0}} ' 

	sprintf(cmdline,"find /mnt/user/face_crop_image/ -name *.jpg  | awk -F'_' ' {if (substr($3,7,10)\"\"substr($3,26,2) \":\" substr($3,28,2) \":\" substr($3,30,2)>=\"%s\") {print $0 }} ' | xargs rm -rf ",maxUuid.toUtf8().data());	  	 

	qDebug()<<"cmdline: "<<cmdline;
	
	system(cmdline);

}

bool ShrinkFaceImageThread::doShrinkFaceImageByFolderOnce()
{
    QString attendanceDir = "/mnt/user/face_crop_image/";
    QDir dir(attendanceDir);
    dir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
    dir.setSorting(QDir::Name);

    QStringList dateFolders = dir.entryList();
    if (dateFolders.isEmpty()) {
        return false;
    }

    // Oldest folder = first in sorted list
    QString oldestFolder = dateFolders.first();
    QString folderPath = attendanceDir + oldestFolder;

    qDebug() << "Deleting oldest folder:" << oldestFolder;

    // Delete folder recursively (Qt version, avoids system("rm -rf"))
    removeDirectoryRecursively(folderPath);

    // Also clean database records for that date
#ifdef Q_OS_LINUX
    QSqlQuery query(QSqlDatabase::database("isc_ir_arcsoft_face"));
    QDate date = QDate::fromString(oldestFolder, "yyyy-MM-dd");
    if (date.isValid()) {
        QString start = date.toString("yyyy/MM/dd") + " 00:00:00";
        QString end   = date.toString("yyyy/MM/dd") + " 23:59:59";
        QString sql = QString("DELETE FROM identifyrecord WHERE time >= '%1' AND time <= '%2'")
                        .arg(start, end);
        if (!query.exec(sql)) {
            qDebug() << "Database cleanup failed for" << oldestFolder << ":" << query.lastError().text();
        } else {
            qDebug() << "Database cleanup done for" << oldestFolder;
        }
    }
#endif

    return true;
}

// Helper function to remove directory recursively
bool ShrinkFaceImageThread::removeDirectoryRecursively(const QString &dirPath)
{
    QDir dir(dirPath);
    
    if (!dir.exists()) {
        return false;
    }
    
    bool success = true;
    QStringList files = dir.entryList(QDir::Files);
    
    int processedFiles = 0;
    for (const QString &fileName : files) {
        QString filePath = dirPath + "/" + fileName;
        if (!QFile::remove(filePath)) {
            success = false;
        }
        
        processedFiles++;
        // YIELD CPU every 50 files to prevent blocking
        if (processedFiles % 50 == 0) {
            QThread::msleep(10);
        }
    }
    
    return success;
}


void ShrinkFaceImageThread::run()
{
    // Set LOW PRIORITY for this thread so it doesn’t interfere with camera
    this->setPriority(QThread::LowestPriority);

    static int monitorCycleCount = 0;

    while (!isInterruptionRequested())
    {
#ifdef Q_OS_LINUX
        monitorCycleCount++;

        // Monitor every 5 minutes (20 × 15s = 300s = 5 minutes)
        if (monitorCycleCount % 20 == 0) {
            QStorageInfo storage("/mnt/user");
            storage.refresh();
            qint64 bytesFree = storage.bytesFree();
            qint64 bytesTotal = storage.bytesTotal();
            int freePercent = (bytesFree * 100) / bytesTotal;

            qDebug() << "Storage check - Free:" << (bytesFree / (1024*1024)) << "MB"
                     << "of" << (bytesTotal / (1024*1024)) << "MB"
                     << "(" << freePercent << "% free )";

            // Cleanup trigger when storage free < 12%
            if (freePercent < 12) {
                qDebug() << "Low storage detected (" << freePercent << "% free), starting cleanup...";

                this->setPriority(QThread::IdlePriority); // Lower priority while cleaning

                // Batch cleanup loop: remove oldest folders until free space > 15%
                while (freePercent < 15) {
                    if (!doShrinkFaceImageByFolderOnce()) {
                        qDebug() << "No more folders to delete.";
                        break;
                    }

                    // Refresh after each batch
                    storage.refresh();
                    bytesFree = storage.bytesFree();
                    freePercent = (bytesFree * 100) / bytesTotal;

                    qDebug() << "Cleanup progress: now" << freePercent << "% free";
                }

                this->setPriority(QThread::LowestPriority); // Restore monitoring mode
                qDebug() << "Cleanup done, back to rest mode.";
            }
        }
#endif
        msleep(15*1000); // Sleep 15s between cycles
    }
}