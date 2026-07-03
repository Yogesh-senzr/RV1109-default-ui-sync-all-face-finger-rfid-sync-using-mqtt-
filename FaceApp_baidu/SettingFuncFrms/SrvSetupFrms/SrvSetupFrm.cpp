#include "SrvSetupFrm.h"

#include "Config/ReadConfig.h"
#include "MessageHandler/Log.h"
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QHBoxLayout>
#include <QSettings>
#include <QTextCodec>

// Define your hardcoded server addresses here
namespace HardcodedAddresses {
    const QString MAIN_SERVER_ADDRESS = "https://access.sensenservice.com/flows/trigger/6606d8bd-9fc1-4aeb-8a62-997662ad434a";
    const QString PERSON_RECORD_ADDRESS = "https://access.sensenservice.com/flows/trigger/340e7b83-35c7-4f0b-9b0c-5cbf753cb43b";
    const QString PERSON_REGISTRATION_ADDRESS = "https://access.sensenservice.com/flows/trigger/c67721fc-dee6-46b8-8016-6cfc7da20d85";
    const QString SYNC_USERS_ADDRESS = "https://access.sensenservice.com/flows/trigger/d81b8a1f-6b1f-43eb-939d-22cc5af7e670";
    const QString USER_DETAIL_ADDRESS = "https://access.sensenservice.com/flows/trigger/a8465e3d-3b4f-42c7-8f89-fd64571d40fd";
}

class SrvSetupFrmPrivate
{
    Q_DECLARE_PUBLIC(SrvSetupFrm)
public:
    SrvSetupFrmPrivate(SrvSetupFrm *dd);
private:
    void InitUI();
    void InitData();
    void InitConnect();
private:
    QLineEdit *m_pHttpServerAddress;
    QLineEdit *m_pHttpServerPassword;

    QLineEdit *m_pPostPersonRecordHttpServerAddress;
    QLineEdit *m_pPostPersonRecordHttpServerPassword;

    QLineEdit *m_pNewRegisteredPersonHttpServerAddress;
    QLineEdit *m_pNewRegisteredPersonHttpServerPassword;

    QLineEdit *m_pSyncUsersHttpServerAddress;
    QLineEdit *m_pSyncUsersHttpServerPassword;

    QLineEdit *m_pUserDetailHttpServerAddress;
    QLineEdit *m_pUserDetailHttpServerPassword;    

    QPushButton *m_pConfirmButton;
private:
    SrvSetupFrm *const q_ptr;
};

SrvSetupFrmPrivate::SrvSetupFrmPrivate(SrvSetupFrm *dd)
    : q_ptr(dd)
{
    this->InitUI();
    this->InitData();
    this->InitConnect();
}

SrvSetupFrm::SrvSetupFrm(QWidget *parent)
    : SettingBaseFrm(parent)
    , d_ptr(new SrvSetupFrmPrivate(this))
{

}

SrvSetupFrm::~SrvSetupFrm()
{

}

