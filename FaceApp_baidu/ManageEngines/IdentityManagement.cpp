#include "IdentityManagement.h"
#include "IdentityManagementPrivate.h"

#include "FaceDataResolverObj.h"
#include "ScanningPersonRecord.h"
#include "PersonRecordToDB.h"

#include "PCIcore/SensorManager.h"
#include "PCIcore/Audio.h"
#include "PCIcore/Utils_Door.h"

#include "json-cpp/json.h"
#include "Helper/myhelper.h" 

#include "BaiduFace/BaiduFaceManager.h"
#include "HealthCodeDevices/HealthCodeManage.h"
#include "PCIcore/RkUtils.h"
#include <sys/stat.h>

#include "Delegate/Toast.h"
#include "Config/ReadConfig.h"
#include "MessageHandler/Log.h"
#include "Threads/powerManagerThread.h"
#include "DB/RegisteredFacesDB.h"
#include "Application/FaceApp.h"
#include "PCIcore/GPIO.h"

#include <QtCore/QMutex>
#include <QtCore/QWaitCondition>
#include <QtCore/QQueue>
#include <QtCore/QDebug>
#include <time.h>
#include <QtCore/QDateTime>

#include <curl/curl.h>

#include <QMetaEnum>
#include <QtCore/QTime> 

#define DateTime QDateTime::currentDateTime().toString("yyyy/MM/dd hh:mm:ss")
#define Today QDateTime::currentDateTime().toString("yyyy-MM-dd")
#define OutTimer (20)
extern double gAlogStateFaceSimilar; //全局人脸对比阈值

IdentityManagement::IdentityManagement(IdentityManagementPrivate *q, QObject *parent)
    : QThread(parent)
    , d_ptr(q)
{
    this->start();
}

IdentityManagement::IdentityManagement(QObject *parent)
    : QThread(parent)
    , d_ptr(new IdentityManagementPrivate(this))  
{
    this->start();
    printf("%s %s[%d] IdentityManagement %p \n",__FILE__,__FUNCTION__,__LINE__,this);
}

IdentityManagement::~IdentityManagement()
{
    Q_D(IdentityManagement);
    this->requestInterruption();
    d->sync.lock();
    for (int i = 0; i < d->m_ThreadObjs.count(); i++)
    {
        delete d->m_ThreadObjs.at(i);
    }

    d->sync.unlock();

    d->pauseCond.wakeOne();
    this->quit();
    this->wait();
}

void IdentityManagement::setVivoDetection(const bool &b)
{
    Q_D(IdentityManagement);
    d->mVivoDetection = b;
}

void IdentityManagement::setAlarmTemp(const float &value)
{
    Q_D(IdentityManagement);
    d->mAlarmTemp = value;
}

void IdentityManagement::setMask_value(const float &value)
{
    Q_D(IdentityManagement);
    d->mMaskValue = value;
}

void IdentityManagement::setFqThreshold(const float &value)
{
    Q_D(IdentityManagement);
    d->mFqThreshold = value;
}

void IdentityManagement::setThanthreshold(const float &value)
{
    Q_D(IdentityManagement);
    d->mThanthreshold = value;
}

void IdentityManagement::setidcardThanthreshold(const float &value)
{
    Q_D(IdentityManagement);
    d->midcardThanthreshold = value;
}

void IdentityManagement::setIdentifyInterval(const int &value)
{
    Q_D(IdentityManagement);
    d->mIdentifyInterval = value;
}

void IdentityManagement::setDoor_MustOpenMode(const QString &mode)
{
    Q_D(IdentityManagement);
    d->mMustOpenMode = mode;//开门模式必选项
}

void IdentityManagement::setDoor_OptionalOpenMode(const QString &mode)
{
    Q_D(IdentityManagement);
    d->mOptionalOpenMode = mode;//开门模式可
}

void IdentityManagement::ShowLRHealthCodeAndOpenDoor(HEALTINFO_t info,const int nKind) //显示健康码并开门
{
    Q_D(IdentityManagement);    
    if (d->mCurTemperatureValue >= d->mAlarmTemp  ) //37.3 //体温过高
    {
        return ;
    }
    //d->mIdentifyFaceRecord.Languagetips = true; //不提示其它语音

    info.warningTemp = d->mCurTemperatureValue; //当前体温
    info.temperature = QString("%1").arg(d->mCurTemperatureValue); //当前体温
        
    int hshours = 1000;
    if (!info.hsdateflag.isEmpty())
    {
        hshours = info.hsdateflag.toInt();
    }
    else if ((info.hsdateflag.isEmpty() && !info.hsdatetime.isEmpty()) || !info.hsdatetime.isEmpty())
    {
        //"hsjcsj":"2022-04-20 04:50:57"
        QDateTime time = QDateTime::currentDateTime();
        QDateTime start = QDateTime::fromString(info.hsdatetime,"yyyy-MM-dd hh:mm:ss");
        
        hshours = start.secsTo(time)/3600;
        
        if (hshours<=24)  info.hsdateflag="24";
        else if (hshours<=48)  info.hsdateflag="48";
        else if (hshours<=72)  info.hsdateflag="72";
        else info.hsdateflag="72greater";      

        if (nKind==2) //身份证
        {
            if (hshours<=24)  info.hsdateflag="24";
            else if (hshours<=48)  info.hsdateflag="48";
            else if (hshours<=72)  info.hsdateflag="72";
            else info.hsdateflag=">72";   
        }      
    }

    //if(d->mMustOpenMode.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::QRCODE_LOCAL))
    if (d->mMustOpenMode.contains("8")   ||  d->mOptionalOpenMode.contains("8"))
    {
        emit sigShowLRHealthCode(info); //显示绿码 //显示出来,不受限制
    }
        //开门处理    void ShowLRHealthCodeAndOpenDoor(HEALTINFO_t info); //显示健康码并开门   
    LogD(">>>%s,%s,%d,qrcode=%d\n",__FILE__,__func__,__LINE__,info.qrcode);      
    if (info.qrcode == 1 ) //绿码开门处理　
    {
        //d->mHealthCode.color = info.color.toInt();
        //24 小时          
        if (d->mMustOpenMode.contains(d->Door_OpenModeToSTR(_DOOR_OPEN_MODE::QRCODE_LOCAL)) 
          ||  d->mOptionalOpenMode.contains(d->Door_OpenModeToSTR(_DOOR_OPEN_MODE::QRCODE_LOCAL)) )
        {
            int cfghshours =0;//  ReadConfig::GetInstance()->getDoor_hshours();
            switch (cfghshours) 
            {
                case 0: if (hshours<=24) {// 可开门 mIdentifyFaceRecord.process_state.append("&4"); 
                      d->mIdentifyFaceRecord.process_state.append("&8");
                    }
                    break;
                case 1: if (hshours<=48) {// 可开门
                    d->mIdentifyFaceRecord.process_state.append("&8");  
                    }
                    break;
                case 2: if (hshours<=72) {// 可开门
                    d->mIdentifyFaceRecord.process_state.append("&8");
                    }
                    break;
                case 3:  {// 可开门
                    d->mIdentifyFaceRecord.process_state.append("&8");
                    }
                    break;
            }
      
        }
        if (nKind==2) //身份证
        {
            int cfghshours = 0;// ReadConfig::GetInstance()->getDoor_hshours();
            switch (cfghshours) 
            {
                case 0: if (hshours<=24) {// 可开门 mIdentifyFaceRecord.process_state.append("&4"); 
                    d->mIdentifyFaceRecord.process_state.append("&6");
                    }
                    break;
                case 1: if (hshours<=48) {// 可开门
                    d->mIdentifyFaceRecord.process_state.append("&6");  
                    }
                    break;
                case 2: if (hshours<=72) {// 可开门
                    d->mIdentifyFaceRecord.process_state.append("&6");
                    }
                    break;
                case 3:  {// 可开门
                    d->mIdentifyFaceRecord.process_state.append("&6");
                    }
                    break;
            }            
        }

        if (d->CanOpenDoor())
        {            
            //开门
            YNH_LJX::Audio::Audio_PlayCustomerPcm("zh", "greenplsPass.wav", true);            
			int iRelay =  ReadConfig::GetInstance()->getDoor_Relay();
		    LogD(">>>%s,%s,%d,iRelay=%d\n",__FILE__,__func__,__LINE__,iRelay);
			if (iRelay == 1) //1:常开
			{
				YNH_LJX::GPIO::Device_SetDeviceState(DEVICE_Relay, 1);
			}
			else if (iRelay == 2) //2:常闭
			{        
				YNH_LJX::GPIO::Device_SetDeviceState(DEVICE_Relay, 0);
			} 
			else 				
				YNH_LJX::Utils_Door::GetInstance()->OpenDoor("");
            
            d->mIdentifyFaceRecord.face_idcardnum=info.idnumber;
            d->mIdentifyFaceRecord.face_name=info.name;
            //face_aids //开门组合
            PersonRecordToDB::GetInstance()->appRecordData(d->mIdentifyFaceRecord);

            //slotDisClearMessage();
            //d->mIdentifyFaceRecord.Finish = true; 
            //d->mIdentifyFaceRecord.Languagetips = false; 
        }	
    }                
}

