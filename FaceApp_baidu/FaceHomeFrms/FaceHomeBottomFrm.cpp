#include "FaceHomeBottomFrm.h"
#include "FaceMainFrm.h"  // Add this include
#include "Config/ReadConfig.h"
#include <QtWidgets/QLabel>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QVBoxLayout>
#include <QtCore/QDateTime>
#include <QtWidgets/QStyleOption>
#include <QtGui/QPainter>
#include <QtGui/QMouseEvent>  // Add this include for QMouseEvent
#include <QtCore/QDebug>
#include <QtCore/QCoreApplication>

class FaceHomeBottomFrmPrivate
{
    Q_DECLARE_PUBLIC(FaceHomeBottomFrm)
public:
    FaceHomeBottomFrmPrivate(FaceHomeBottomFrm *dd);
private:
    void InitUI();
    void InitData();
    void InitConnect();
private:
    // Replace old labels with sync-related labels
    QLabel *m_pTenantNameLabel;    // Shows tenant name
    QLabel *m_pSyncCountLabel;     // Shows "15/488 users synced"
    QLabel *m_pSyncStatusLabel;    // Shows "Sync Status: Completed/Running/Failed"
    QLabel *m_pLastSyncLabel;      // Shows "Last Sync: 2025-01-15 10:30"
    QLabel *m_pLocalFaceCountLabel;
    QLabel *m_pHomeIconLabel;   
        QLabel *m_pIPLabel;       // For IP address display
   // Home icon for settings access - MAKE SURE THIS IS HERE
private:
    FaceHomeBottomFrm *m_FaceHomeBottomFrm;
private:
    FaceHomeBottomFrm *const q_ptr;
};

FaceHomeBottomFrmPrivate::FaceHomeBottomFrmPrivate(FaceHomeBottomFrm *dd)
    : q_ptr(dd)
{
    this->InitUI();
    this->InitData();
    this->InitConnect();
}

FaceHomeBottomFrm::FaceHomeBottomFrm(QWidget *parent)
    : HomeBottomBaseFrm(parent)
    , d_ptr(new FaceHomeBottomFrmPrivate(this))
{

}

FaceHomeBottomFrm::~FaceHomeBottomFrm()
{

}

