#include "SysSetupFrm.h"

#include "../SetupItemDelegate/CItemWidget.h"
#include "LanguageFrm.h"
#include "FillLightFrm.h"
#include "LuminanceFrm.h"
#include "VolumeFrm.h"
#include "LogPasswordChangeFrm.h"
#include "QRCodeFrm.h"
#include "SyncFunctionality.h"
#include "CompareFingerFrm.h"
#include "Helper/myhelper.h"

#include "Config/ReadConfig.h"
#include "OperationTipsFrm.h"
#include "DeviceInfo.h"
#include "DeleteAllFingerprintsFrm.h"


#include <QListWidget>
#include <QHBoxLayout>
#include <QApplication>
#include <QScreen>
#include <QDesktopWidget>
#include <QMouseEvent>
#include <QDebug>
#include <QListWidgetItem>
#include <QtConcurrent/QtConcurrent>
#include <QNetworkInterface>

class SysSetupFrmPrivate
{
    Q_DECLARE_PUBLIC(SysSetupFrm)
public:
    SysSetupFrmPrivate(SysSetupFrm *dd);
private:
    void InitUI();
    void InitData();
    void InitConnect();
private:
    //检测顶层窗口是否显示
    bool CheckTopState();
private:
    QListWidget *m_pListWidget;
private:
    LanguageFrm *m_pLanguageFrm;//语音设置
    LuminanceFrm *m_pLuminanceFrm;//亮度设置
    FillLightFrm *m_pFillLightFrm;//补光设置
    VolumeFrm *m_pVolumeFrm;//音量设置
    LogPasswordChangeFrm *m_pLogPasswordChangeFrm;//登陆密码修改
    QRCodeFrm *m_pQRCodeFrm;
    DeleteAllFingerprintsFrm *m_pDeleteAllFingerprintsFrm;
    CompareFingerFrm *m_pCompareFingerFrm;

private:
    SysSetupFrm *const q_ptr;
};

SysSetupFrmPrivate::SysSetupFrmPrivate(SysSetupFrm *dd)
    : q_ptr(dd)
    , m_pListWidget(nullptr)
    , m_pLanguageFrm(nullptr)
    , m_pLuminanceFrm(nullptr)
    , m_pFillLightFrm(nullptr)
    , m_pVolumeFrm(nullptr)
    , m_pLogPasswordChangeFrm(nullptr)
    , m_pQRCodeFrm(nullptr)
    , m_pDeleteAllFingerprintsFrm(nullptr)
    , m_pCompareFingerFrm(nullptr)  // Add this line

{
    qDebug() << "SysSetupFrmPrivate constructor START";
    
    try {
        qDebug() << "Creating LanguageFrm...";
        m_pLanguageFrm = new LanguageFrm(q_ptr);
        qDebug() << "LanguageFrm created successfully";
        
        qDebug() << "Creating LuminanceFrm...";
        m_pLuminanceFrm = new LuminanceFrm(q_ptr);
        qDebug() << "LuminanceFrm created successfully";
        
        qDebug() << "Creating FillLightFrm...";
        m_pFillLightFrm = new FillLightFrm(q_ptr);
        qDebug() << "FillLightFrm created successfully";
        
        qDebug() << "Creating VolumeFrm...";
        m_pVolumeFrm = new VolumeFrm(q_ptr);
        qDebug() << "VolumeFrm created successfully";
        
        qDebug() << "Creating LogPasswordChangeFrm...";
        m_pLogPasswordChangeFrm = new LogPasswordChangeFrm(q_ptr);
        qDebug() << "LogPasswordChangeFrm created successfully";
        
        qDebug() << "Creating QRCodeFrm...";
        m_pQRCodeFrm = new QRCodeFrm(q_ptr);
        qDebug() << "QRCodeFrm created successfully";

        m_pDeleteAllFingerprintsFrm = new DeleteAllFingerprintsFrm(q_ptr);
        qDebug() << "DeleteAllFingerprintsFrm created successfully";

        qDebug() << "Creating CompareFingerFrm...";
        m_pCompareFingerFrm = new CompareFingerFrm(q_ptr);
        qDebug() << "CompareFingerFrm created successfully";

        qDebug() << "Calling InitUI...";
        this->InitUI();
        qDebug() << "InitUI completed";
        
        qDebug() << "Calling InitData...";
        this->InitData();
        qDebug() << "InitData completed";
        
        qDebug() << "Calling InitConnect...";
        this->InitConnect();
        qDebug() << "InitConnect completed";
        
    } catch (...) {
        qDebug() << "EXCEPTION in SysSetupFrmPrivate constructor!";
        throw;
    }
    
    qDebug() << "SysSetupFrmPrivate constructor END";
}