void IdentityManagementPrivate::CheckMaskMode(const CORE_FACE_S &face)
{//有口罩 
    if(!CheckLiveness(face))return;//检测活体
    else if(face.attr_info.quality>=this->mMaskValue)
    {//有口罩模式下只有图片质量大于对比值对处理
        if(mIdentifyFaceRecord.time_Start == 0)
        {//默认状态
            mIdentifyFaceRecord.face = face;
            mIdentifyFaceRecord.temp_value = this->mCurTemperatureValue;
            mIdentifyFaceRecord.FaceImgPath = QString("/mnt/user/face_crop_image/%1/%2_%3.jpg").arg(Today).arg(QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz")).arg(face.track_id);
            mIdentifyFaceRecord.FaceFullImgPath = QString("/mnt/user/face_crop_image/%1/Full_%2_%3.jpg").arg(Today).arg(QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz")).arg(face.track_id);

            mIdentifyFaceRecord.time_End = mIdentifyFaceRecord.time_Start = (double)clock();//解发超时起始时间
            mIdentifyFaceRecord.TimeoutSec = 60;//超时多少秒
            mIdentifyFaceRecord.face_aids = this->mMustOpenMode;
        }else if((face.attr_info.quality > mIdentifyFaceRecord.face.attr_info.quality)
                 || (mIdentifyFaceRecord.face.attr_info.face_mask != face.attr_info.face_mask))
        {//新的图片质量比旧的还要好
            if(this->ComparingFace(face, mIdentifyFaceRecord))
            {//是否同一个人，相同
                CORE_FACE_RECT_TYPE_EN enFaceType = mIdentifyFaceRecord.face.enFaceType;
                mIdentifyFaceRecord.face = face;
                mIdentifyFaceRecord.face.enFaceType = enFaceType;
                mIdentifyFaceRecord.temp_value = this->mCurTemperatureValue;
            }else
            {//不相同那么删除旧的信息
                #if 0
                    mIdentifyFaceRecord = {};// 不能清除,因要走完开门流程
                #else 
                    mIdentifyFaceRecord.face={};
                    mIdentifyFaceRecord.face.enFaceType= CORE_FACE_RECT_TYPE_UNKNOW;
                    mIdentifyFaceRecord.time_Start = 0;
                #endif                 
                this->CheckMaskMode(face);
            }
        }
    }    
    mIdentifyFaceRecord.face.attr_info.face_mask = face.attr_info.face_mask;
    if (mIdentifyFaceRecord.face.attr_info.face_mask ==1)
    {
        if (!mIdentifyFaceRecord.process_state.contains("&4"))    
        mIdentifyFaceRecord.process_state.append("&4");    
    }   
    if(this->mCurTemperatureValue)
    {
    	if(this->mCurTemperatureValue>=this->mAlarmTemp)
    	{
    		static int count = 0;
    		if(count++ == 30)
    		{
    			count = 0;
    			YNH_LJX::Audio::Audio_PlayTempabnormalPcm("zh");
    		}
    	}

        if (this->mMustOpenMode.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::THERMOMETRY)) ||  this->mOptionalOpenMode.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::THERMOMETRY)))
        {                        	
            emit q_func()->sigTipsMessage(ALARM_MESSAGE, 2, QObject::tr("Temperature:%1").arg(QString::number(this->mCurTemperatureValue, 'f', 1)));
        }
    }
}

void IdentityManagementPrivate::CheckNotMaskMode(const CORE_FACE_S &face)
{//没有口罩
    if(!CheckLiveness(face))return;//检测活体
    else if(face.attr_info.quality>=this->mFqThreshold)
    {//没有口罩模式下只有图片质量大于对比值对处理  
        if(mIdentifyFaceRecord.time_Start == 0)
        {//默认状态
            mIdentifyFaceRecord.face = face;
            mIdentifyFaceRecord.temp_value = this->mCurTemperatureValue;
            if (mIdentifyFaceRecord.face.FaceImgPath>"")
            {
                mIdentifyFaceRecord.FaceImgPath = mIdentifyFaceRecord.face.FaceImgPath;                
            } 
            else 
            {
                mIdentifyFaceRecord.FaceImgPath = QString("/mnt/user/face_crop_image/%1/%2_%3.jpg").arg(Today).arg(QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz")).arg(face.track_id);
            }
            mIdentifyFaceRecord.FaceFullImgPath = QString("/mnt/user/face_crop_image/%1/Full_%2_%3.jpg").arg(Today).arg(QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz")).arg(face.track_id);

            mIdentifyFaceRecord.time_End = mIdentifyFaceRecord.time_Start = (double)clock();//解发超时起始时间
            mIdentifyFaceRecord.TimeoutSec = 60;//超时多少秒
            mIdentifyFaceRecord.face_aids = this->mMustOpenMode;
        }else if((face.attr_info.quality > mIdentifyFaceRecord.face.attr_info.quality)
                 || (mIdentifyFaceRecord.face.attr_info.face_mask != face.attr_info.face_mask))
        {//新的图片质量比旧的还要好          
            if(this->ComparingFace(face, mIdentifyFaceRecord) )
            {//是否同一个人，相同
                CORE_FACE_RECT_TYPE_EN enFaceType = mIdentifyFaceRecord.face.enFaceType;
                mIdentifyFaceRecord.face = face;
                mIdentifyFaceRecord.face.enFaceType = enFaceType;
                mIdentifyFaceRecord.temp_value = this->mCurTemperatureValue;
            }else
            {//不相同那么删除旧的信息
                #if 0
                    mIdentifyFaceRecord = {};// 不能清除,因要走完开门流程
                #else 
                    mIdentifyFaceRecord.face={};
                    mIdentifyFaceRecord.face.enFaceType= CORE_FACE_RECT_TYPE_UNKNOW;
                    mIdentifyFaceRecord.time_Start = 0;
                #endif 

                this->CheckNotMaskMode(face);
            }            
        }        
    }
    mIdentifyFaceRecord.face.attr_info.face_mask = face.attr_info.face_mask;
    if(this->mCurTemperatureValue)
    {
    	if(this->mCurTemperatureValue>=this->mAlarmTemp)
    	{
    		static int count = 0;
    		if(count++ == 30)
    		{
    			count = 0;
    			YNH_LJX::Audio::Audio_PlayTempabnormalPcm("zh");
    		}
    	}

        if (this->mMustOpenMode.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::THERMOMETRY)) ||  this->mOptionalOpenMode.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::THERMOMETRY)))
        {
            if (this->mCurTemperatureValue>=this->mAlarmTemp)         
            {            	
                emit q_func()->sigTipsMessage(TOP_MESSAGE, 2, QObject::tr("<font size=20 color=red>Temperature:%1</font>").arg(QString::number(this->mCurTemperatureValue, 'f', 1)));
                qXLApp->GetPowerManagerThread()->setIdentifyState(false);
            }
            else 
            {            	
                emit q_func()->sigTipsMessage(TOP_MESSAGE, 2, QObject::tr("<font size=12 color=white>Temperature:%1</font>").arg(QString::number(this->mCurTemperatureValue, 'f', 1)));            
            }
        }
    }
} 

bool IdentityManagementPrivate::CheckLiveness(const CORE_FACE_S &face)
{//判断用户模式是否开启活体检测
    if(mVivoDetection && face.attr_info.liveness_ir == 1)return true;
    else if(mVivoDetection && face.attr_info.liveness_ir != 1)
    {        
        return false;
    }
    return (!mVivoDetection) ? true : false;
}

bool IdentityManagementPrivate::CheckMaskState(const CORE_FACE_S &face)
{
    return (face.attr_info.face_mask == 0);
}

bool IdentityManagementPrivate::CheckTempState()
{/*判断体温检测*/
    //体温异常重读
    if(this->mCurTemperatureValue == 0.0)this->mCurTemperatureValue = qXLApp->GetSensorManager()->GetSensorFloatValue();
    if(this->mCurTemperatureValue>=this->mAlarmTemp) return true;
    return false;
}

bool IdentityManagementPrivate::ComparingFace(const CORE_FACE_S &face, const IdentifyFaceRecord_t &oldFace)
{//找查队列所有信息进行比较
    Q_UNUSED(face);
    Q_UNUSED(oldFace);
    double similar = ((BaiduFaceManager *)qXLApp->GetAlgoFaceManager())->getFaceFeatureCompare(face.pFaceFeature, face.nFaceFeatureSize,  oldFace.face.pFaceFeature, oldFace.face.nFaceFeatureSize);
    if(similar>=this->mThanthreshold)return true;
    return false;
}

#if 0
开门模式: 必选， 可选

必选项:刷卡,刷脸,测温,口罩,二维码,身份证，人证比对
可选项:刷卡,刷脸,测温,口罩,二维码,身份证 

1.必选项,必须全部满足
2.可行项:满足条件之一
3.同时有必选项,可选项
  必选项 全部满足,同时,可选项至少满足一项
4.人证比对: 刷脸是必须的对应是人,证是身份证 
5.刷卡是批 IC卡或 韦根
6.其它相关 函数:
    slotReadIccardNum  //刷卡  √
    slotHealthCodeInfo	//二维码	√
    slotIdentityCardInfo  //身份证	√
    slotTemperatureValue //测温 √
    EchoFaceRecognition  //刷脸 √
    CheckMaskMode //口罩	√
    slotLRHealthCodeInfo2 //本地码