void FaceHomeBottomFrmPrivate::InitUI()
{
    // Create all labels
    m_pTenantNameLabel = new QLabel;
    m_pSyncCountLabel = new QLabel;
    m_pSyncStatusLabel = new QLabel;
    m_pLastSyncLabel = new QLabel;
    m_pLocalFaceCountLabel = new QLabel;
    
    // ENSURE THESE ARE CREATED:
    m_pIPLabel = new QLabel;
   // m_pMacLabel = new QLabel;
    
    // Create home icon
    m_pHomeIconLabel = new QLabel;
    m_pHomeIconLabel->setPixmap(QPixmap(":/Images/Homeicon.png").scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_pHomeIconLabel->setAlignment(Qt::AlignCenter);
    m_pHomeIconLabel->setMinimumSize(60, 60);
    m_pHomeIconLabel->setStyleSheet("QLabel { padding: 6px; border-radius: 30px; } QLabel:hover { background-color: rgba(255,255,255,0.1); }");

    // Create main layout
    QHBoxLayout *mainLayout = new QHBoxLayout(q_func());
    mainLayout->setContentsMargins(20, 5, 20, 5);
    
    // Left side: all information in vertical layout
    QVBoxLayout *leftLayout = new QVBoxLayout;
    leftLayout->setSpacing(3);
    
    // Add all labels to the layout
    leftLayout->addWidget(m_pTenantNameLabel);
    leftLayout->addWidget(m_pSyncCountLabel);
    leftLayout->addWidget(m_pLocalFaceCountLabel);
    leftLayout->addWidget(m_pSyncStatusLabel);
    leftLayout->addWidget(m_pLastSyncLabel);
    
    // ADD THE NETWORK INFO LABELS:
    leftLayout->addWidget(m_pIPLabel);      // IP address
  //  leftLayout->addWidget(m_pMacLabel);     // MAC address
    
    // Add left layout to main layout
    mainLayout->addLayout(leftLayout);
    mainLayout->addStretch(1);
    mainLayout->addWidget(m_pHomeIconLabel);
    mainLayout->addStretch(4);
}
void FaceHomeBottomFrm::setNetInfo(const QString &address, const QString &make)
{
    Q_D(FaceHomeBottomFrm);
    
    // Get display settings from ReadConfig
    bool showIP = ReadConfig::GetInstance()->getHomeDisplay_DisplayIP();
   // bool showMAC = ReadConfig::GetInstance()->getHomeDisplay_DisplayMac();
    
    qDebug() << "setNetInfo called - ShowIP:" << showIP;
    qDebug() << "IP:" << address << "MAC:" << make;
    
    // Only update and show IP if enabled in config
    if (showIP && d->m_pIPLabel) {
        if (address.isEmpty()) {
            d->m_pIPLabel->setText(QObject::tr("IP: Not Available"));
        } else {
            d->m_pIPLabel->setText(QObject::tr("IP: %1").arg(address));
        }
        d->m_pIPLabel->show();
        qDebug() << "IP label updated and shown:" << d->m_pIPLabel->text();
    } else if (d->m_pIPLabel) {
        d->m_pIPLabel->hide();
        qDebug() << "IP label hidden (disabled in config)";
    }
    
}
void FaceHomeBottomFrmPrivate::InitData()
{
    // Set object names for styling
    m_pTenantNameLabel->setObjectName("FaceHomeBottomLabel");
    m_pSyncCountLabel->setObjectName("FaceHomeBottomLabel");
    m_pSyncStatusLabel->setObjectName("FaceHomeBottomLabel");
    m_pLastSyncLabel->setObjectName("FaceHomeBottomLabel");
    m_pLocalFaceCountLabel->setObjectName("FaceHomeBottomLabel");
    
    // ENSURE THESE HAVE STYLING:
    m_pIPLabel->setObjectName("FaceHomeBottomLabel");
  //  m_pMacLabel->setObjectName("FaceHomeBottomLabel");
    
    m_pHomeIconLabel->setObjectName("HomeIconLabel");

    // Set initial text
    m_pTenantNameLabel->setText(QObject::tr("Tenant: Not Available"));
    m_pSyncCountLabel->setText(QObject::tr("Users: 0/0"));
    m_pLocalFaceCountLabel->setText(QObject::tr("Local Faces: 0/0"));
    m_pSyncStatusLabel->setText(QObject::tr("Sync Status: Not Started"));
    m_pLastSyncLabel->setText(QObject::tr("Last Sync: Never"));
    
    // SET INITIAL NETWORK INFO:
    m_pIPLabel->setText(QObject::tr("IP: Detecting..."));
   // m_pMacLabel->setText(QObject::tr("MAC: Detecting..."));

    // Increase height to accommodate 7 items (was 130, then 150)
    q_func()->setFixedHeight(170);
}

void FaceHomeBottomFrmPrivate::InitConnect()
{
    // No connections needed for now
}

// IMPLEMENTATION OF ALL METHODS:

void FaceHomeBottomFrm::setTenantName(const QString &tenantName)
{
    Q_D(FaceHomeBottomFrm);
    if (tenantName.isEmpty()) {
        d->m_pTenantNameLabel->setText(QObject::tr("Tenant: Not Available"));
    } else {
        d->m_pTenantNameLabel->setText(QObject::tr("Tenant: %1").arg(tenantName));
    }
}

void FaceHomeBottomFrm::setSyncUserCount(int currentCount, int totalCount)
{
    Q_D(FaceHomeBottomFrm);
    d->m_pSyncCountLabel->setText(QObject::tr("Users: %1/%2").arg(currentCount).arg(totalCount));
}

void FaceHomeBottomFrm::setSyncStatus(const QString &status)
{
    Q_D(FaceHomeBottomFrm);
    QString statusText;
    
    if (status.toLower() == "completed") {
        statusText = QObject::tr("Sync Status: ✓ Completed");
    } else if (status.toLower() == "running") {
        statusText = QObject::tr("Sync Status: ⟳ Running...");
    } else if (status.toLower() == "syncing") {
        statusText = QObject::tr("Sync Status: ⟳ Syncing...");
    } else if (status.toLower() == "failed") {
        statusText = QObject::tr("Sync Status: ✗ Failed");
    } else if (status.toLower() == "partial") {
        statusText = QObject::tr("Sync Status: ⚠ Partial");
    } else if (status.toLower() == "up to date") {
        statusText = QObject::tr("Sync Status: ✓ Up to Date");
    } else if (status.toLower() == "disabled") {
        statusText = QObject::tr("Sync Status: ⊘ Disabled");
    } else if (status.toLower() == "checking") {
        statusText = QObject::tr("Sync Status: ⋯ Checking");
    } else if (status.toLower() == "analyzing") {
        statusText = QObject::tr("Sync Status: ⚡ Analyzing");
    } else if (status.toLower() == "getting server list") {
        statusText = QObject::tr("Sync Status: ⬇ Getting List");
    } else if (status.toLower() == "processing") {
        statusText = QObject::tr("Sync Status: ⚙ Processing");
    } else if (status.toLower() == "removing extra users") {
        statusText = QObject::tr("Sync Status: ⚠ Removing");
    } else if (status.toLower() == "finding updates") {
        statusText = QObject::tr("Sync Status: ⚡ Finding Updates");
    } else if (status.contains("fetching users")) {
        statusText = QObject::tr("Sync Status: ⬇ %1").arg(status);
    } else {
        statusText = QObject::tr("Sync Status: %1").arg(status);
    }
    
    d->m_pSyncStatusLabel->setText(statusText);
}

void FaceHomeBottomFrm::setLastSyncTime(const QString &time)
{
    Q_D(FaceHomeBottomFrm);
    if (time.isEmpty()) {
        d->m_pLastSyncLabel->setText(QObject::tr("Last Sync: Never"));
    } else {
        d->m_pLastSyncLabel->setText(QObject::tr("Last Sync: %1").arg(time));
    }
}

void FaceHomeBottomFrm::setLocalFaceCount(int localCount, int totalCount)
{
    Q_D(FaceHomeBottomFrm);
    d->m_pLocalFaceCountLabel->setText(QObject::tr("Local Faces: %1/%2").arg(localCount).arg(totalCount));
}

void FaceHomeBottomFrm::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    QStyleOption opt;
    opt.init(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &painter, this);
    QWidget::paintEvent(event);
}

void FaceHomeBottomFrm::mousePressEvent(QMouseEvent *event)
{
    Q_D(FaceHomeBottomFrm);
    
    // Check if the click is on the home icon area
    if (d->m_pHomeIconLabel && d->m_pHomeIconLabel->geometry().contains(event->pos())) {
        qDebug() << "Home icon clicked - triggering settings access";
        
        // Get the main window and trigger the settings
        QWidget *mainWindow = this;
        while (mainWindow->parent() && qobject_cast<QWidget*>(mainWindow->parent())) {
            mainWindow = qobject_cast<QWidget*>(mainWindow->parent());
        }
        
        // Try to find FaceMainFrm and trigger the settings
        FaceMainFrm *faceMainFrm = qobject_cast<FaceMainFrm*>(mainWindow);
        if (faceMainFrm) {
            qDebug() << "Calling triggerSettings on FaceMainFrm";
            faceMainFrm->triggerSettings();
        } else {
            qDebug() << "Could not find FaceMainFrm parent";
        }
    }
    
    QWidget::mousePressEvent(event);
}