SysSetupFrm::SysSetupFrm(QWidget *parent)
    : SettingBaseFrm(parent)
    , d_ptr(new SysSetupFrmPrivate(this))
{
    // Constructor is now clean - all initialization happens in SysSetupFrmPrivate
}

SysSetupFrm::~SysSetupFrm()
{

}

void SysSetupFrmPrivate::InitUI()
{
    m_pListWidget = new QListWidget;
    QHBoxLayout *layout = new QHBoxLayout(q_func());
    layout->setSpacing(0);
    layout->setMargin(0);
    layout->addWidget(m_pListWidget);
}

void SysSetupFrmPrivate::InitData()
{
    int width = QApplication::desktop()->screenGeometry().width();
    int spacing = 0;
    switch(width)
    {
    case 480:spacing = 22; break;
    case 600:spacing = 22;break;
    case 720:spacing = 36;break;
    case 800:spacing = 0;break;
    }

    m_pListWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QStringList listName;

    // Removed AccessControlSettings, MainUISettings, and TimeSetting
    listName<<QObject::tr("StorageCapacity")<<QObject::tr("LoginPassword")<<QObject::tr("About");

    {   // Language Settings
        QListWidgetItem *pItem = new QListWidgetItem;
        CItemWidget *pItemWidget = new CItemWidget;
        pItemWidget->setAddSpacing(spacing);
        pItemWidget->setData(QObject::tr("LanguageSettings"), ":/Images/SmallRound.png", QObject::tr("English"));
        m_pListWidget->addItem(pItem);
        if (width==480)
            pItemWidget->setAddSpacing(120);
        m_pListWidget->setItemWidget(pItem, pItemWidget);
    }

    {   // Brightness Settings
        QListWidgetItem *pItem = new QListWidgetItem;
        CItemWidget *pItemWidget = new CItemWidget;
        pItemWidget->setAddSpacing(spacing);
        pItemWidget->setData(QObject::tr("BrightnessSetting"), ":/Images/SmallRound.png", "10");
        m_pListWidget->addItem(pItem);
        if (width==480)
            pItemWidget->setAddSpacing(120);
        m_pListWidget->setItemWidget(pItem, pItemWidget);
    }

    {   // Fill Light Settings
        QListWidgetItem *pItem = new QListWidgetItem;
        CItemWidget *pItemWidget = new CItemWidget;
        pItemWidget->setAddSpacing(spacing);
        pItemWidget->setData(QObject::tr("FillLightSetting"), ":/Images/SmallRound.png", QObject::tr("Auto"));
        m_pListWidget->addItem(pItem);
        if (width==480)
            pItemWidget->setAddSpacing(120);
        m_pListWidget->setItemWidget(pItem, pItemWidget);
    }

    {   // Volume Settings
        QListWidgetItem *pItem = new QListWidgetItem;
        CItemWidget *pItemWidget = new CItemWidget;
        pItemWidget->setAddSpacing(spacing);
        pItemWidget->setData(QObject::tr("VolumeSetting"), ":/Images/SmallRound.png", "8");
        m_pListWidget->addItem(pItem);
        if (width==480)
            pItemWidget->setAddSpacing(120);
        m_pListWidget->setItemWidget(pItem, pItemWidget);
    }

    // QR Code item before about
    {
        QListWidgetItem *pItem = new QListWidgetItem;
        CItemWidget *pItemWidget = new CItemWidget;
        pItemWidget->setAddSpacing(spacing);
        pItemWidget->setData(QObject::tr("QR Enrollment"), ":/Images/SmallRound.png");
        m_pListWidget->addItem(pItem);
        if (width==480)
            pItemWidget->setAddSpacing(120);
        m_pListWidget->setItemWidget(pItem, pItemWidget);
    }
     {   // Delete All Fingerprints
        QListWidgetItem *pItem = new QListWidgetItem;
        CItemWidget *pItemWidget = new CItemWidget;
        pItemWidget->setAddSpacing(spacing);
        pItemWidget->setData(QObject::tr("Delete All Fingerprints"), ":/Images/SmallRound.png");
        m_pListWidget->addItem(pItem);
        if (width==480)
            pItemWidget->setAddSpacing(120);
        m_pListWidget->setItemWidget(pItem, pItemWidget);
    }

     {
        QListWidgetItem *pItem = new QListWidgetItem;
        CItemWidget *pItemWidget = new CItemWidget;
        pItemWidget->setAddSpacing(spacing);
        pItemWidget->setData(QObject::tr("Compare Finger"), ":/Images/SmallRound.png");
        m_pListWidget->addItem(pItem);
        if (width==480)
            pItemWidget->setAddSpacing(120);
        m_pListWidget->setItemWidget(pItem, pItemWidget);
    }

    // Add the rest of the items from listName (StorageCapacity, LoginPassword, About)
    for(int i = 0; i < listName.count(); i++)
    {
        QListWidgetItem *pItem = new QListWidgetItem;
        CItemWidget *pItemWidget = new CItemWidget;
        pItemWidget->setAddSpacing(spacing);
        pItemWidget->setData(listName.at(i), ":/Images/SmallRound.png");
        m_pListWidget->addItem(pItem);
        if (width==480)
            pItemWidget->setAddSpacing(120);
        m_pListWidget->setItemWidget(pItem, pItemWidget);
    }

    {   // Sync Devices
        QListWidgetItem *pItem = new QListWidgetItem;
        CItemWidget *pItemWidget = new CItemWidget;
        pItemWidget->setAddSpacing(spacing);
        pItemWidget->setData(QObject::tr("Sync Devices"), ":/Images/SmallRound.png");
        m_pListWidget->addItem(pItem);
        if (width == 480)
            pItemWidget->setAddSpacing(120);
        m_pListWidget->setItemWidget(pItem, pItemWidget);
    }
    
}