#endif     

bool  IdentityManagementPrivate::CanOpenDoor()
{
    printf(">>>>>%s,%s,%d CheckPassageOfTime\n",__FILE__,__func__,__LINE__);
    if(!RegisteredFacesDB::GetInstance()->CheckPassageOfTime(this->mIdentifyFaceRecord.face_uuid))
    {
        printf(">>>>>%s,%s,%d CheckPassageOfTime\n",__FILE__,__func__,__LINE__);
        emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("CheckPassageOfTimeHint"));//当前时段禁止通行,No traffic in the current period.
        return false;
    }     
    //是否超时
    int passtimer = ((double)clock()- this->mIdentifyFaceRecord.time_Start)/1000/1000;   // 1000/1000;  CLOCK_REALTIME

    
    if (mMustOpenMode.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE)) 
        ||  mOptionalOpenMode.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE)) )
    {
        if(this->mIdentifyInterval &&  (passtimer >= this->mIdentifyInterval) )
        {
            //清空信息 
            q_func()->slotDisClearMessage();   
            return false;
        }
        if (this->mIdentifyFaceRecord.Finish ) //如果完成此流程则退出
        return false;        
    }    

    //能否开门
    QString door_mode = this->mMustOpenMode;
    QString process = this->mIdentifyFaceRecord.process_state;
    QString optional_mode = this->mOptionalOpenMode;

    //必选项
    bool bMust  = false;

    QList<bool> bList;

    bList<<false<<false<<false<<false<<false<<false<<false;

    for (int i=1;i<8;i++)
    {
        QString subStr=Door_OpenModeToSTR((_DOOR_OPEN_MODE)i);       
         if  (door_mode.contains(subStr) && process.contains(subStr )) bList[i-1] = true;
         else if (!door_mode.contains(subStr))  bList[i-1] = true; 
    }
   

    if ( bList[0] && bList[1] && bList[2] && bList[3] && bList[4] && bList[5] && bList[6]    )
        bMust = true;
    else bMust = false;

    if (door_mode.isEmpty())
      bMust = true;
   
    //可选项
    bool bOptional = false;


    for (int i=1;i<7;i++)
    {

       QString subStr=Door_OpenModeToSTR((_DOOR_OPEN_MODE)i);
    
       if(optional_mode.contains(subStr) && process.contains(subStr) ) 
       {
         bOptional =true;
         break;
       }
    }
    if (optional_mode.isEmpty())
       bOptional = true;

    if (bMust && bOptional )	  
       return true;
    else 
      return false;

}

bool IdentityManagementPrivate::CheckDoorOpenMode(IdentifyFaceRecord_t &t)
{
    if ( ((BaiduFaceManager *)qXLApp->GetAlgoFaceManager())->getRegFaceState() )
       return false;
    QString door_mode = this->mMustOpenMode;
    QString process = t.process_state;
    
    QString optional_mode = this->mOptionalOpenMode;
    door_mode = ReadConfig::GetInstance()->getDoor_MustOpenMode();
    optional_mode = ReadConfig::GetInstance()->getDoor_OptionalOpenMode();

    if(door_mode.size() == 0 && optional_mode.size() == 0)
    {
        // TODO: Face recognition cannot be cancelled. If face recognition is not mandatory,
        // force configuration of face recognition for smooth process flow
        // If nothing is selected, default to BOTH IC card AND face recognition
        door_mode = Door_OpenModeToSTR(_DOOR_OPEN_MODE::ICCARD) + "&" + Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE);
    }

    printf(">>>>>%s,%s,%d CheckPassageOfTime\n",__FILE__,__func__,__LINE__);
    if(!RegisteredFacesDB::GetInstance()->CheckPassageOfTime(t.face_uuid))
    {
        printf(">>>>>%s,%s,%d CheckPassageOfTime\n",__FILE__,__func__,__LINE__);
        emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("CheckPassageOfTimeHint"));//当前时段禁止通行,No traffic in the current period.
        return false;
    } 
