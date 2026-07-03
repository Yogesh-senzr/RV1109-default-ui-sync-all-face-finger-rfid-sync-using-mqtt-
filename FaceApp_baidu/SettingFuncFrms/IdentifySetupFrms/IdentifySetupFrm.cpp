#include "IdentifySetupFrm.h"

#include "IdentifyDistanceFrm.h"

#include "../SetupItemDelegate/CItemWidget.h"
#include "../SetupItemDelegate/CItemBoxWidget.h"
#include "SettingFuncFrms/SetupItemDelegate/CInputBaseDialog.h"
#include "Config/ReadConfig.h"
#include "HttpServer/ConnHttpServerThread.h"

#include <QListWidget>
#include <QHBoxLayout>
#include <QApplication>
#include <QScreen>
#include <QDesktopWidget>
#include <QtConcurrent/QtConcurrent>
#include <QMessageBox>

class IdentifySetupFrmPrivate
{
    Q_DECLARE_PUBLIC(IdentifySetupFrm)
public:
    IdentifySetupFrmPrivate(IdentifySetupFrm *dd);
private:
    void InitUI();
    void InitData();
    void InitConnect();
public:
    bool CheckTopWidget();
private:
    QListWidget *m_pListWidget;
private:
    IdentifySetupFrm *const q_ptr;
};

IdentifySetupFrmPrivate::IdentifySetupFrmPrivate(IdentifySetupFrm *dd)
    : q_ptr(dd)
{
    this->InitUI();
    this->InitData();
    this->InitConnect();
}

IdentifySetupFrm::IdentifySetupFrm(QWidget *parent)
    : SettingBaseFrm(parent)
    , d_ptr(new IdentifySetupFrmPrivate(this))
{

}

IdentifySetupFrm::~IdentifySetupFrm()
{

}

void IdentifySetupFrmPrivate::InitUI()
{
    m_pListWidget = new QListWidget;
    QHBoxLayout *layout = new QHBoxLayout(q_func());
    layout->setSpacing(0);
    layout->setMargin(0);
    layout->addWidget(m_pListWidget);
}

void IdentifySetupFrmPrivate::InitData()
{
    int width = QApplication::desktop()->screenGeometry().width();
    int spacing = 0;
    switch(width)
    {
    case 480:spacing = 14;break;		
    case 600:spacing = 14;break;
    case 720:spacing = 26;break;
    case 800:spacing = 0;break;
    }

    m_pListWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // Living Body Detection (Index 0)
    {
        QListWidgetItem *pItem = new QListWidgetItem;
        CItemBoxWidget *pItemWidget = new CItemBoxWidget;
        pItemWidget->setAddSpacing(spacing);
        pItemWidget->setAddSpacing(10);

        pItemWidget->setData(QObject::tr("LivingBodyDetection"), ":/Images/SmallRound.png");//"活体检测"
		if (width ==480)
			pItemWidget->setAddSpacing(120);
        m_pListWidget->addItem(pItem);
        m_pListWidget->setItemWidget(pItem, pItemWidget);
    }
    
    // Living Threshold (Index 1)
    {
        QListWidgetItem *pItem = new QListWidgetItem;
        CItemWidget *pItemWidget = new CItemWidget;
        pItemWidget->setAddSpacing(spacing);
        pItemWidget->setAddSpacing(10);

        pItemWidget->setData(QObject::tr("LivingThreshold"), ":/Images/SmallRound.png", "0.7");//活体阈值
		if (width ==480)
			pItemWidget->setAddSpacing(120);		
        m_pListWidget->addItem(pItem);
        m_pListWidget->setItemWidget(pItem, pItemWidget);
    }
    
    // Comparison Threshold (Index 2)
    {
        QListWidgetItem *pItem = new QListWidgetItem;
        CItemWidget *pItemWidget = new CItemWidget;
        pItemWidget->setAddSpacing(spacing);
        pItemWidget->setAddSpacing(10);

        pItemWidget->setData(QObject::tr("ComparisonThreshold"), ":/Images/SmallRound.png", "0.8");//比对阈值
		if (width ==480)
			pItemWidget->setAddSpacing(120);		
        m_pListWidget->addItem(pItem);
        m_pListWidget->setItemWidget(pItem, pItemWidget);
    }
    
    // Face Quality Threshold (Index 3)
    {
        QListWidgetItem *pItem = new QListWidgetItem;
        CItemWidget *pItemWidget = new CItemWidget;
        pItemWidget->setAddSpacing(spacing);
        pItemWidget->setAddSpacing(10);

        pItemWidget->setData(QObject::tr("FaceQualityThreshold"), ":/Images/SmallRound.png", "0.5");//人脸质量阈值
		if (width ==480)
			pItemWidget->setAddSpacing(120);
        m_pListWidget->addItem(pItem);
        m_pListWidget->setItemWidget(pItem, pItemWidget);
    }

    // Identification Interval (Index 4)
    {
        QListWidgetItem *pItem = new QListWidgetItem;
        CItemWidget *pItemWidget = new CItemWidget;
        pItemWidget->setAddSpacing(spacing);
        pItemWidget->setAddSpacing(10);

        pItemWidget->setData(QObject::tr("IdentificationInterval"), ":/Images/SmallRound.png", "1s");//识别间隔
		if (width ==480)
			pItemWidget->setAddSpacing(120);		
        m_pListWidget->addItem(pItem);
        m_pListWidget->setItemWidget(pItem, pItemWidget);
    }
    
    // Recognition Distance (Index 5)
    {
        QListWidgetItem *pItem = new QListWidgetItem;
        CItemWidget *pItemWidget = new CItemWidget;
        pItemWidget->setAddSpacing(spacing);
        pItemWidget->setAddSpacing(10);

        pItemWidget->setData(QObject::tr("RecognitionDistance"), ":/Images/SmallRound.png", "150cm");//识别距离
		if (width ==480)
			pItemWidget->setAddSpacing(120);		
        m_pListWidget->addItem(pItem);
        m_pListWidget->setItemWidget(pItem, pItemWidget);
    }
}