void SrvSetupFrmPrivate::InitUI()
{
    m_pHttpServerAddress = new QLineEdit;
    m_pHttpServerPassword = new QLineEdit;
    m_pPostPersonRecordHttpServerAddress = new QLineEdit;
    m_pPostPersonRecordHttpServerPassword = new QLineEdit;
    m_pNewRegisteredPersonHttpServerAddress = new QLineEdit;
    m_pNewRegisteredPersonHttpServerPassword = new QLineEdit;
    m_pSyncUsersHttpServerAddress = new QLineEdit;
    m_pSyncUsersHttpServerPassword = new QLineEdit;
    m_pUserDetailHttpServerAddress = new QLineEdit;
    m_pUserDetailHttpServerPassword = new QLineEdit; 

    // Make address fields read-only since they're hardcoded
    m_pHttpServerAddress->setReadOnly(true);
    m_pPostPersonRecordHttpServerAddress->setReadOnly(true);
    m_pNewRegisteredPersonHttpServerAddress->setReadOnly(true);
    m_pSyncUsersHttpServerAddress->setReadOnly(true);
    m_pUserDetailHttpServerAddress->setReadOnly(true);
    
    // Style for read-only fields
    QString readOnlyStyle = "QLineEdit { background-color: #f0f0f0; color: #666; border: 1px solid #ccc; }";
    m_pHttpServerAddress->setStyleSheet(readOnlyStyle);
    m_pPostPersonRecordHttpServerAddress->setStyleSheet(readOnlyStyle);
    m_pNewRegisteredPersonHttpServerAddress->setStyleSheet(readOnlyStyle);
    m_pSyncUsersHttpServerAddress->setStyleSheet(readOnlyStyle);
    m_pUserDetailHttpServerAddress->setStyleSheet(readOnlyStyle);

    // Disable context menu for all fields
    m_pHttpServerAddress->setContextMenuPolicy(Qt::NoContextMenu);
    m_pHttpServerPassword->setContextMenuPolicy(Qt::NoContextMenu);
    m_pPostPersonRecordHttpServerAddress->setContextMenuPolicy(Qt::NoContextMenu);
    m_pPostPersonRecordHttpServerPassword->setContextMenuPolicy(Qt::NoContextMenu);
    m_pNewRegisteredPersonHttpServerAddress->setContextMenuPolicy(Qt::NoContextMenu);  
    m_pNewRegisteredPersonHttpServerPassword->setContextMenuPolicy(Qt::NoContextMenu);
    m_pSyncUsersHttpServerAddress->setContextMenuPolicy(Qt::NoContextMenu);  
    m_pSyncUsersHttpServerPassword->setContextMenuPolicy(Qt::NoContextMenu);
    m_pUserDetailHttpServerAddress->setContextMenuPolicy(Qt::NoContextMenu);  
    m_pUserDetailHttpServerPassword->setContextMenuPolicy(Qt::NoContextMenu);

    m_pConfirmButton = new QPushButton;//确定
    QVBoxLayout *vLayout = new QVBoxLayout;
    vLayout->addWidget(new QLabel(QObject::tr("HTTPServerAddress (Fixed)"))); // Updated label
    vLayout->addWidget(m_pHttpServerAddress);

    QVBoxLayout *vLayout1 = new QVBoxLayout;
    vLayout1->addWidget(new QLabel(QObject::tr("HTTPServerPassword")));//HTTP服务器密码
    vLayout1->addWidget(m_pHttpServerPassword);

    QVBoxLayout *vLayout2 = new QVBoxLayout;
    vLayout2->addWidget(new QLabel(QObject::tr("PostPersonRecordHttpServerAddress (Fixed)"))); // Updated label
    vLayout2->addWidget(m_pPostPersonRecordHttpServerAddress);

    QVBoxLayout *vLayout3 = new QVBoxLayout;
    vLayout3->addWidget(new QLabel(QObject::tr("PostPersonRecordHttpServerPassword")));//HTTP离线推送识别记录服务器密码
    vLayout3->addWidget(m_pPostPersonRecordHttpServerPassword);

    QVBoxLayout *vLayout4 = new QVBoxLayout;
    vLayout4->addWidget(new QLabel(QObject::tr("NewRegisteredPersonHttpServerAddress (Fixed)"))); // Updated label
    vLayout4->addWidget(m_pNewRegisteredPersonHttpServerAddress);

    QVBoxLayout *vLayout5 = new QVBoxLayout;
    vLayout5->addWidget(new QLabel(QObject::tr("NewRegisteredPersonHttpServerPassword")));
    vLayout5->addWidget(m_pNewRegisteredPersonHttpServerPassword);

    QVBoxLayout *vLayout6 = new QVBoxLayout;
    vLayout6->addWidget(new QLabel(QObject::tr("SyncUsersHttpServerAddress (Fixed)"))); // Updated label
    vLayout6->addWidget(m_pSyncUsersHttpServerAddress);

    QVBoxLayout *vLayout7 = new QVBoxLayout;
    vLayout7->addWidget(new QLabel(QObject::tr("SyncUsersHttpServerPassword")));
    vLayout7->addWidget(m_pSyncUsersHttpServerPassword);

    QVBoxLayout *vLayout8 = new QVBoxLayout;
    vLayout8->addWidget(new QLabel(QObject::tr("UserDetailHttpServerAddress (Fixed)"))); // Updated label
    vLayout8->addWidget(m_pUserDetailHttpServerAddress);

    QVBoxLayout *vLayout9 = new QVBoxLayout;
    vLayout9->addWidget(new QLabel(QObject::tr("UserDetailHttpServerPassword")));
    vLayout9->addWidget(m_pUserDetailHttpServerPassword);

    QHBoxLayout *hLayout = new QHBoxLayout;
    hLayout->addStretch();
    hLayout->addWidget(m_pConfirmButton);
    hLayout->addStretch();

    QVBoxLayout *malayout = new QVBoxLayout(q_func());
    malayout->setSpacing(0);
    malayout->setContentsMargins(30, 0, 30, 20);
    malayout->addLayout(vLayout);
    malayout->addSpacing(5);
    malayout->addLayout(vLayout1);
    malayout->addSpacing(5);
    malayout->addLayout(vLayout2);
    malayout->addSpacing(5);
    malayout->addLayout(vLayout3);
    malayout->addSpacing(5);
    malayout->addLayout(vLayout4);
    malayout->addSpacing(5);
    malayout->addLayout(vLayout5);
    malayout->addSpacing(5);
    malayout->addLayout(vLayout6);
    malayout->addSpacing(5);
    malayout->addLayout(vLayout7);
    malayout->addSpacing(5);
    malayout->addLayout(vLayout8);
    malayout->addSpacing(5);
    malayout->addLayout(vLayout9);
    malayout->addSpacing(10);
    malayout->addLayout(hLayout);

    malayout->addStretch();
}