//必选项 
    //ICCARD = 1,//刷卡
    if(door_mode.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::ICCARD))
             && !process.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::ICCARD)))
    {//提示刷卡     	
        if(this->micCard.number.isEmpty() || micCard.number=="000000")
        {//卡号是空的， 提示刷卡
            emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("Plsswipe"));//请刷卡
        }else if(((t.time_End - this->micCard.timer)/1000/1000)>=20)
        {//卡号不为空,查找一下卡号是否存
            emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("PlsswipeAgain"));//请重刷卡
        }else
        {//查询卡是否存
            PERSONS_s info;            
            if(RegisteredFacesDB::GetInstance()->selectICcardPerson(this->micCard.number, info))
            {
                if (this->micCard.number.endsWith(info.iccard) || info.iccard.endsWith(this->micCard.number))
                {
                    t.process_state.append("&1");   
                    t.FaceType = NOT_STRANGER;//是否陌生人, 1是陌生人，2注册人员， 0未识别    
                    t.face_personid = info.personid;//人脸库id
                    t.face_persontype = info.persontype;//是否陌生人（1陌生人，2非陌生人）                                                     

                }else emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("CardNumberNotExist"));//卡号不存在Card number does not exist
            }else emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("ErrorQueryingData"));//查询数据出错,Error in querying data
        }
    }       

    //SWIPING_FACE = 2,//刷脸
    if(door_mode.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE))
             && t.FaceType != NOT_STRANGER)
    {//刷脸,已经实别但是不通过      	
        //printf(">>>>>%s,%s,%d\n",__FILE__,__func__,__LINE__);
        //emit q_func()->sigTipsMessage(BOTTOM_MESSAGE, 3, QObject::tr("<font color=\"#FFFF33\">stranger</font>"));//<font color=\"#FFFF33\">陌生人</font>
        //return true;
        t.process_state.append("&2");
    }
    
    if(door_mode.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE))
             && !process.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE)))
    {//刷脸,已经实别但是没有处理过        
         emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, "");//正在识别人脸
    }
    //THERMOMETRY = 3,//测温    
    if(door_mode.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::THERMOMETRY))
             && (t.temp_value>=this->mAlarmTemp))
    {/*报体温异常*/
        YNH_LJX::Audio::Audio_PlayTempabnormalPcm("zh");
        emit q_func()->sigTipsMessage(ALARM_MESSAGE, 1, QObject::tr("DOOR_OPEN_MODE_THERMOMETRY_Hint"));//体温异常

    }
    //QRCODE = 5,//二维码
    if(door_mode.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::QRCODE))
             && !process.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::QRCODE)))
    {//提示二唯码
        if(this->mHealthCode.timer == 0)
        {//未刷过二维码
            emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("PlsswipeNHcode"));//请刷国康码Please swipe the national health code
        }else if(((t.time_End - this->mHealthCode.timer)/1000/1000)>=30)
        {
            emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("PlsswipeNHcodeAgain"));//请重刷国康码
        }else if(this->mHealthCode.qrcode == 0)
        {
            t.process_state.append("&5");
        }

    } 
    //IDCARD = 6,//身份证
    if (door_mode == Door_OpenModeToSTR(_DOOR_OPEN_MODE::IDCARD))
    {
        //只有 刷身份证 一项
        emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("PlsswipeIdCard"));//请刷身份证                
    }
    if(door_mode.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::IDCARD))
             && !process.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::IDCARD)))
    {//提示身份证

        if(this->mIdentityCard.idCard.isEmpty())
        {//卡号是空的， 提示刷卡
            if (door_mode == Door_OpenModeToSTR(_DOOR_OPEN_MODE::IDCARD))
            {
                //只有 刷身份证 一项
                emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("PlsswipeIdCard"));//请刷身份证                
            }
            else 
            {
                emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("PlsswipeIdCard"));//请刷身份证
            }
        }else if(((t.time_End - this->mIdentityCard.timer)/1000/1000)>=30)
        {//卡号不为空,查找一下卡号是否存
            emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("PlsswipeIdCardAgain"));//请重刷身份证
        }else
        {//查找身份
            PERSONS_s info;
            
            if(RegisteredFacesDB::GetInstance()->selectIDcardPerson(this->mIdentityCard.idCard, info))
            {
                if(info.idcard == this->mIdentityCard.idCard)
                {
                    t.process_state.append("&6");
                    t.FaceType = NOT_STRANGER;//是否陌生人, 1是陌生人，2注册人员， 0未识别     
                    t.face_personid = info.personid;//人脸库id
                    t.face_persontype = info.persontype;//是否陌生人（1陌生人，2非陌生人）                                   

                }
                else {
                   YNH_LJX::Audio::Audio_PlayCustomerPcm("zh","IdCardNoNotExist.wav",true);  
                   emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("IdCardNoNotExist"));//身份证不存在
                }
            }else emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("ErrorQueryingData"));//查询数据出错Error in querying data
        }
    }
    //PERSON_IDCARD =7,//人证比对
    if(door_mode.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::PERSON_IDCARD))
             && !process.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::PERSON_IDCARD)))
    {//提示人证比对
    	        
        if (!process.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE)))
        {
           //请注视屏幕!   
           YNH_LJX::Audio::Audio_PlayCustomerPcm("zh","PlsLookScreen.wav",false); 
        }

        if(this->mIdentityCard.idCard.isEmpty())
        {
            emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("PlsswipeIdCard"));//请刷身份证
        }else
        {//进行人脸比对
            double similar = 0.0;
            similar = ((BaiduFaceManager*)qXLApp->GetAlgoFaceManager())->getPersonIdCardCompare(mIdentifyFaceRecord.face.FaceImgPath);//mMatchCoreFace.FaceImgPath ,this->mIdentityCard.pathImage
            //similar = ((BaiduFaceManager*)qXLApp->GetAlgoFaceManager())->getPersonIdCardCompare_baidu(this->mIdentityCard.pathImage);
            LogD(">>>>%s,%s,%d,similar=%f,midcardThanthreshold=%f\n",__FILE__,__func__,__LINE__,similar,midcardThanthreshold);
            if(similar>=this->midcardThanthreshold)
            {
                t.FaceType = NOT_STRANGER;
                emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("PersonIdCardCompareOK"));//人证比对成功
                t.process_state.append("&7");

      
            
            }else 
            {
                emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("PersonIdCardCompareBad"));//非本人证件
                YNH_LJX::Audio::Audio_PlayCustomerPcm("zh", "PersonIdCardCompareBad.wav", false); //人证比对失败
            }
        }
    }
    //可选项   
    //刷卡,刷脸,测温,口罩,二维码,身份证 

    if(optional_mode.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::ICCARD))
             && !process.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::ICCARD)))
    {//提示刷卡 
        if(this->micCard.number.isEmpty())
        {//卡号是空的， 提示刷卡
            emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("Plsswipe"));//请刷卡
        }else if(((t.time_End - this->micCard.timer)/1000/1000)>=20)
        {//卡号不为空,查找一下卡号是否存
            emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("PlsswipeAgain"));//请重刷卡
        }else
        {//查询卡是否存
            PERSONS_s info;
            if(RegisteredFacesDB::GetInstance()->selectICcardPerson(this->micCard.number, info))
            {
                if (this->micCard.number.endsWith(info.iccard) || info.iccard.endsWith(this->micCard.number))
                {
                    t.process_state.append("&1");    
                    t.FaceType = NOT_STRANGER;//是否陌生人, 1是陌生人，2注册人员， 0未识别    
                    t.face_personid = info.personid;//人脸库id
                    t.face_persontype = info.persontype;//是否陌生人（1陌生人，2非陌生人）                                                     
                }else emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("CardNumberNotExist"));//卡号不存在Card number does not exist
            }else emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("ErrorQueryingData"));//查询数据出错,Error in querying data
        }
    }       
    
    //SWIPING_FACE = 2,//刷脸
    if(optional_mode.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE))
             && t.FaceType != NOT_STRANGER)
    {//刷脸,已经实别但是不通过  
        emit q_func()->sigTipsMessage(BOTTOM_MESSAGE, 3, QObject::tr("<font color=\"#FFFF33\">stranger</font>"));//<font color=\"#FFFF33\">陌生人</font>
        return true;

    }else if(optional_mode.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE))
             && !process.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE)))
    {//刷脸,已经实别但是没有处理过
         if (t.FaceType != NOT_STRANGER)
           t.process_state.append("&2");
         emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, "");//正在识别人脸
    }
    //THERMOMETRY = 3,//测温    
    if(optional_mode.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::THERMOMETRY))
             && (t.temp_value>=this->mAlarmTemp))
    {/*报体温异常*/
        YNH_LJX::Audio::Audio_PlayTempabnormalPcm("zh");
        emit q_func()->sigTipsMessage(ALARM_MESSAGE, 1, QObject::tr("DOOR_OPEN_MODE_THERMOMETRY_Hint"));//体温异常

    }

    //MASK = 4,//口罩
    if(  optional_mode.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::MASK))  
            )
    {//检测口罩
        //YNH_LJX::Audio::Audio_PlayUnbtMaskPcm("zh");//太频繁了
        if (t.face.attr_info.face_mask != 1) 
        {
           emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("DOOR_OPEN_MODE_MASK_Hint"));//请佩戴口罩 Please wear a mask.
        } else  t.process_state.append("&4");
         
    }
    
    //QRCODE = 5,//二维码
    if(optional_mode.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::QRCODE))
             && !process.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::QRCODE)))
    {//提示二唯码
        if(this->mHealthCode.timer == 0)
        {//未刷过二维码
            emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("PlsswipeNHcode"));//请刷国康码Please swipe the national health code
        }else if(((t.time_End - this->mHealthCode.timer)/1000/1000)>=30)
        {
            emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("PlsswipeNHcodeAgain"));//请重刷国康码
        }else if(this->mHealthCode.qrcode == 0)
        {
            t.process_state.append("&5");
        }

    }  
    //IDCARD = 6,//身份证
    if(optional_mode.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::IDCARD))
             && !process.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::IDCARD)))
    {//提示身份证
        if(this->mIdentityCard.idCard.isEmpty())
        {//卡号是空的， 提示刷卡
            emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("PlsswipeIdCard"));//请刷身份证
        }else if(((t.time_End - this->mIdentityCard.timer)/1000/1000)>=30)
        {//卡号不为空,查找一下卡号是否存
            emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("PlsswipeIdCardAgain"));//请重刷身份证
        }else
        {//查找身份
            PERSONS_s info;
            if(RegisteredFacesDB::GetInstance()->selectIDcardPerson(this->mIdentityCard.idCard, info))
            {
                if(info.idcard == this->mIdentityCard.idCard)
                {
                    t.process_state.append("&6");
                    t.FaceType = NOT_STRANGER;//是否陌生人, 1是陌生人，2注册人员， 0未识别
                    t.face_personid = info.personid;//人脸库id
                    t.face_persontype = info.persontype;//是否陌生人（1陌生人，2非陌生人）                    
                }
                else {
                   YNH_LJX::Audio::Audio_PlayCustomerPcm("zh","IdCardNoNotExist.wav",true);                    
                   emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("IdCardNoNotExist"));//身份证不存在
                }
            }else emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("ErrorQueryingData"));//查询数据出错Error in querying data
        }
    }

    //LogD(">>>%s,%s,%d\n",__FILE__,__func__,__LINE__);
    if (CanOpenDoor())  //if (bMust && bOptional )   
    {   
        return true;
    }
    
    return false;
}

void IdentityManagement::slotIdentityCardFullInfo(const QString name,const QString idCardNo,const QString address,const QString sex, 
        const QString nationality,const QString birthDate,const QString issuingAuthority,const QString startDate, const QString endDate)
{
    Q_D(IdentityManagement);
    if(d->mMustOpenMode.contains(d->Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE)) ||
       d->mOptionalOpenMode.contains(d->Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE)) )
    {} else    
    { 
        //非刷脸时,IC,身份证,二维码等的 超时处理
        int passtimer = ((double)clock()- d->mIdentifyFaceRecord.time_Start)/1000/1000;   // 1000/1000;  CLOCK_REALTIME
        if(d->mIdentifyInterval &&  (passtimer >= d->mIdentifyInterval) )
        {
           d->mIdentifyFaceRecord.time_Start=0;
           d->mIdentifyFaceRecord.time_End = d->mIdentifyFaceRecord.time_Start = (double)clock();//解发超时起始时间      

        }    
    }

    //保存到 d->mIdentifyFaceRecord 中
    if (d->mIDCardInfo==ISC_NULL)//ISC_NULL,nullptr
    {
        d->mIDCardInfo = (IDCardInfo*) YNH_LJX::RkUtils::Utils_Malloc(sizeof(IDCardInfo));
    }
    memset(d->mIDCardInfo,0x0,sizeof(IDCardInfo));  
    memcpy(d->mIDCardInfo->szName, name.toStdString().data(), name.toStdString().length()+1);
    memcpy(d->mIDCardInfo->szIDCardNum, idCardNo.toStdString().data(), idCardNo.toStdString().length()+1);
    memcpy(d->mIDCardInfo->szIDAddress, address.toStdString().data(), address.toStdString().length()+1);
    memcpy(d->mIDCardInfo->szSex, sex.toStdString().data(), sex.toStdString().length()+1);
    memcpy(d->mIDCardInfo->szNationality, nationality.toStdString().data(), nationality.toStdString().length()+1);
    memcpy(d->mIDCardInfo->szBirthDate, birthDate.toStdString().data(), birthDate.toStdString().length()+1);
    memcpy(d->mIDCardInfo->szIssuingAuthority, issuingAuthority.toStdString().data(), issuingAuthority.toStdString().length()+1);
    memcpy(d->mIDCardInfo->szStartDate, startDate.toStdString().data(), startDate.toStdString().length()+1);
    memcpy(d->mIDCardInfo->szEndDate, endDate.toStdString().data(), endDate.toStdString().length()+1);   
      
    //d->mReadCard = 1; //已经读卡
    d->mIdentityCard.idCard  = d->mIDCardInfo->szIDCardNum;
    d->mIdentityCard = {name, sex, idCardNo, "/mnt/user/facedb/img/idcard.jpg", QDateTime::currentDateTime().toTime_t()};
    d->mIdentifyFaceRecord.process_state.append("&6");
    
    emit sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("readIDCardSuccess:")+d->mIDCardInfo->szIDCardNum);//读取身份证成功
    YNH_LJX::Audio::Audio_PlayCustomerPcm("zh","readIDCardSuccess.wav",true);  

}        


