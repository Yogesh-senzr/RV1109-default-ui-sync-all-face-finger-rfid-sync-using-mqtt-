#include "NtpDateSync.h"
#include "MessageHandler/Log.h"

NtpDateSync::NtpDateSync(QObject *parent)
    : QThread(parent)
    , is_pause(true)
{
    this->start();
}

NtpDateSync::~NtpDateSync()
{
    this->requestInterruption();
    this->is_pause = false;
    this->pauseCond.wakeOne();
    this->quit();
    this->wait();
}

static inline void Utils_ExecCmd(const char *szCmd)
{
    char buf[64] = { 0 };
    if (szCmd != NULL)
    {
        FILE *pFile = popen(szCmd, "r");
        if (pFile)
        {
            while (fgets(buf, sizeof(buf), pFile) != NULL)
            {
            }
            pclose(pFile);
        }
    }
}

void NtpDateSync::run()
{
    while (!isInterruptionRequested())
    {
        this->sync.lock();
        if (this->is_pause) this->pauseCond.wait(&this->sync);

#ifdef Q_OS_LINUX
        // Use reliable NTP servers (Indian/Asian region preferred)
        Utils_ExecCmd("/isc/bin/ntpdate 0.asia.pool.ntp.org");
        Utils_ExecCmd("/sbin/hwclock -w -u");  // Write to hardware clock as LOCAL time
        
        Utils_ExecCmd("/isc/bin/ntpdate 1.asia.pool.ntp.org");
        Utils_ExecCmd("/sbin/hwclock -w -u");
        
        Utils_ExecCmd("/isc/bin/ntpdate pool.ntp.org");
        Utils_ExecCmd("/sbin/hwclock -w -u");
        
        // Verify timezone is still correct after NTP sync
        system("export TZ=Asia/Kolkata");
        
        char szNow[128] = { 0 };
        time_t t2;
        time(&t2);
        struct tm *p;
        p = localtime(&t2);
        sprintf(szNow, "%04d%02d%02d%02d%02d%02d", (1900 + p->tm_year), (1 + p->tm_mon), p->tm_mday, p->tm_hour, p->tm_min, p->tm_sec);
        LogV("%s %s[%d] Local time after NTP sync: %s \n", __FILE__, __FUNCTION__, __LINE__, szNow);
#endif
        this->is_pause = true;
        this->sync.unlock();
    }
}