void SysSetupFrmPrivate::InitConnect()
{
    QObject::connect(m_pListWidget, &QListWidget::itemClicked, q_func(), &SysSetupFrm::slotIemClicked);
}

bool SysSetupFrmPrivate::CheckTopState()
{
    if(!m_pLuminanceFrm->isHidden())
    {
        ((CItemWidget *)this->m_pListWidget->itemWidget(this->m_pListWidget->item(1)))->setRNameText(QString::number(m_pLuminanceFrm->getAdjustValue()));
        ReadConfig::GetInstance()->setLuminance_Value(m_pLuminanceFrm->getAdjustValue());
        m_pLuminanceFrm->hide();

        return true;
    }else if(!m_pVolumeFrm->isHidden())
    {
        ((CItemWidget *)this->m_pListWidget->itemWidget(this->m_pListWidget->item(3)))->setRNameText(QString::number(m_pVolumeFrm->getAdjustValue()));
        m_pVolumeFrm->hide();
        return true;
    }
    return false;
}

static QString LanguageIdexToSTR(const int &index)
{
    switch(index)
    {
    case 0:return QObject::tr("English");
    case 1:return QObject::tr("English");
    //case 2:return QObject::tr("日本語");
    //case 3:return QObject::tr("한글");
    }
    return QObject::tr("English");
}

static QString FillLightIndexToSTR(const int &index)
{
    switch(index)
    {
        case 0:return QObject::tr("NormallyClosed");//常闭
        case 1:return QObject::tr("NormallyOpen");//常开
        case 2:return QObject::tr("Auto");//自动
    }
    return QObject::tr("Auto");//自动
}