void IdentityManagement::slotMatchCoreFace(const CORE_FACE_S face)
{
    Q_D(IdentityManagement);
    
    qDebug() << "📄 NEW FACE DETECTED - Clearing old text at" << QTime::currentTime().toString("hh:mm:ss.zzz");
    emit sigTipsMessage(BOTTOM_MESSAGE, 3, "");
    
    d->sync.lock();
    
    // CHECK DOOR MODE FIRST
    QString doorMode = ReadConfig::GetInstance()->getDoor_MustOpenMode();
    
    if (doorMode == "2" || doorMode == "1") {
        // DOOR MODE 1 & 2: Skip all validation, direct processing
        LogD("%s %s[%d] === DOOR MODE %s === Skipping mask/quality validation - direct processing\n", 
             __FILE__, __FUNCTION__, __LINE__, doorMode.toStdString().c_str());
             
        // Set basic face data and go straight to search
        if(d->mIdentifyFaceRecord.time_Start == 0) {
            d->mIdentifyFaceRecord.face = face;
            d->mIdentifyFaceRecord.time_End = d->mIdentifyFaceRecord.time_Start = (double)clock();
            d->mIdentifyFaceRecord.face_aids = d->mMustOpenMode;
            d->mIdentifyFaceRecord.face.enFaceType = CORE_FACE_RECT_TYPE_SEARCH;
            
            // Direct to recognition
            d->m_ThreadObjs.at(0)->SafeResolverData(d->mIdentifyFaceRecord.face);
        }
    } else {
        // OTHER DOOR MODES: Use existing validation logic
        switch(face.attr_info.face_mask) {
            case 0:   
                d->CheckNotMaskMode(face);
                break;
            case 1:
                d->CheckMaskMode(face);
                break;
        }
    }
    
    d->sync.unlock();
    d->pauseCond.wakeOne();
}

void IdentityManagement::slotDisClearMessage()
{
    Q_D(IdentityManagement);
    //重置刷卡标识
    //1.超时清空(从第1项开始计时), 2. 开门后清空         

    d->mIdentifyFaceRecord = {};//当前人员信息 
    d->mIdentityCard = {};//身份证信息
    d->mHealthCode = {};//健康码信息         
    d->micCard = {};//ic卡    
    d->mIdentifyFaceRecord.face_iccardnum="";
    d->mIdentifyFaceRecord.process_state=""; //清空    
    d->mIdentifyFaceRecord.process_state.clear();
    d->mIdentifyFaceRecord.face.attr_info.face_mask =0;
    d->mIdentifyFaceRecord.face.enFaceType=CORE_FACE_RECT_TYPE_UNKNOW;
    d->mIdentifyFaceRecord.time_Start=0; //在无人脸时,如IC,身份证,国康码,粤康码,测温度

    d->mCurTemperatureValue = 0;
    d->mHealthCode.timer = 0 ;  
    //如里有测温,则削除测试提示
    if(d->mMustOpenMode.contains(d->Door_OpenModeToSTR(_DOOR_OPEN_MODE::THERMOMETRY)) ||
       d->mOptionalOpenMode.contains(d->Door_OpenModeToSTR(_DOOR_OPEN_MODE::THERMOMETRY)) )
      emit sigTipsMessage(ALARM_MESSAGE, 2, QObject::tr(""));
    emit sigTipsMessage(BOTTOM_MESSAGE, 3, QObject::tr(""));
    d->mFirstTime = 1; //首次通行
        // ADD THIS LINE TO CLEAR PERSON IMAGE:
    emit sigClearPersonImage();

}
void IdentityManagement::slotDisMissMessage(const bool state)
{
    Q_D(IdentityManagement);

    // ADD THESE LINES AT THE BEGINNING:
    if (!state) {  // No face detected
        emit sigTipsMessage(BOTTOM_MESSAGE, 3, "");
    } else {
    }

    if(state)return;

   if (d->mMustOpenMode.contains(d->Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE)) 
     || d->mOptionalOpenMode.contains(d->Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE)))
    {
        slotDisClearMessage();
    }
}

void IdentityManagement::slotReadIccardNum(const QString iccard)
{
    printf("%s %s[%d] IdentityManagement %p \n",__FILE__,__FUNCTION__,__LINE__,this);
    LogD("%s %s[%d] szBuf=%s \n",__FILE__,__FUNCTION__,__LINE__,iccard.toStdString().c_str());
    Q_D(IdentityManagement);
	{
		int nDeskW = QApplication::desktop()->screenGeometry().width();
		int nDeskH = QApplication::desktop()->screenGeometry().height();
        if (ReadConfig::GetInstance()->getDebugMode_Value())
		  Toast::showTips(QRect(nDeskW - 300, nDeskH / 2 + 100, 320, 50), iccard, 8);
	}
     
    int passtimer = ((double)clock()- d->mIdentifyFaceRecord.time_Start)/1000/1000;   // 1000/1000;  CLOCK_REALTIME

    //可选的开门方式中有刷卡，则自行判断下刷卡的权限
    QString door_mode = d->mMustOpenMode;
    QString optional_mode = d->mOptionalOpenMode;    
    QString process = d->mIdentifyFaceRecord.process_state;    
    LogD("%s %s[%d]  \n",__FILE__,__FUNCTION__,__LINE__);
    //提示刷卡
    if (iccard.isEmpty())
    {    //卡号是空的， 提示刷卡
        emit sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("Plsswipe"));//请刷卡
    } else
    {   

        LogD("%s %s[%d]  \n",__FILE__,__FUNCTION__,__LINE__);
        d->micCard.number = iccard;    
        d->mIdentifyFaceRecord.face_iccardnum = iccard;       
         
        //查询卡是否存
        PERSONS_s info;
        if (RegisteredFacesDB::GetInstance()->selectICcardPerson(iccard, info))
        {

            LogD("%s %s[%d]  \n",__FILE__,__FUNCTION__,__LINE__);
            //if (info.iccard == iccard)
            if (iccard.endsWith(info.iccard) || info.iccard.endsWith(iccard))
            {
                //在数据库中有记录               
                LogD("%s %s[%d]  \n",__FILE__,__FUNCTION__,__LINE__);
                d->mIdentifyFaceRecord.process_state.append("&1");  
                d->mIdentifyFaceRecord.FaceType = NOT_STRANGER;
                d->mIdentifyFaceRecord.face_name = info.name;
                d->mIdentifyFaceRecord.face_personid = info.uuid.toInt();
                d->mIdentifyFaceRecord.face_uuid = info.uuid;         
                d->mIdentifyFaceRecord.FaceImgPath="/mnt/user/face_crop_image";         

                if(d->mIdentifyFaceRecord.time_Start == 0)
                {//默认状态
                    d->mIdentifyFaceRecord.time_End = d->mIdentifyFaceRecord.time_Start = (double)clock();//解发超时起始时间
                }
                LogD("%s %s[%d]  \n",__FILE__,__FUNCTION__,__LINE__);
                emit sigTipsMessage(TOP_MESSAGE, 1, "");

                //没有必选项,只有可选项,且可选项包含 "刷卡"                
                d->mIdentifyFaceRecord.Finish = false;           
                if (d->CanOpenDoor())
                {
                #if 0    
                    if (d->mFirstTime ==1 ) {}
                    else if  (d->mIdentifyInterval &&  ( passtimer <d->mIdentifyInterval  ) )
                    {
                        //要时间间隔 
                        return;
                    }    
                #endif        
                    
                    //开门
                                       
                   LogD("%s %s[%d]  \n",__FILE__,__FUNCTION__,__LINE__);
                    if (!d->mIdentifyFaceRecord.face_name.isEmpty() &&  d->mIdentifyFaceRecord.face_name!=QObject::tr("stranger") )
                    {                      
                QString displayText = OptimizedDisplayHelper::createVerifiedDisplay(
                d->mIdentifyFaceRecord.face_name,
                d->mIdentifyFaceRecord.face_idcardnum  // Use ID card, ignore IC card
            );
                       emit sigTipsMessage(BOTTOM_MESSAGE, 3, displayText);
        
                      
                      if (door_mode.contains(d->Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE)) || optional_mode.contains(d->Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE)))
	                    YNH_LJX::Audio::Audio_PlayRecognizedPcm("zh");
                    }

                    emit sigRecognizedPerson(
        d->mIdentifyFaceRecord.face_name,           // name from DB
        d->mIdentifyFaceRecord.face_personid,      // personId from DB  
        d->mIdentifyFaceRecord.face_uuid,          // uuid from DB
        d->mIdentifyFaceRecord.face_idcardnum      // employee ID from DB
    );
                    LogD("%s %s[%d]  \n",__FILE__,__FUNCTION__,__LINE__);
                   #if 1 //在此句
                    PersonRecordToDB::GetInstance()->appRecordData(d->mIdentifyFaceRecord);
                   #endif  

					int iRelay =  ReadConfig::GetInstance()->getDoor_Relay();
					LogD(">>>%s,%s,%d,iRelay=%d\n",__FILE__,__func__,__LINE__,iRelay);
					if (iRelay == 1) //1:常开
					{
						YNH_LJX::GPIO::Device_SetDeviceState(DEVICE_Relay, 1);
					}
					else if (iRelay == 2) //2:常闭
					{        
						YNH_LJX::GPIO::Device_SetDeviceState(DEVICE_Relay, 0);
					} 
					else 					
						YNH_LJX::Utils_Door::GetInstance()->OpenDoor(iccard);
                    LogD("%s %s[%d]  \n",__FILE__,__FUNCTION__,__LINE__);
                    
                    slotDisClearMessage();
                    d->mFirstTime+=1;
                    d->mIdentifyFaceRecord.Finish = true;                    
                }

            } else
            {
                LogD("%s %s[%d]  \n",__FILE__,__FUNCTION__,__LINE__);
                emit sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("CardNumberNotExist"));//卡号不存在
            }
        } else
        {
            LogD("%s %s[%d]  \n",__FILE__,__FUNCTION__,__LINE__);
            emit sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("ErrorQueryingData"));//查询数据出错
        }
    }
	LogD("%s %s[%d]  \n",__FILE__,__FUNCTION__,__LINE__);
    d->sync.lock();
    d->micCard = {iccard, QDateTime::currentDateTime().toTime_t()};
    d->sync.unlock();
    LogD("%s %s[%d]  \n",__FILE__,__FUNCTION__,__LINE__);
}