void IdentifySetupFrmPrivate::InitConnect()
{
    QObject::connect(m_pListWidget, &QListWidget::itemClicked, q_func(), &IdentifySetupFrm::slotIemClicked);
}

bool IdentifySetupFrmPrivate::CheckTopWidget()
{
    foreach (QWidget *w, qApp->topLevelWidgets()) {
        if(w->objectName() == "InputBaseDialog")return true;
    }
    return false;
}

static inline QString IdentifyDistanceToSTR(const int &index)
{
    switch(index)
    {
    case 0: return QString("50cm");
    case 1: return QString("100cm");
    default :return QString("150cm");
    }
}

void IdentifySetupFrm::setEnter()
{
    Q_D(IdentifySetupFrm);
    
    // Get the HTTP server thread instance directly using your singleton
    ConnHttpServerThread* httpThread = ConnHttpServerThread::GetInstance();
    
    // Living Body Detection (Index 0) - keep from ReadConfig (this is a boolean, not a threshold)
    ((CItemBoxWidget *)d->m_pListWidget->itemWidget(d->m_pListWidget->item(0)))->setCheckBoxState(
        ReadConfig::GetInstance()->getIdentity_Manager_CheckLiving());
    
    if (httpThread && httpThread->hasHeartbeatThresholds()) {
        qDebug() << "DEBUG: setEnter - Using ALL values from heartbeat";
        
        // Living Threshold (Index 1) - liveness_threshold from heartbeat
        float livenessThreshold = httpThread->getHeartbeatLivenessThreshold();
        ((CItemWidget *)d->m_pListWidget->itemWidget(d->m_pListWidget->item(1)))->setRNameText(
            QString::number(livenessThreshold, 'f', 2));
        
        // Comparison Threshold (Index 2) - comparison_threshold from heartbeat
        float comparisonThreshold = httpThread->getHeartbeatComparisonThreshold();
        ((CItemWidget *)d->m_pListWidget->itemWidget(d->m_pListWidget->item(2)))->setRNameText(
            QString::number(comparisonThreshold, 'f', 2));
        
        // Face Quality Threshold (Index 3) - quality_threshold from heartbeat
        float qualityThreshold = httpThread->getHeartbeatQualityThreshold();
        ((CItemWidget *)d->m_pListWidget->itemWidget(d->m_pListWidget->item(3)))->setRNameText(
            QString::number(qualityThreshold, 'f', 2));
        
        // Identification Interval (Index 4) - identification_interval from heartbeat
        int identificationInterval = httpThread->getHeartbeatIdentificationInterval();
        ((CItemWidget *)d->m_pListWidget->itemWidget(d->m_pListWidget->item(4)))->setRNameText(
            QString::number(identificationInterval) + "s");
        
        // Recognition Distance (Index 5) - recognition_distance from heartbeat
        int recognitionDistance = httpThread->getHeartbeatRecognitionDistance();
        ((CItemWidget *)d->m_pListWidget->itemWidget(d->m_pListWidget->item(5)))->setRNameText(
            QString::number(recognitionDistance) + "cm");
        
        qDebug() << "DEBUG: setEnter - Applied ALL heartbeat values:";
        qDebug() << "  liveness_threshold:" << livenessThreshold;
        qDebug() << "  comparison_threshold:" << comparisonThreshold;
        qDebug() << "  quality_threshold:" << qualityThreshold;
        qDebug() << "  identification_interval:" << identificationInterval;
        qDebug() << "  recognition_distance:" << recognitionDistance;
    } else {
        qDebug() << "WARNING: setEnter - No heartbeat values available";
        
        // Show placeholder when heartbeat data is not available for ALL items
        ((CItemWidget *)d->m_pListWidget->itemWidget(d->m_pListWidget->item(1)))->setRNameText("--");
        ((CItemWidget *)d->m_pListWidget->itemWidget(d->m_pListWidget->item(2)))->setRNameText("--");
        ((CItemWidget *)d->m_pListWidget->itemWidget(d->m_pListWidget->item(3)))->setRNameText("--");
        ((CItemWidget *)d->m_pListWidget->itemWidget(d->m_pListWidget->item(4)))->setRNameText("--");
        ((CItemWidget *)d->m_pListWidget->itemWidget(d->m_pListWidget->item(5)))->setRNameText("--");
    }
}

