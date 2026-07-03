#include "WatchDogManageThread.h"
#include "Config/ReadConfig.h"
#include "MessageHandler/Log.h"
#include "PCIcore/Watchdog.h"
#include "Helper/myhelper.h"
#include <QtCore/QDateTime>
#include<unistd.h>

// Add these static variables for face recognition monitoring
static time_t g_last_face_processing_time = 0;
static int g_total_frame_count = 0;
static int g_total_recognition_count = 0;
static bool g_face_system_responsive = true;
static time_t g_last_heartbeat_time = 0;

// Add these functions to be called from BaiduFaceManager
extern "C" {
    void WatchDog_UpdateFaceProcessing() {
        g_last_face_processing_time = time(NULL);
        g_total_frame_count++;
        g_face_system_responsive = true;
    }
    
    void WatchDog_UpdateRecognition() {
        g_total_recognition_count++;
        g_last_face_processing_time = time(NULL);
    }
    
    void WatchDog_SetFaceSystemHang() {
        g_face_system_responsive = false;
        LogE("WATCHDOG: Face system marked as hanging!\n");
    }
    
    void WatchDog_FaceHeartbeat() {
        g_last_heartbeat_time = time(NULL);
    }
}


WatchDogManageThread::WatchDogManageThread(QObject *parent)
    : QThread(parent)
{
    // Initialize face monitoring
    g_last_face_processing_time = time(NULL);
    g_last_heartbeat_time = time(NULL);
    
	if (access("/udisk/debug_mode", F_OK) && access("/udisk/update.img", F_OK))
	{
		YNH_LJX::Watchdog::WatchDog_OpenWatchDog();
		YNH_LJX::Watchdog::WatchDog_FeedWatchDog(-1);
	}
    this->start();
}

WatchDogManageThread::~WatchDogManageThread()
{
    this->requestInterruption();
    this->pauseCond.wakeOne();

    this->quit();
    this->wait();
}

void WatchDogManageThread::run()
{
    int watchdog_cycle_count = 0;
    time_t last_face_check = time(NULL);
    
    while (!isInterruptionRequested())
    {
        this->sync.lock();
        watchdog_cycle_count++;
        time_t current_time = time(NULL);
        
        //喂看门狗
        if (access("/udisk/debug_mode", F_OK) && access("/udisk/update.img", F_OK))
		{
        	YNH_LJX::Watchdog::WatchDog_FeedWatchDog(-1);
		}else
		{
			YNH_LJX::Watchdog::WatchDog_CloseWatchDog();
		}

        // Original reboot timer functionality (KEPT INTACT)
        if(ReadConfig::GetInstance()->getMaintenance_boot() != 0)
        {
            QDateTime curDateTime = QDateTime::currentDateTime();
            QString strBootTime = ReadConfig::GetInstance()->getMaintenance_bootTimer();
            QString strBootDateTime = curDateTime.toString("yyyy/MM/dd")+" " + strBootTime;
            QDateTime bootDateTime = QDateTime::fromString(strBootDateTime,"yyyy/MM/dd hh:mm:ss");

            if(bootDateTime.secsTo(curDateTime) > 10 && bootDateTime.secsTo(curDateTime) < 20)
            {
            	LogD("%s %s[%d] time to reboot \n",__FILE__,__FUNCTION__,__LINE__);
            	myHelper::Utils_Reboot();
            }
        }

        this->pauseCond.wait(&this->sync, 3000);
        this->sync.unlock();
    }
}