void SysSetupFrm::setEnter()
{
    Q_D(SysSetupFrm);
    
    qDebug() << "=== setEnter() DEBUG START ===";
    
    if (!d) {
        qDebug() << "FATAL: d_ptr is NULL!";
        return;
    }
    qDebug() << "d_ptr is valid:" << d;
    
    qDebug() << "About to call ReadConfig::GetInstance()...";
    ReadConfig* config = ReadConfig::GetInstance();
    qDebug() << "ReadConfig::GetInstance() returned:" << config;
    
    if (!config) {
        qDebug() << "FATAL: ReadConfig instance is NULL!";
        return;
    }
    qDebug() << "ReadConfig is valid, continuing...";

    // Check all form pointers
    qDebug() << "Checking form pointers...";
    qDebug() << "m_pLanguageFrm:" << d->m_pLanguageFrm;
    qDebug() << "m_pLuminanceFrm:" << d->m_pLuminanceFrm;
    qDebug() << "m_pFillLightFrm:" << d->m_pFillLightFrm;
    qDebug() << "m_pVolumeFrm:" << d->m_pVolumeFrm;
    qDebug() << "m_pListWidget:" << d->m_pListWidget;

    if (!d->m_pLanguageFrm || !d->m_pLuminanceFrm || 
        !d->m_pFillLightFrm || !d->m_pVolumeFrm || !d->m_pListWidget) {
        qDebug() << "Critical: One or more form objects are NULL!";
        return;
    }
    qDebug() << "All form pointers are valid";

    try {
        qDebug() << "About to call form setter methods...";
        
        qDebug() << "Setting language mode...";
        d->m_pLanguageFrm->setLanguageMode(config->getLanguage_Mode());
        qDebug() << "Language mode set successfully";
        
        qDebug() << "Setting luminance value...";
        d->m_pLuminanceFrm->setAdjustValue(config->getLuminance_Value());
        qDebug() << "Luminance value set successfully";
        
        qDebug() << "Setting fill light mode...";
        d->m_pFillLightFrm->setFillLightMode(config->getFillLight_Value());
        qDebug() << "Fill light mode set successfully";
        
        qDebug() << "Setting volume value...";
        d->m_pVolumeFrm->setAdjustValue(config->getVolume_Value());
        qDebug() << "Volume value set successfully";

        qDebug() << "Form values set, now updating UI display text...";

        // Check list widget item count
        int itemCount = d->m_pListWidget->count();
        qDebug() << "List widget item count:" << itemCount;
        
        if (itemCount < 4) {
            qDebug() << "Warning: List widget has fewer than 4 items, expected at least 4";
            return;
        }

        // Update UI display text with extensive debugging
        for (int i = 0; i < 4; ++i) {
            qDebug() << "Processing item" << i << "...";
            
            QListWidgetItem* item = d->m_pListWidget->item(i);
            qDebug() << "Item" << i << "pointer:" << item;
            
            if (!item) {
                qDebug() << "ERROR: Item" << i << "is null, skipping";
                continue;
            }
            
            qDebug() << "Getting widget for item" << i << "...";
            QWidget* widgetPtr = d->m_pListWidget->itemWidget(item);
            qDebug() << "Widget pointer for item" << i << ":" << widgetPtr;
            
            if (!widgetPtr) {
                qDebug() << "ERROR: Widget for item" << i << "is null, skipping";
                continue;
            }
            
            qDebug() << "Casting widget to CItemWidget for item" << i << "...";
            CItemWidget* widget = qobject_cast<CItemWidget*>(widgetPtr);
            qDebug() << "CItemWidget cast result for item" << i << ":" << widget;
            
            if (!widget) {
                qDebug() << "ERROR: Widget for item" << i << "is not CItemWidget, skipping";
                continue;
            }

            qDebug() << "About to set text for item" << i << "...";
            
            switch(i) {
                case 0: // Language
                    qDebug() << "Setting language text...";
                    widget->setRNameText(LanguageIdexToSTR(config->getLanguage_Mode()));
                    qDebug() << "Language text set successfully";
                    break;
                case 1: // Brightness
                    qDebug() << "Setting brightness text...";
                    widget->setRNameText(QString::number(config->getLuminance_Value()));
                    qDebug() << "Brightness text set successfully";
                    break;
                case 2: // Fill Light
                    qDebug() << "Setting fill light text...";
                    widget->setRNameText(FillLightIndexToSTR(config->getFillLight_Value()));
                    qDebug() << "Fill light text set successfully";
                    break;
                case 3: // Volume
                    qDebug() << "Setting volume text...";
                    widget->setRNameText(QString::number(config->getVolume_Value()));
                    qDebug() << "Volume text set successfully";
                    break;
            }
            
            qDebug() << "Item" << i << "processing completed successfully";
        }
        
        qDebug() << "All UI updates completed successfully";
        
    } catch (const std::exception& e) {
        qDebug() << "Exception in setEnter():" << e.what();
    } catch (...) {
        qDebug() << "Unknown exception caught in setEnter()";
    }
    
    qDebug() << "=== setEnter() DEBUG END ===";
}