void IdentifySetupFrm::slotIemClicked(QListWidgetItem *item)
{
    Q_D(IdentifySetupFrm);
#ifdef SCREENCAPTURE        
    grab().save(QString("/mnt/user/screenshot/%1.png").arg(this->metaObject()->className()),"png");   
#endif    
    if(d->CheckTopWidget())return;
    
    // Get heartbeat thread directly using your singleton
    ConnHttpServerThread* httpThread = ConnHttpServerThread::GetInstance();
    
    if(item == d->m_pListWidget->item(1))
    {//Living Threshold (Index 1) - read-only from heartbeat
        return;
    }
    else if(item == d->m_pListWidget->item(2))
    {//Comparison Threshold (Index 2) - read-only from heartbeat
        return;
    }
    else if(item == d->m_pListWidget->item(3))
    {//Face Quality Threshold (Index 3) - read-only from heartbeat
        return;
    }
    else if(item == d->m_pListWidget->item(4))
    {//Identification Interval (Index 4) - now read-only from heartbeat
        return;
    }
    else if(item == d->m_pListWidget->item(5))
    {//Recognition Distance (Index 5) - now read-only from heartbeat
        return;
    }
}

void IdentifySetupFrm::setLeaveEvent()
{
    Q_D(IdentifySetupFrm);
    // Only save the Living Body Detection checkbox state since all other values come from heartbeat
    ReadConfig::GetInstance()->setIdentity_Manager_CheckLiving(
        ((CItemBoxWidget *)d->m_pListWidget->itemWidget(d->m_pListWidget->item(0)))->getCheckBoxState() ? 1 : 0);
    
    // Close any remaining dialogs (though there shouldn't be any now)
    foreach (QWidget *w, qApp->topLevelWidgets()) {
        if(w->objectName() == "InputBaseDialog")
        {
            QDialog *dlg = (QDialog *)w;
            dlg->done(1);
        }
    }

    QtConcurrent::run(ReadConfig::GetInstance(), &ReadConfig::setSaveConfig);
}
#ifdef SCREENCAPTURE  //ScreenCapture   
void IdentifySetupFrm::mouseDoubleClickEvent(QMouseEvent* event)
{
    grab().save(QString("/mnt/user/screenshot/%1.png").arg(this->metaObject()->className()),"png");   
}	
#endif