void IdentityManagement::slotHealthCodeInfo(const int type, const QString name, const QString idCard, const int qrCodeType, const double warningTemp, const QString msg)
{
    //读取到健康码 name： "李*"  idCard： "**************0333"  qrCodeType： 0  msg： "成功"  type  1
    //读取到健康码 name： ""  idCard： ""  qrCodeType： -1  msg： "网络异常"  type  1
    qDebug()<<"读取到健康码 name："<<name<<" idCard："<<idCard<<" qrCodeType："<<qrCodeType <<" msg："<<msg <<" type "<<type;
    Q_D(IdentityManagement);

    //可选的开门方式中有刷卡，则自行判断下刷卡的权限
    QString door_mode = d->mMustOpenMode;
    QString optional_mode = d->mOptionalOpenMode;

    //提示刷卡
    if (idCard.isEmpty())
    {    //卡号是空的， 提示刷卡
        emit sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("PlsswipeNHcode"));//请刷国康码
    } else
    {    // 成功 
        if (qrCodeType==0)
        {
            d->mIdentifyFaceRecord.process_state.append("&5");  
            if(d->mIdentifyFaceRecord.time_Start == 0)
            {//默认状态
                d->mIdentifyFaceRecord.time_End = d->mIdentifyFaceRecord.time_Start = (double)clock();//解发超时起始时间
            }            
            d->mIdentifyFaceRecord.Finish = false;              
            if (d->CanOpenDoor())
            {
                //开门
                    if (!d->mIdentifyFaceRecord.face_name.isEmpty() && d->mIdentifyFaceRecord.face_name!=QObject::tr("stranger") )
                    {                        
QString displayText = OptimizedDisplayHelper::createVerifiedDisplay(
                d->mIdentifyFaceRecord.face_name,
                d->mIdentifyFaceRecord.face_idcardnum
            );
emit sigTipsMessage(BOTTOM_MESSAGE, 3, displayText);
                    if (door_mode.contains(d->Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE)) || optional_mode.contains(d->Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE)))
	                    YNH_LJX::Audio::Audio_PlayRecognizedPcm("zh");
                    }

				int iRelay =  ReadConfig::GetInstance()->getDoor_Relay();
				LogD(">>>%s,%s,%d,OpenDoor iRelay=%d\n",__FILE__,__func__,__LINE__,iRelay);
				if (iRelay == 1) //1:常开
				{
					YNH_LJX::GPIO::Device_SetDeviceState(DEVICE_Relay, 1);
				}
				else if (iRelay == 2) //2:常闭
				{        
					YNH_LJX::GPIO::Device_SetDeviceState(DEVICE_Relay, 0);
				} 
				else 				
					YNH_LJX::Utils_Door::GetInstance()->OpenDoor("");
                PersonRecordToDB::GetInstance()->appRecordData(d->mIdentifyFaceRecord);
                slotDisClearMessage();
                d->mIdentifyFaceRecord.Finish = true; 
            }	
        }

    }
   
    d->sync.lock();
    d->mHealthCode = {name, "", idCard, msg, qrCodeType, type, warningTemp, QDateTime::currentDateTime().toTime_t()};
    d->sync.unlock();

    emit sigShowHealthCode(d->mHealthCode.type, d->mHealthCode.name, d->mHealthCode.idCard, d->mHealthCode.qrcode, d->mHealthCode.warningTemp, d->mHealthCode.msg);

}
void IdentityManagement::slotLRHealthCodeInfo2(HEALTINFO_t info)
{
    Q_D(IdentityManagement);   
    		 printf("%s %s[%d] info.name : %s \n", __FILE__, __FUNCTION__, __LINE__, info.name.toStdString().c_str());
    {
		int nDeskW = QApplication::desktop()->screenGeometry().width();
		int nDeskH = QApplication::desktop()->screenGeometry().height(); 
        if (ReadConfig::GetInstance()->getDebugMode_Value())        
            Toast::showTips(QRect(nDeskW - 350, nDeskH / 2 + 100, 320, 50), info.name+"---"+info.idnumber, 20);
    }
    //如无人脸信息,则提示:请正视屏幕

    if (d->mIdentifyFaceRecord.FaceImgPath.isEmpty())
    {
        YNH_LJX::Audio::Audio_PlayCustomerPcm("zh","PlsLookScreen.wav",false);
        return ;
    }  
  
    d->mIdentifyFaceRecord.time_End = d->mIdentifyFaceRecord.time_Start = (double)clock();//解发超时起始时间    

    ShowLRHealthCodeAndOpenDoor(info,1);//nKind:1://粤2.身份证
}   
void IdentityManagement::slotIdentityCardInfo(const QString name, const QString idCard, const QString sex, const QString path)
{
    Q_UNUSED(path);

    Q_D(IdentityManagement);
	{
		int nDeskW = QApplication::desktop()->screenGeometry().width();
		int nDeskH = QApplication::desktop()->screenGeometry().height();
        if (ReadConfig::GetInstance()->getDebugMode_Value())
		 Toast::showTips(QRect(nDeskW - 350, nDeskH / 2 + 100, 320, 50), name+"---"+idCard, 20);
	}
        
    if (d->mIdentifyFaceRecord.FaceImgPath.isEmpty())
    {
        YNH_LJX::Audio::Audio_PlayCustomerPcm("zh","PlsLookScreen.wav",false);
        return ;
    }      

    QString door_mode = d->mMustOpenMode;
    QString optional_mode = d->mOptionalOpenMode;
    QString process = d->mIdentifyFaceRecord.process_state;
    //提示刷卡
    if (idCard.isEmpty())
    {    //卡号是空的， 提示刷卡
        
        emit sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("PlsswipeIdCard"));//请刷
    } else
    {    //查询卡是否存        
       printf(">>>>%s,%s,%d,door_mode=%s,optional_mode=%s\n", \
       __FILE__,__func__,__LINE__,d->mMustOpenMode.toStdString().c_str(),d->mOptionalOpenMode.toStdString().c_str() );

        if (d->mIdentifyFaceRecord.FaceImgPath.isEmpty())
        {
            YNH_LJX::Audio::Audio_PlayCustomerPcm("zh","PlsLookScreen.wav",false);
            return;
        }  

        //过滤部分,

        YNH_LJX::Audio::Audio_PlayCustomerPcm("zh", "Buzzer.wav", true);//放在这里嘀

        d->mIdentityCard.idCard = idCard;
        //推送数据到 后台时用到
        d->mIdentifyFaceRecord.face_idcardnum = idCard;
        d->mIdentifyFaceRecord.face_name = name;
        d->mIdentifyFaceRecord.face_sex = sex;
        d->mIdentifyFaceRecord.temp_value = d->mCurTemperatureValue;




        PERSONS_s info;
        if (RegisteredFacesDB::GetInstance()->selectIDcardPerson(idCard, info))
        {
            
            //printf(">>>>%s,%s,%d, FaceImgPath=%s\n",__FILE__,__func__,__LINE__,d->mIdentifyFaceRecord.FaceImgPath.toStdString().c_str());
            if (info.idcard == idCard)
            {                
                d->mIdentifyFaceRecord.process_state.append("&6");  
                d->mIdentifyFaceRecord.face_personid = info.uuid.toInt();
                d->mIdentifyFaceRecord.face_uuid = info.uuid;
                if(d->mIdentifyFaceRecord.time_Start == 0)
                {//默认状态
                    d->mIdentifyFaceRecord.time_End = d->mIdentifyFaceRecord.time_Start = (double)clock();//解发超时起始时间
                }
                //可选的开门方式中有刷卡，则自行判断下刷卡的权限
                if (d->CanOpenDoor())
                {
                    //开门
                    if (!d->mIdentifyFaceRecord.face_name.isEmpty() && d->mIdentifyFaceRecord.face_name!=QObject::tr("stranger") )
                    {                        
            QString displayText = OptimizedDisplayHelper::createVerifiedDisplay(
                d->mIdentifyFaceRecord.face_name,
                d->mIdentifyFaceRecord.face_idcardnum
            );
emit sigTipsMessage(BOTTOM_MESSAGE, 3, displayText);
                    if (door_mode.contains(d->Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE)) || optional_mode.contains(d->Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE)))
	                    YNH_LJX::Audio::Audio_PlayRecognizedPcm("zh");
                    }
                    //printf(">>>>%s,%s,%d,OpenDoor FaceImgPath=%s\n",__FILE__,__func__,__LINE__,d->mIdentifyFaceRecord.FaceImgPath.toStdString().c_str());
					int iRelay =  ReadConfig::GetInstance()->getDoor_Relay();
					LogD(">>>%s,%s,%d, iRelay=%d\n",__FILE__,__func__,__LINE__,iRelay);
					if (iRelay == 1) //1:常开
					{
						YNH_LJX::GPIO::Device_SetDeviceState(DEVICE_Relay, 1);
					}
					else if (iRelay == 2) //2:常闭
					{        
						YNH_LJX::GPIO::Device_SetDeviceState(DEVICE_Relay, 0);
					} 
					else 					
						YNH_LJX::Utils_Door::GetInstance()->OpenDoor("");      
                    PersonRecordToDB::GetInstance()->appRecordData(d->mIdentifyFaceRecord); 
                    slotDisClearMessage();             
                } else 
                {
                    
                    if (   (door_mode.contains(d->Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE)) || optional_mode.contains(d->Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE)))
                           && !d->mIdentifyFaceRecord.process_state.contains("2")   ) 
                    {
                        YNH_LJX::Audio::Audio_PlayCustomerPcm("zh","PlsLookScreen.wav",false); 
                    }
                }

            } else
            {
          
                d->mIdentifyFaceRecord.process_state.append("&6");  
                if(d->mIdentifyFaceRecord.time_Start == 0)
                {//默认状态
                    d->mIdentifyFaceRecord.time_End = d->mIdentifyFaceRecord.time_Start = (double)clock();//解发超时起始时间
                }
                
                if (d->CanOpenDoor())
                {
                    //开门
                    if (!d->mIdentifyFaceRecord.face_name.isEmpty() && d->mIdentifyFaceRecord.face_name!=QObject::tr("stranger") )
                    {                        
                   QString displayText = OptimizedDisplayHelper::createVerifiedDisplay(
    d->mIdentifyFaceRecord.face_name,
    d->mIdentifyFaceRecord.face_idcardnum  // Only ID card, no IC card
);
                      emit sigTipsMessage(BOTTOM_MESSAGE, 3, displayText);
                        if (door_mode.contains(d->Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE)) || optional_mode.contains(d->Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE)))
                        {}
                        else 
                        {
                            YNH_LJX::Audio::Audio_PlayRecognizedPcm("zh");
							int iRelay =  ReadConfig::GetInstance()->getDoor_Relay();
							LogD(">>>%s,%s,%d, OpenDoor iRelay=%d\n",__FILE__,__func__,__LINE__,iRelay);
							if (iRelay == 1) //1:常开
							{
								YNH_LJX::GPIO::Device_SetDeviceState(DEVICE_Relay, 1);
							}
							else if (iRelay == 2) //2:常闭
							{        
								YNH_LJX::GPIO::Device_SetDeviceState(DEVICE_Relay, 0);
							} 
							else 							
								YNH_LJX::Utils_Door::GetInstance()->OpenDoor("");   
                            PersonRecordToDB::GetInstance()->appRecordData(d->mIdentifyFaceRecord);                                
                            slotDisClearMessage();                               
                        }
                        
                    }
          
                }                
            }
        } else
        {
            printf(">>>>%s,%s,%d\n",__FILE__,__func__,__LINE__); 
            emit sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("ErrorQueryingData"));//查询数据出错
        }
    }
	   
    d->sync.lock();
    d->mIdentityCard = {name, sex, idCard, path, QDateTime::currentDateTime().toTime_t()};
    d->sync.unlock();

}