void SrvSetupFrmPrivate::InitData()
{
    m_pConfirmButton->setFixedSize(282, 62);
    m_pConfirmButton->setText(QObject::tr("Save"));//保存 
}

void SrvSetupFrmPrivate::InitConnect()
{
    QObject::connect(m_pConfirmButton, &QPushButton::clicked, q_func(), &SrvSetupFrm::slotConfirmButton);
}

void SrvSetupFrm::setEnter()
{
    Q_D(SrvSetupFrm);

    // Set hardcoded addresses directly
    d->m_pHttpServerAddress->setText(HardcodedAddresses::MAIN_SERVER_ADDRESS);
    d->m_pPostPersonRecordHttpServerAddress->setText(HardcodedAddresses::PERSON_RECORD_ADDRESS);
    d->m_pNewRegisteredPersonHttpServerAddress->setText(HardcodedAddresses::PERSON_REGISTRATION_ADDRESS);
    d->m_pSyncUsersHttpServerAddress->setText(HardcodedAddresses::SYNC_USERS_ADDRESS);
    d->m_pUserDetailHttpServerAddress->setText(HardcodedAddresses::USER_DETAIL_ADDRESS);

    // Load passwords from configuration (these can still be changed)
    d->m_pHttpServerPassword->setText(ReadConfig::GetInstance()->getSrv_Manager_Password());
    d->m_pPostPersonRecordHttpServerPassword->setText(ReadConfig::GetInstance()->getPost_PersonRecord_Password());
    d->m_pNewRegisteredPersonHttpServerPassword->setText(ReadConfig::GetInstance()->getPerson_Registration_Password());
    d->m_pSyncUsersHttpServerPassword->setText(ReadConfig::GetInstance()->getSyncUsersPassword());
    d->m_pUserDetailHttpServerPassword->setText(ReadConfig::GetInstance()->getUserDetailPassword());

    // Add debug log
    LogD("Using hardcoded Person Registration URL: %s\n", 
         HardcodedAddresses::PERSON_REGISTRATION_ADDRESS.toStdString().c_str());
}

void SrvSetupFrm::slotConfirmButton()
{
    Q_D(SrvSetupFrm);
    
    // Only save passwords since addresses are hardcoded
    ReadConfig::GetInstance()->setSrv_Manager_Password(d->m_pHttpServerPassword->text());
    ReadConfig::GetInstance()->setPost_PersonRecord_Password(d->m_pPostPersonRecordHttpServerPassword->text());
    ReadConfig::GetInstance()->setPerson_Registration_Password(d->m_pNewRegisteredPersonHttpServerPassword->text());
    ReadConfig::GetInstance()->setSyncUsersPassword(d->m_pSyncUsersHttpServerPassword->text());
    ReadConfig::GetInstance()->setUserDetailPassword(d->m_pUserDetailHttpServerPassword->text());
    
    // Note: Not saving addresses since they're hardcoded
    LogD("Configuration saved - addresses are hardcoded, only passwords saved\n");
    
    ReadConfig::GetInstance()->setSaveConfig();
}

void SrvSetupFrm::slotIemClicked(QListWidgetItem */*item*/)
{

}

#ifdef SCREENCAPTURE  //ScreenCapture  
void SrvSetupFrm::mouseDoubleClickEvent(QMouseEvent* event)
{
    grab().save(QString("/mnt/user/screenshot/%1.png").arg(this->metaObject()->className()),"png");    
}
#endif