void SysSetupFrm::setLeaveEvent()
{
    Q_D(SysSetupFrm);
	d->CheckTopState();	//点击离开时保存设置值
    if(!d->m_pLuminanceFrm->isHidden())d->m_pLuminanceFrm->hide();
    if(!d->m_pVolumeFrm->isHidden())d->m_pVolumeFrm->hide();
    QtConcurrent::run(ReadConfig::GetInstance(), &ReadConfig::setSaveConfig);
}

void SysSetupFrm::slotIemClicked(QListWidgetItem *item)
{
    Q_D(SysSetupFrm);
#ifdef SCREENCAPTURE  //ScreenCapture     
    grab().save(QString("/mnt/user/screenshot/%1.png").arg(this->metaObject()->className()),"png");  
#endif 
    int width = QApplication::desktop()->screenGeometry().width();    
    if(d->CheckTopState())
    {//不处理
        return;
    }
    else if(item == d->m_pListWidget->item(0))
    {//语言设置 (Language Settings)
        d->m_pLanguageFrm->exec();
        ReadConfig::GetInstance()->setLanguage_Mode(d->m_pLanguageFrm->getLanguageMode());
        ((CItemWidget *)d->m_pListWidget->itemWidget(d->m_pListWidget->item(0)))->setRNameText(LanguageIdexToSTR(ReadConfig::GetInstance()->getLanguage_Mode()));
    }
    else if(item == d->m_pListWidget->item(1))
    {//亮度设置 (Brightness Settings)
        d->m_pLuminanceFrm->show();
    }
    else if(item == d->m_pListWidget->item(2))
    {//补光设置 (Fill Light Settings)
        d->m_pFillLightFrm->exec();
        ReadConfig::GetInstance()->setFillLight_Value(d->m_pFillLightFrm->getFillLightMode());
        ((CItemWidget *)d->m_pListWidget->itemWidget(d->m_pListWidget->item(2)))->setRNameText(FillLightIndexToSTR(ReadConfig::GetInstance()->getFillLight_Value()));
    }
    else if(item == d->m_pListWidget->item(3))
    {//音量设置 (Volume Settings)
        d->m_pVolumeFrm->show();
    }
    else if(item == d->m_pListWidget->item(4))
    {//QR Code Enrollment
        QString serialNumber = DeviceInfo::getInstance()->getSerialNumber(); // Retrieve serial from DeviceInfo
        d->m_pQRCodeFrm->setSerialNumber();
        d->m_pQRCodeFrm->exec();
    }

     else if(item == d->m_pListWidget->item(5))
    {//Delete All Fingerprints
        d->m_pDeleteAllFingerprintsFrm->exec();
    }

       else if(item == d->m_pListWidget->item(6)) // Compare Finger (adjust index based on your actual position)
    {
        d->m_pCompareFingerFrm->exec();
    }
    else if(item == d->m_pListWidget->item(7))
    {//Storage Capacity
        emit sigShowFrm(((CItemWidget *)d->m_pListWidget->itemWidget(item))->getNameText());
    }
    else if(item == d->m_pListWidget->item(8))
    {//Login Password
        if (width==480)
            d->m_pLogPasswordChangeFrm->setFixedSize(d->m_pLogPasswordChangeFrm->width()-40, d->m_pLogPasswordChangeFrm->height());
        d->m_pLogPasswordChangeFrm->show();
        int ret = d->m_pLogPasswordChangeFrm->exec();
        if(ret == 0)
            ReadConfig::GetInstance()->setLoginPassword(d->m_pLogPasswordChangeFrm->getNewPasw());
    }
    else if(item == d->m_pListWidget->item(9))
    {//About
        emit sigShowFrm(((CItemWidget *)d->m_pListWidget->itemWidget(item))->getNameText());
    }
    else if (item == d->m_pListWidget->item(10)) // Sync Devices
    {
        SyncFunctionality::ShowSyncDialog();
    }
}

// rv1109
#ifdef SCREENCAPTURE  //ScreenCapture 
void SysSetupFrm::mouseDoubleClickEvent(QMouseEvent* event)
{
    grab().save(QString("/mnt/user/screenshot/%1.png").arg(this->metaObject()->className()),"png");    
}
#endif