void IdentityManagement::slotTemperatureValue(const float value)
{
    Q_D(IdentityManagement);
    //LogD(">>>%s,%s,%d,value=%2f\n",__FILE__,__func__,__LINE__,value);
    if (d->mCurTemperatureValue == 0)
      d->mCurTemperatureValue = value;
    QString door_mode = d->mMustOpenMode;
    QString optional_mode = d->mOptionalOpenMode;

    if (d->CheckLiveness(d->mIdentifyFaceRecord.face) || d->mIdentifyFaceRecord.FaceImgPath>"") 
    {} else  //没人就退出了
      return;
    if(door_mode.contains(d->Door_OpenModeToSTR(_DOOR_OPEN_MODE::THERMOMETRY)) == false && optional_mode.contains(d->Door_OpenModeToSTR(_DOOR_OPEN_MODE::THERMOMETRY)) == false)
    {
    	return;
    }


		//提示刷卡
    if (value >= d->mAlarmTemp  ) //t.temp_value>=this->mAlarmTemp,37.3
    {    //卡号是空的， 提示刷卡
        emit sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("TemperatureTooHigh"));//体温过高
    } else
    {   
        if (!d->mIdentifyFaceRecord.process_state.contains("&3"))
            d->mIdentifyFaceRecord.process_state.append("&3");
        LogD(">>>%s,%s,%d,value=%2f\n",__FILE__,__func__,__LINE__,value);
        if(door_mode.contains(d->Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE))  || optional_mode.contains(d->Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE)) )
        {
            return; //如果有刷脸,则不走此流程
        }
        //LogD(">>>%s,%s,%d,\n",__FILE__,__func__,__LINE__);
#if 1    
        if(d->mIdentifyFaceRecord.time_Start == 0)
        {//默认状态
            d->mIdentifyFaceRecord.time_End = d->mIdentifyFaceRecord.time_Start = (double)clock();//解发超时起始时间
		}

        //LogD(">>>%s,%s,%d\n",__FILE__,__func__,__LINE__);
        if (d->CanOpenDoor())
        {

            //开门
            if (!d->mIdentifyFaceRecord.face_name.isEmpty() && d->mIdentifyFaceRecord.face_name!=QObject::tr("stranger") )
            {                
QString displayText = OptimizedDisplayHelper::createVerifiedDisplay(
                d->mIdentifyFaceRecord.face_name,
                d->mIdentifyFaceRecord.face_idcardnum
            );
                emit sigTipsMessage(BOTTOM_MESSAGE, 3, displayText);
            if (door_mode.contains(d->Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE)) || optional_mode.contains(d->Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE)))
	                    YNH_LJX::Audio::Audio_PlayRecognizedPcm("zh");
            }
            
			int iRelay =  ReadConfig::GetInstance()->getDoor_Relay();
			LogD(">>>%s,%s,%d, OpenDoor iRelay=%d\n",__FILE__,__func__,__LINE__,iRelay);			
			if (iRelay == 1) //1:常开
			{
				YNH_LJX::GPIO::Device_SetDeviceState(DEVICE_Relay, 1);
			}
			else if (iRelay == 2) //2:常闭
			{        
				YNH_LJX::GPIO::Device_SetDeviceState(DEVICE_Relay, 0);
			} 
			else 
				YNH_LJX::Utils_Door::GetInstance()->OpenDoor(""); 
            PersonRecordToDB::GetInstance()->appRecordData(d->mIdentifyFaceRecord);            
            slotDisClearMessage();       

        }
  
       if ( d->mMustOpenMode == d->Door_OpenModeToSTR(_DOOR_OPEN_MODE::THERMOMETRY) 
          || d->mOptionalOpenMode == d->Door_OpenModeToSTR(_DOOR_OPEN_MODE::THERMOMETRY))     
        {                
        
            emit sigTipsMessage(TOP_MESSAGE, 1, QObject::tr(""));
            YNH_LJX::Audio::Audio_PlayCustomerPcm("zh","temp_normal.wav",true); 
            emit sigTipsMessage(BOTTOM_MESSAGE, 3, QObject::tr("TemperaturenSucceeded"));//Face recognition succeeded 识别成功 
        }
#endif         
    }
	
}
void IdentityManagement::EchoFaceRecognition(const int &id, const int &FaceType, 
                                           const int &face_personid, const int &face_persontype, 
                                           const QString &face_name, const QString &face_sex, 
                                           const QString &face_uuid, const QString &face_idcardnum, 
                                           const QString &face_iccardnum, const QString &face_gids, 
                                           const QString &face_aids, const QByteArray &face_feature) {
    Q_D(IdentityManagement);
    
    LogD("%s %s[%d] === ECHO RECOGNITION === Name: %s, PersonID: %d, Type: %s\n",
         __FILE__, __FUNCTION__, __LINE__, face_name.toStdString().c_str(), face_personid,
         (FaceType == NOT_STRANGER) ? "REGISTERED_USER" : "STRANGER");
    
    // ✅ MINIMAL CRITICAL SECTION: Only essential data updates
    {
        QMutexLocker locker(&d->sync);
        
        if (d->mIdentifyFaceRecord.face.enFaceType != CORE_FACE_RECT_TYPE_UNKNOW) {
            d->mIdentifyFaceRecord.Tick = 0;
            
            if (d->mIdentifyFaceRecord.time_Start == 0) {
                d->mIdentifyFaceRecord.time_End = d->mIdentifyFaceRecord.time_Start = (double)clock();
            }
            
            // ✅ ESSENTIAL UPDATES
            d->mIdentifyFaceRecord.face.enFaceType = CORE_FACE_RECT_TYPE_MATCH;
            d->mIdentifyFaceRecord.FaceType = FaceType;
            d->mIdentifyFaceRecord.face_personid = face_personid;
            d->mIdentifyFaceRecord.face_name = (FaceType == NOT_STRANGER) ? face_name : QObject::tr("stranger");
            d->mIdentifyFaceRecord.face_uuid = face_uuid;
            d->mIdentifyFaceRecord.face_idcardnum = face_idcardnum;
            
            if (FaceType == NOT_STRANGER) {
                d->mIdentifyFaceRecord.process_state.append("&2");
            }
        }
    } // Lock released quickly
    
    // ✅ HEAVY OPERATIONS OUTSIDE LOCK
    QString doorMode = ReadConfig::GetInstance()->getDoor_MustOpenMode();
        // ✅ ADDITIONAL: Show stranger message
        if (FaceType == NOT_STRANGER) {
        // Recognized user - GREEN rectangle
        ((BaiduFaceManager *)qXLApp->GetAlgoFaceManager())->setIdentifyState(
            true, face_name, face_personid, face_uuid, face_idcardnum);
    } else {
        // Stranger - RED rectangle
        ((BaiduFaceManager *)qXLApp->GetAlgoFaceManager())->setIdentifyState(
            false, "stranger", 0, "", "");
            
        // ✅ USE EXISTING SIGNAL instead of direct call
        if (doorMode == "2") {
            static QTime lastStrangerTime;
            if (lastStrangerTime.msecsTo(QTime::currentTime()) > 2000) {
                lastStrangerTime = QTime::currentTime();
                
                // Use existing signal system
                emit sigTipsMessage(BOTTOM_MESSAGE, 3, 
                    QObject::tr("<font color=\"#FFFF33\">stranger</font>"));
                
                // Play stranger audio
                YNH_LJX::Audio::Audio_PlayPeopleStrangerPcm("zh");
            }
        }
    }
}

 bool IdentityManagementPrivate::DistributeTheTasks()
{
	static int nAlgoStateAboutFaceCount = 0;
	if(nAlgoStateAboutFaceCount ++ == 10)
	{
		nAlgoStateAboutFaceCount = 0;
		QString state = "";
		state += QString::number(this->mIdentifyFaceRecord.face.attr_info.face_mask)+",";
		state += QString::number(this->mIdentifyFaceRecord.face.attr_info.liveness_ir)+",";
		state += QString::number(this->mIdentifyFaceRecord.face.attr_info.quality,'f',2)+",";
		state += QString::number(gAlogStateFaceSimilar,'g',2);

		emit q_func()->sigShowAlgoStateAboutFace(state);
	}

    switch((int)this->mIdentifyFaceRecord.face.enFaceType)
    {
        case CORE_FACE_RECT_TYPE_UNKNOW://没有人脸图
            return false;
        case CORE_FACE_RECT_TYPE_MOVING:
        {
            if(this->mMustOpenMode.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE)) || this->mOptionalOpenMode.contains(Door_OpenModeToSTR(_DOOR_OPEN_MODE::SWIPING_FACE)))//刷脸检测人员库
            {//查找人脸
                this->mIdentifyFaceRecord.face.enFaceType = CORE_FACE_RECT_TYPE_SEARCH;
                m_ThreadObjs.at(0)->SafeResolverData(this->mIdentifyFaceRecord.face);
            }
            else this->mIdentifyFaceRecord.face.enFaceType = CORE_FACE_RECT_TYPE_MATCH;
        }break;
        case CORE_FACE_RECT_TYPE_SEARCH://正查找人脸
            break;
case CORE_FACE_RECT_TYPE_MATCH:
{
    QString door_mode = ReadConfig::GetInstance()->getDoor_MustOpenMode();
    
    if (door_mode == "2" || door_mode == "1") {
        if (this->mIdentifyFaceRecord.FaceType == NOT_STRANGER) {
            LogD("%s %s[%d] === DOOR MODE %s === Opening door for recognized user: %s\n", 
                 __FILE__, __FUNCTION__, __LINE__, door_mode.toStdString().c_str(),
                 this->mIdentifyFaceRecord.face_name.toStdString().c_str());

            // DISABLE power manager LED control
            qXLApp->GetPowerManagerThread()->setRecognitionInProgress(true);
            
            // NOW control LEDs manually
            YNH_LJX::GPIO::Device_SetDeviceState(DEVICE_Light_Red, 0);
            YNH_LJX::GPIO::Device_SetDeviceState(DEVICE_Light_White, 0);
            YNH_LJX::GPIO::Device_SetDeviceState(DEVICE_Light_Green, 1);
            
            YNH_LJX::Audio::Audio_PlayRecognizedPcm("zh");
            
            int iRelay = ReadConfig::GetInstance()->getDoor_Relay();
            if (iRelay == 1) {
                YNH_LJX::GPIO::Device_SetDeviceState(DEVICE_Relay, 1);
            } else if (iRelay == 2) {
                YNH_LJX::GPIO::Device_SetDeviceState(DEVICE_Relay, 0);
            } else {
                YNH_LJX::Utils_Door::GetInstance()->OpenDoor("");
            }
            
            PersonRecordToDB::GetInstance()->appRecordData(this->mIdentifyFaceRecord);
            
            // Keep green light for 3 seconds, then restore power manager control
            QTimer::singleShot(1000, [this]() {
                YNH_LJX::GPIO::Device_SetDeviceState(DEVICE_Light_Green, 0);
                // RE-ENABLE power manager LED control
                qXLApp->GetPowerManagerThread()->setRecognitionInProgress(false);
            });
            this->mIdentifyFaceRecord.face.enFaceType = CORE_FACE_RECT_TYPE_DELETE;
            return true;
        } else {
            // STRANGER HANDLING WITH IMMEDIATE AUDIO + INTERVAL RESPECT
            
            // Check if we should play stranger audio (respect interval)
            bool shouldPlayAudio = true;
            
            // Check if this is the same person within interval
            static int lastStrangerTrackId = -1;
            static double lastStrangerAudioTime = 0;
            
            double currentTime = (double)clock();
            double timeSinceLastAudio = (currentTime - lastStrangerAudioTime) / 1000 / 1000; // Convert to seconds
            
            if (lastStrangerTrackId == this->mIdentifyFaceRecord.face.track_id && 
                this->mIdentifyInterval > 0 && 
                timeSinceLastAudio < this->mIdentifyInterval) {
                shouldPlayAudio = false; // Same person within interval, don't repeat audio
            }
            
            // Display stranger message immediately
            emit q_func()->sigTipsMessage(BOTTOM_MESSAGE, 3, QObject::tr("<font color=\"#FFFF33\">stranger</font>"));
            
            // Play stranger audio immediately if interval allows
            if (shouldPlayAudio) {
                YNH_LJX::Audio::Audio_PlayPeopleStrangerPcm("zh");
                lastStrangerTrackId = this->mIdentifyFaceRecord.face.track_id;
                lastStrangerAudioTime = currentTime;
                
                LogD("%s %s[%d] === DOOR MODE %s === Playing stranger audio for track_id: %d\n", 
                     __FILE__, __FUNCTION__, __LINE__, door_mode.toStdString().c_str(), this->mIdentifyFaceRecord.face.track_id);
            } else {
                LogD("%s %s[%d] === DOOR MODE %s === Skipping stranger audio (within interval) for track_id: %d\n", 
                     __FILE__, __FUNCTION__, __LINE__, door_mode.toStdString().c_str(), this->mIdentifyFaceRecord.face.track_id);
            }
            
            this->mIdentifyFaceRecord.face.enFaceType = CORE_FACE_RECT_TYPE_DELETE;
            return true;
        }
    } else {
        // Existing logic for other door modes...
        if(!RegisteredFacesDB::GetInstance()->CheckPassageOfTime(this->mIdentifyFaceRecord.face_uuid)) {
            emit q_func()->sigTipsMessage(TOP_MESSAGE, 1, QObject::tr("CheckPassageOfTimeHint"));
            return false;
        } 
        
        if(this->CheckDoorOpenMode(this->mIdentifyFaceRecord)) {
        }
    }
    
    break;
}
        case CORE_FACE_RECT_TYPE_DELETE:
        {
            int passtimer = ((double)clock()- this->mIdentifyFaceRecord.time_End)/1000/1000;
            if(this->mIdentifyInterval &&  (passtimer >= this->mIdentifyInterval))
            {//重置刷卡标识
               // if(this->mIdentifyFaceRecord.FaceType != NOT_STRANGER)
                    //YNH_LJX::Audio::Audio_PlayPeopleStrangerPcm("zh");
                this->mIdentifyFaceRecord = {};
                this->mIdentityCard = {};//身份证信息
                this->mHealthCode = {};//健康码信息
                this->micCard = {};//ic卡
            }
        }break;//到了这里人员已实别所有通过了
    }

    return true;
}

void IdentityManagement::run()
{
    Q_D(IdentityManagement);
    while (!isInterruptionRequested())
    {
        d->sync.lock();
        if (d->DistributeTheTasks())d->pauseCond.wait(&d->sync, 30);
        else d->pauseCond.wait(&d->sync);
        d->sync.unlock();
    }
}
