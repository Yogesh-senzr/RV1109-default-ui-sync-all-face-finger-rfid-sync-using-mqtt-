#include "AddUserFrm.h"

#include "CameraPicFrm.h"
#include "SettingFuncFrms/SettingMenuTitleFrm.h"

#include "BaiduFace/BaiduFaceManager.h"
#include "OperationTipsFrm.h"

#include "DB/RegisteredFacesDB.h"
#include "Application/FaceApp.h"

#include "SettingFuncFrms/SysSetupFrms/LanguageFrm.h"
#include "UserViewFrm.h"
#include "HttpServer/ConnHttpServerThread.h"
#include "BaiduFace/FingerprintManager.h" // NEW: Include fingerprint manager header
#include "MqttHeartbeatManager.h"  // NEW: Include MQTT manager for face publishing


#include <QPropertyAnimation>
#include <QScreen>
#include <QApplication>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlError>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QStyleOption>
#include <QtGui/QPainter>
#include <QtWidgets/QHBoxLayout>
#include <QtConcurrent/QtConcurrent>
#include <QtCore/QFile>
#include <QtCore/QDebug>
#include <QApplication>
#include <QTransform>
#include <QImageReader>
#include <QSettings>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>                    // For QDir class (directory operations)
#include <QThread>                 // For QThread::msleep() function
#include <QJsonObject>             // For JSON payload creation (debugging)
#include <QJsonDocument>           // For JSON document handling (debugging)

static PERSONS_t mPerson = {0};
class AddUserFrmPrivate
{
    Q_DECLARE_PUBLIC(AddUserFrm)
public:
    AddUserFrmPrivate(AddUserFrm *dd);
private:
    void InitUI();
    void InitData();
    void InitConnect();
private:
    CameraPicFrm *m_pCameraPicFrm;//å®žæ—¶æ˜¾ç¤ºç›¸æœºçš„å›¾ç‰‡
private:
    SettingMenuTitleFrm *m_pSettingMenuTitleFrm;
private:
    QLineEdit *m_pNameEdit;//å§“å
    QLineEdit *m_pIDCardEdit;//èº«ä»½è¯
    QLineEdit *m_pCardEdit;//å¡å·
   // QComboBox *m_psexBox;//æ€§åˆ«
    QPushButton *m_pCaptureFaceButton;//é‡‡é›†äººè„¸
    QPushButton *m_pRegFaceButton;//æ³¨å†Œäººè„¸
    QPushButton *m_pCaptureFingerButton;
    QPushButton *m_pRegFingerButton;
    QPushButton *m_pDeleteFingerButton;
    QLabel *m_pFingerStatusLabel;
private:
    QByteArray mFaceFeature;
    QByteArray mFingerTemplate;  // NEW: Will store fingerprint data
    QImage mCapturedFaceImage;   // NEW: Store captured face image for MQTT publishing

private:
    QString mHintText;
    QString mFingerHintText;     // NEW: Status message for fingerprint

private:
    AddUserFrm *const q_ptr;
};

AddUserFrmPrivate::AddUserFrmPrivate(AddUserFrm *dd)
    : q_ptr(dd)
{
    this->InitUI();
    this->InitData();
    this->InitConnect();
}

AddUserFrm::AddUserFrm(QWidget *parent)
    : QWidget(parent)
    , d_ptr(new AddUserFrmPrivate(this))
{
    d_func()->m_pCameraPicFrm = new CameraPicFrm(this);//å®žæ—¶æ˜¾ç¤ºç›¸æœºçš„å›¾ç‰‡
    d_func()->m_pCameraPicFrm->setFixedSize(300, 256);
    d_func()->m_pCameraPicFrm->hide();
    QObject::connect(this, &AddUserFrm::sigModifyUser, this, &AddUserFrm::slotModifyUser);
}

AddUserFrm::~AddUserFrm()
{

}

void AddUserFrmPrivate::InitUI()
{
    LanguageFrm::GetInstance()->UseLanguage(0);
    m_pSettingMenuTitleFrm = new SettingMenuTitleFrm;

    m_pNameEdit = new QLineEdit;
    m_pIDCardEdit = new QLineEdit;
    m_pCardEdit = new QLineEdit;
    
    m_pCaptureFaceButton = new QPushButton;
    m_pRegFaceButton = new QPushButton;
    
    // NEW: Initialize fingerprint controls
    m_pCaptureFingerButton = new QPushButton;
    m_pRegFingerButton = new QPushButton;
    m_pFingerStatusLabel = new QLabel;

    m_pDeleteFingerButton = new QPushButton;
m_pDeleteFingerButton->setFixedSize(220, 62);
m_pDeleteFingerButton->setText(QObject::tr("Delete Fingerprint"));
m_pDeleteFingerButton->setEnabled(false);

    // Setup required field indicators (existing code)
    {
        QLabel *StarLabel = new QLabel("<p><font color=\"red\">*</font></p>");
        StarLabel->setStyleSheet("background:transparent;");
        QHBoxLayout* StarLayout = new QHBoxLayout;
        StarLayout->addStretch();
        StarLayout->addWidget(StarLabel);
        StarLayout->setSpacing(0);
        StarLayout->setContentsMargins(0, 0, 3, 0);
        m_pNameEdit->setLayout(StarLayout);
        m_pNameEdit->setContextMenuPolicy(Qt::NoContextMenu);

        QLabel *idCardStar = new QLabel("<p><font color=\"red\">*</font></p>");
        idCardStar->setStyleSheet("background:transparent;");
        QHBoxLayout* idCardLayout = new QHBoxLayout;
        idCardLayout->addStretch();
        idCardLayout->addWidget(idCardStar);
        idCardLayout->setSpacing(0);
        idCardLayout->setContentsMargins(3, 0, 3, 0);
        m_pIDCardEdit->setLayout(idCardLayout);
        m_pIDCardEdit->setContextMenuPolicy(Qt::NoContextMenu);
    
        m_pCardEdit->setContextMenuPolicy(Qt::NoContextMenu);
    }

    m_pNameEdit->setFocusPolicy(Qt::StrongFocus);
    m_pIDCardEdit->setFocusPolicy(Qt::StrongFocus);
    m_pCardEdit->setFocusPolicy(Qt::StrongFocus);

    m_pNameEdit->setAttribute(Qt::WA_AcceptTouchEvents);
    m_pIDCardEdit->setAttribute(Qt::WA_AcceptTouchEvents);
    m_pCardEdit->setAttribute(Qt::WA_AcceptTouchEvents);

    m_pNameEdit->installEventFilter(q_func());
    m_pIDCardEdit->installEventFilter(q_func());
    m_pCardEdit->installEventFilter(q_func());

    // Name field layout
    QHBoxLayout *hlayout1 = new QHBoxLayout;
    hlayout1->addSpacing(30);
    hlayout1->addWidget(new QLabel(QObject::tr(" Name:")));
    hlayout1->addWidget(m_pNameEdit);
    hlayout1->addSpacing(30);
    ((QLabel *)hlayout1->itemAt(1)->widget())->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // Employee ID field layout
    QHBoxLayout *hlayout2 = new QHBoxLayout;
    hlayout2->addSpacing(30);
    hlayout2->addWidget(new QLabel(QObject::tr("EmpID:")));
    hlayout2->addWidget(m_pIDCardEdit);
    hlayout2->addSpacing(30);
    ((QLabel *)hlayout2->itemAt(1)->widget())->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // Card number field layout
    QHBoxLayout *hlayout3 = new QHBoxLayout;
    hlayout3->addSpacing(30);
    hlayout3->addWidget(new QLabel(QObject::tr(" CardNo:")));
    hlayout3->addWidget(m_pCardEdit);
    hlayout3->addSpacing(30);
    ((QLabel *)hlayout3->itemAt(1)->widget())->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // Face capture buttons layout
    QHBoxLayout *hlayout4 = new QHBoxLayout;
    hlayout4->addStretch();
    hlayout4->addWidget(m_pCaptureFaceButton);
    hlayout4->addWidget(m_pRegFaceButton);
    hlayout4->addStretch();
    
    // NEW: Fingerprint buttons layout
    QHBoxLayout *hlayout5 = new QHBoxLayout;
    hlayout5->addStretch();
    hlayout5->addWidget(m_pCaptureFingerButton);
    hlayout5->addWidget(m_pRegFingerButton);
    hlayout5->addWidget(m_pDeleteFingerButton);
    hlayout5->addStretch();

    QFrame *f = new QFrame;
    f->setStyleSheet("QFrame{background-color:rgb(231, 231, 231);}");

    QVBoxLayout *layout = new QVBoxLayout();
    layout->setContentsMargins(0, 5, 0, 20);
    
    // Add input fields
    layout->addLayout(hlayout1);
    layout->addLayout(hlayout2);
    layout->addLayout(hlayout3);
    layout->addSpacing(15);
    
    // Face section
    layout->addLayout(hlayout4);
    layout->addSpacing(20);
    
    // NEW: Fingerprint section
    QLabel *fingerSectionLabel = new QLabel(QObject::tr("━━━━━ Fingerprint ━━━━━"));
    fingerSectionLabel->setAlignment(Qt::AlignCenter);
    fingerSectionLabel->setStyleSheet("QLabel { color: #666; font-size: 14px; }");
    layout->addWidget(fingerSectionLabel);
    
    layout->addWidget(m_pFingerStatusLabel);
    layout->addSpacing(10);
    layout->addLayout(hlayout5);
    
    f->setLayout(layout);

    QVBoxLayout *malayout = new QVBoxLayout(q_func());
    malayout->setMargin(0);
    malayout->addWidget(m_pSettingMenuTitleFrm);
    malayout->addStretch();
    malayout->addWidget(f);
}

void AddUserFrmPrivate::InitData()
{
    q_func()->setObjectName("AddUserFrm");
    
    // Face buttons (existing)
    m_pCaptureFaceButton->setFixedSize(220, 62);
    m_pRegFaceButton->setFixedSize(220, 62);
    m_pCaptureFaceButton->setText(QObject::tr("AcquisitionFace"));
    m_pRegFaceButton->setText(QObject::tr("RegisterFace"));
    m_pRegFaceButton->setEnabled(false);
    
    // NEW: Fingerprint buttons
    m_pCaptureFingerButton->setFixedSize(220, 62);
    m_pRegFingerButton->setFixedSize(220, 62);
    m_pCaptureFingerButton->setText(QObject::tr("Capture Finger"));
    m_pRegFingerButton->setText(QObject::tr("Register Finger"));
    m_pRegFingerButton->setEnabled(false);
    
    // NEW: Fingerprint status label
    m_pFingerStatusLabel->setAlignment(Qt::AlignCenter);
    m_pFingerStatusLabel->setStyleSheet("QLabel { color: #888; font-size: 13px; }");
    m_pFingerStatusLabel->setText(QObject::tr("No fingerprint captured"));

    // Input field placeholders (existing)
    m_pNameEdit->setPlaceholderText(QObject::tr("Required"));
    m_pIDCardEdit->setPlaceholderText(QObject::tr("Enter the employeeID as per SammyApp"));
    m_pCardEdit->setPlaceholderText(QObject::tr("Optional"));
    m_pSettingMenuTitleFrm->setTitleText(QObject::tr("AddPerson"));
}


void AddUserFrmPrivate::InitConnect()
{
    // Face connections (existing)
    QObject::connect(m_pCaptureFaceButton, &QPushButton::clicked, 
                    q_func(), &AddUserFrm::slotCaptureFaceButton);
    QObject::connect(m_pRegFaceButton, &QPushButton::clicked, 
                    q_func(), &AddUserFrm::slotRegFaceButton);
    
    // NEW: Fingerprint connections
    QObject::connect(m_pCaptureFingerButton, &QPushButton::clicked, 
                    q_func(), &AddUserFrm::slotCaptureFingerButton);
    QObject::connect(m_pRegFingerButton, &QPushButton::clicked, 
                    q_func(), &AddUserFrm::slotRegFingerButton);
    QObject::connect(m_pDeleteFingerButton, &QPushButton::clicked, 
                q_func(), &AddUserFrm::slotDeleteFingerButton);

    // Other connections (existing)
    QObject::connect(m_pSettingMenuTitleFrm, &SettingMenuTitleFrm::sigReturnSuperiorClicked, 
                    q_func(), &AddUserFrm::slotReturnSuperiorClicked);
    QObject::connect(m_pNameEdit, &QLineEdit::textChanged, 
                    q_func(), &AddUserFrm::slotTextChanged);
    QObject::connect(UserViewFrm::GetInstance(), &UserViewFrm::sigPersonInfo,  
                    q_func(), &AddUserFrm::slotPersonInfo);
}

void AddUserFrm::slotModifyUser()
{
    Q_D(AddUserFrm);
    mModify++;
    if (mModify<=1 && mPerson.name!=nullptr)
    {        
        d->m_pSettingMenuTitleFrm->setTitleText(QObject::tr("ModifyPerson"));	
        d->m_pNameEdit->setText(mPerson.name);
        d->m_pIDCardEdit->setText(mPerson.idcard);
        d->m_pCardEdit->setText(mPerson.iccard);//å¡å·
        
        // Disable editing for name, ID card, sex, and card number in modify mode
        d->m_pNameEdit->setReadOnly(true);
        d->m_pIDCardEdit->setReadOnly(true);
        d->m_pCardEdit->setReadOnly(false);
        //d->m_psexBox->setEnabled(false);
        
        // Optional: Change the style to make it clear they're not editable
        QString readOnlyStyle = "QLineEdit { background-color: #F0F0F0; color: #707070; }";
        d->m_pNameEdit->setStyleSheet(readOnlyStyle);
        d->m_pIDCardEdit->setStyleSheet(readOnlyStyle);
        d->m_pCardEdit->setStyleSheet(readOnlyStyle);
    }
}
void AddUserFrm::modifyRecord(QString aName, QString aIDCard, QString aCardNo, QString asex, QString apersonid, QString auuid, int apersonalModuleId)
{
    Q_D(AddUserFrm);

    d->m_pSettingMenuTitleFrm->setTitleText(QObject::tr("ModifyPerson"));
    d->m_pNameEdit->setText(aName);
    d->m_pIDCardEdit->setText(aIDCard);
    d->m_pCardEdit->setText(aCardNo);
    
    // Disable editing in modify mode
    d->m_pNameEdit->setReadOnly(true);
    d->m_pIDCardEdit->setReadOnly(true);
    d->m_pCardEdit->setReadOnly(true);
    
    QString readOnlyStyle = "QLineEdit { background-color: #F0F0F0; color: #707070; }";
    d->m_pNameEdit->setStyleSheet(readOnlyStyle);
    d->m_pIDCardEdit->setStyleSheet(readOnlyStyle);
    d->m_pCardEdit->setStyleSheet(readOnlyStyle);

    mPerson.name = aName;
    mPerson.sex = asex;
    mPerson.idcard = aIDCard;
    mPerson.iccard = aCardNo;
    mPerson.personid = apersonid.toInt();
    mPerson.uuid = auuid;
    mPerson.personalModuleId = apersonalModuleId;  // store server-assigned ID for MQTT publish
    
    printf("[modifyRecord] Loaded user '%s' (empId='%s', personalModuleId=%d)\n",
           aName.toStdString().c_str(),
           aIDCard.toStdString().c_str(),
           apersonalModuleId);
    fflush(stdout);
}

void AddUserFrm::slotCaptureFaceButton()
{
    Q_D(AddUserFrm);
    int faceNum = 0;
    double threshold = 0;
    int ret = -1;
    QString path;
    
    // Clean up previous captures
    system("rm -f /mnt/user/facedb/RegImage.jpg");
    system("rm -f /mnt/user/facedb/RegImage.jpeg");
    
    // Get captured image path
    path = ((BaiduFaceManager *)qXLApp->GetAlgoFaceManager())->getCurFaceImgPath();
    if (path.length() == 0) {
        QMessageBox::information(this, QObject::tr("Tips"), QObject::tr("Please activate the algorithm first!"));
        return;
    }
    
    printf("%s,%s,%d path=%s\n", __FILE__, __func__, __LINE__, path.toStdString().c_str());
    
    // Load and display image with proper memory management
    QImage img;
    {
        QImageReader reader(path);
        if (!reader.canRead()) {
            d->mHintText = QObject::tr("ErrorDataAcquisiting");
            this->update();
            return;
        }
        img = reader.read();
        if (img.isNull()) {
            d->mHintText = QObject::tr("ErrorDataAcquisiting");
            this->update();
            return;
        }
    }
    
    mPath = path;
    mDraw = true;
    
    // Handle rotation efficiently
    QSettings sysIniFile("/param/RV1109_PARAM.txt", QSettings::IniFormat);
    int rotation = sysIniFile.value("PLATFORM_ROTATION", 0).toInt();
    if (rotation > 0) {
        QTransform transform;
        transform.rotate(rotation);
        img = img.transformed(transform, Qt::SmoothTransformation);
    }
    
    // Update UI
    d->m_pCameraPicFrm->setShowImage(img);
    d->m_pCameraPicFrm->move(this->width() - 175, this->height() - 625);
    d->m_pCameraPicFrm->update();
    d->m_pCameraPicFrm->show();
    
    // Clear previous face feature to free memory
    d->mFaceFeature.clear();
    
    // Extract face features
    ret = ((BaiduFaceManager *)qXLApp->GetAlgoFaceManager())->RegistPerson(path, faceNum, threshold, d->mFaceFeature);
    
    // Set hint text based on result
    if (ret == -1) {
        d->mHintText = QObject::tr("ErrorDataAcquisiting");
    }
    else if (ret == 0 && (faceNum == 0)) {
        d->mHintText = QObject::tr("NotFindFace");
    }
    else if (ret == 0 && (faceNum != 1)) {
        d->mHintText = QObject::tr("MultipleFaces");
    }
    else if (ret == 0 && (threshold <= 0.7)) {
        d->mHintText = QString::number(threshold, 'f', 2) + QString(" ") + QObject::tr("LowFaceQuality");
    }
    else if (ret == 0 && faceNum == 1) {
        d->mHintText = QObject::tr("FaceDataAcquisitOk");
        
        // MODIFIED: Only save cropped image here, don't do heavy processing
        QString employeeId = d->m_pIDCardEdit->text().trimmed();
        if (!employeeId.isEmpty()) {
            // Use RegisteredFacesDB to handle image saving (lightweight operation)
            RegisteredFacesDB* faceDB = RegisteredFacesDB::GetInstance();
            bool imageSaved = faceDB->extractAndSaveFaceImage(employeeId, path);
            
            if (imageSaved) {
                printf("SUCCESS: Face image saved for employee: %s\n", employeeId.toStdString().c_str());
                
                // Load the cropped face image for MQTT publishing
                QString croppedFacePath = QString("/mnt/user/reg_face_image/%1.jpg").arg(employeeId);
                d->mCapturedFaceImage = QImage(croppedFacePath);
                
                if (!d->mCapturedFaceImage.isNull()) {
                    printf("INFO: Cropped face image loaded for MQTT: %s\n", croppedFacePath.toStdString().c_str());
                } else {
                    printf("WARNING: Failed to load cropped face image: %s\n", croppedFacePath.toStdString().c_str());
                }
            } else {
                printf("WARNING: Failed to save face image for employee: %s\n", employeeId.toStdString().c_str());
            }
        }
    } 
    else {
        d->mHintText.clear();
    }
    
    // Clean up temporary file
    QFile fileTemp(path);
    fileTemp.remove();
    
    // Enable/disable registration button
    bool canRegister = (d->mHintText == QObject::tr("FaceDataAcquisitOk") && 
                       !d->m_pNameEdit->text().isEmpty() && 
                       !d->m_pIDCardEdit->text().isEmpty());
    
    d->m_pRegFaceButton->setEnabled(canRegister);
    
#ifdef SCREENCAPTURE
    if (canRegister) {
        grab().save(QString("/mnt/user/screenshot/000%1.png").arg(this->metaObject()->className()), "png");
    }
#endif
    
    this->update();
}


void AddUserFrm::slotRegFaceButton() {
    Q_D(AddUserFrm);
    
    // CRITICAL: Disable button immediately to prevent double-clicks
    d->m_pRegFaceButton->setEnabled(false);
    
    // Show processing indicator
    QApplication::setOverrideCursor(Qt::WaitCursor);
    
    // Process events to update UI
    QApplication::processEvents();
    
    try {
        // Get user input
        QString employeeId = d->m_pIDCardEdit->text().trimmed();
        QString userName = d->m_pNameEdit->text().trimmed();
        QString icCard = d->m_pCardEdit->text().trimmed();
        QString userSex = "Unknown";
        QString department = "";
        QString timeOfAccess = "";
        
        // Basic validation
        if (employeeId.isEmpty() || userName.isEmpty()) {
            QApplication::restoreOverrideCursor();
            QMessageBox::warning(this, QObject::tr("Warning"), QObject::tr("Please fill in required fields!"));
            d->m_pRegFaceButton->setEnabled(true);
            return;
        }
        
        printf("=== FACE REGISTRATION START ===\n");
        printf("Employee ID: '%s', Name: '%s'\n", employeeId.toStdString().c_str(), userName.toStdString().c_str());
        
        // Determine operation type
        bool isModifyingPerson = (d->m_pSettingMenuTitleFrm->getTitleText() == QObject::tr("ModifyPerson"));
        bool localRegistrationSuccess = false;
        
        // CRITICAL: Perform duplicate checks BEFORE heavy operations
        if (!isModifyingPerson) {
            // Quick duplicate checks using lightweight queries
            if (checkForDuplicateUser(employeeId, icCard)) {
                QApplication::restoreOverrideCursor();
                d->m_pRegFaceButton->setEnabled(true);
                return; // Error messages already shown
            }
        }
        
        // Face similarity check (lightweight - only if we have features)
        if (!d->mFaceFeature.isEmpty()) {
            QString duplicateInfo = checkFaceDuplication(isModifyingPerson);
            if (!duplicateInfo.isEmpty()) {
                QApplication::restoreOverrideCursor();
                QMessageBox::warning(this, QObject::tr("Duplicate Face Detected"), duplicateInfo);
                d->m_pRegFaceButton->setEnabled(true);
                return;
            }
        }
        
        // LOCAL PROCESSING ONLY: Use existing RegisteredFacesDB functions (NO SERVER CALLS)
        RegisteredFacesDB* faceDB = RegisteredFacesDB::GetInstance();
        
        if (isModifyingPerson) {
            printf("INFO: Updating existing person: %s\n", mPerson.uuid.toStdString().c_str());
            // MODIFY EXISTING USER: Update face feature only (LOCAL)
            localRegistrationSuccess = faceDB->UpdatePersonFaceFeature(mPerson.uuid, d->mFaceFeature);
        } else {
            // NEW USER REGISTRATION: Store locally only
            printf("INFO: Registering new person: %s\n", userName.toStdString().c_str());
            // Step 1: Store user data without face (creates user in DB and RAM)
            QString uuid = QUuid::createUuid().toString();
            localRegistrationSuccess = faceDB->StoreUserWithoutFaceData(
                uuid, userName, employeeId, icCard, userSex, department, timeOfAccess,
                "", "", "", "" // attendanceMode, tenantId, id, status (empty for now)
            );
            
            if (localRegistrationSuccess && !d->mFaceFeature.isEmpty()) {
                // Step 2: Update with face feature (LOCAL)
                bool faceUpdateSuccess = faceDB->UpdatePersonFaceFeature(uuid, d->mFaceFeature);
                if (!faceUpdateSuccess) {
                    printf("WARNING: User created but face feature update failed\n");
                    // Keep localRegistrationSuccess = true, user is still created
                }
            }
        }
        
        // Restore cursor immediately after LOCAL processing
        QApplication::restoreOverrideCursor();
        
        // SHOW DIALOG IMMEDIATELY based on local registration result
        if (localRegistrationSuccess) {
            printf("SUCCESS: Local registration completed for %s\n", userName.toStdString().c_str());
            
            // Show success message immediately
            OperationTipsFrm dlg(this);
            if (isModifyingPerson) {
                dlg.setMessageBox(QObject::tr("Tips"), QObject::tr("Face updated successfully for 「%1」!!!").arg(mPerson.name));
            } else {
                dlg.setMessageBox(QObject::tr("Tips"), QObject::tr("Register「%1」Person Success!!!").arg(userName));
            }
            dlg.exec(); // Show dialog immediately
            
            // Clear form for new registrations
            if (!isModifyingPerson) {
                clearFormAndResetUI();
            }
            
            // Re-enable button immediately after showing dialog
            d->m_pRegFaceButton->setEnabled(true);
            
            // BACKGROUND SERVER SYNC: Start timer for server synchronization
            QTimer::singleShot(100, [this, faceDB, employeeId, userName, icCard, userSex, department, timeOfAccess, d]() {
                // This runs asynchronously in background
                printf("INFO: Starting background server sync for %s\n", userName.toStdString().c_str());
                
                bool serverSyncSuccess = faceDB->sendCroppedImageToServer(
                    employeeId, userName, icCard, userSex, department, timeOfAccess, d->mFaceFeature
                );
                
                if (serverSyncSuccess) {
                    printf("SUCCESS: Server sync completed for %s\n", userName.toStdString().c_str());
                } else {
                    printf("WARNING: Server sync failed for %s (local registration still valid)\n", userName.toStdString().c_str());
                    // Could optionally show a subtle notification or queue for retry
                }
                
            // ========== MQTT FACE DATA PUBLISHING ==========
printf("\n");
printf("================================================================================\n");
printf("MQTT FACE PUBLISHING: Starting for employee %s\n", employeeId.toStdString().c_str());
printf("================================================================================\n");

// STEP 1: Check if we have the captured face image
printf("[STEP 1] Checking captured face image...\n");
if (d->mCapturedFaceImage.isNull()) {
    printf("ERROR: Captured face image is NULL!\n");
    printf("ERROR: d->mCapturedFaceImage.isNull() = true\n");
    printf("ERROR: Face data CANNOT be published\n");
} else {
    printf("SUCCESS: Face image is available\n");
    printf("  - Image size: %dx%d pixels\n", d->mCapturedFaceImage.width(), d->mCapturedFaceImage.height());
    printf("  - Image format: %d\n", d->mCapturedFaceImage.format());
    
    // STEP 2: Get MQTT manager instance
    printf("\n[STEP 2] Getting MQTT manager instance...\n");
    MqttHeartbeatManager* mqttManager = MqttHeartbeatManager::GetInstance();
    
    if (!mqttManager) {
        printf("ERROR: MQTT manager is NULL!\n");
        printf("ERROR: MqttHeartbeatManager::GetInstance() returned NULL\n");
    } else {
        printf("SUCCESS: MQTT manager obtained\n");
        
        // STEP 3: Get tenant ID
        printf("\n[STEP 3] Getting tenant ID from config...\n");
        QString tenantId = mqttManager->getTenantIdFromConfig();
        
        printf("  - Tenant ID: '%s'\n", tenantId.toStdString().c_str());
        
        // STEP 4: Get assignedTo — use personalModuleId stored in mPerson (set by modifyRecord)
        printf("\n[STEP 4] Getting assignedTo (personalModuleId)...\n");
        int assignedTo = mPerson.personalModuleId;  // directly from server-synced data
        printf("  - mPerson.personalModuleId = %d\n", assignedTo);
        if (assignedTo == 0) {
            // Fallback: look up in RAM by idcard for users enrolled directly on device
            assignedTo = mqttManager->getAssignedToForUser(employeeId);
            printf("  - Fallback getAssignedToForUser('%s') = %d\n",
                   employeeId.toStdString().c_str(), assignedTo);
        }
        
        // STEP 5: Convert image to base64
        printf("\n[STEP 5] Converting image to base64...\n");
        QByteArray byteArray;
        QBuffer buffer(&byteArray);
        buffer.open(QIODevice::WriteOnly);
        
        bool saveSuccess = d->mCapturedFaceImage.save(&buffer, "JPEG", 85);
        
        if (!saveSuccess) {
            printf("ERROR: Failed to save image to buffer!\n");
            printf("ERROR: QImage::save() returned false\n");
        } else {
            printf("SUCCESS: Image saved to buffer\n");
            printf("  - JPEG size: %d bytes\n", byteArray.size());
            
            QString base64Data = QString::fromLatin1(byteArray.toBase64());
            printf("SUCCESS: Image converted to base64\n");
            printf("  - Base64 length: %d characters\n", base64Data.length());
            printf("  - Base64 preview (first 60 chars): %.60s...\n", 
                   base64Data.toStdString().c_str());
            
            // STEP 6: Publish to MQTT
            printf("\n[STEP 6] Publishing to MQTT broker...\n");
            printf("  - Calling publishFaceData()...\n");
            
            bool mqttSuccess = mqttManager->publishFaceData(
                base64Data,   // Base64 string
                tenantId,     // From heartbeat (may be empty)
                employeeId,   // Employee ID
                assignedTo    // User ID
            );
            
            printf("\n[STEP 6 RESULT] publishFaceData() returned: %s\n", 
                   mqttSuccess ? "TRUE (SUCCESS)" : "FALSE (FAILED)");
            
            if (mqttSuccess) {
                printf("\n");
                printf("========================================================================\n");
                printf("SUCCESS: ✓✓✓ Face data published to MQTT for employee %s\n", 
                       employeeId.toStdString().c_str());
                printf("========================================================================\n");
            } else {
                printf("\n");
                printf("========================================================================\n");
                printf("ERROR: ✗✗✗ Failed to publish face data to MQTT for %s\n", 
                       employeeId.toStdString().c_str());
                printf("ERROR: Check publishFaceData() function logs above for details\n");
                printf("========================================================================\n");
            }
        }
    }
}

printf("\n");
printf("================================================================================\n");
printf("MQTT FACE PUBLISHING: Complete\n");
printf("================================================================================\n");
fflush(stdout);
// ===============================================
            });
            
        } else {
            printf("ERROR: Local registration failed for %s\n", userName.toStdString().c_str());
            
            // Show error message
            OperationTipsFrm dlg(this);
            if (isModifyingPerson) {
                dlg.setMessageBox(QObject::tr("Tips"), QObject::tr("Failed to update face for 「%1」!!!").arg(mPerson.name));
            } else {
                dlg.setMessageBox(QObject::tr("Tips"), QObject::tr("Register「%1」Person Fail!!!").arg(userName));
            }
            dlg.exec();
            
            // Re-enable button on failure
            d->m_pRegFaceButton->setEnabled(true);
        }
        
    } catch (const std::exception& e) {
        printf("EXCEPTION in slotRegFaceButton: %s\n", e.what());
        QApplication::restoreOverrideCursor();
        d->m_pRegFaceButton->setEnabled(true);
        
        OperationTipsFrm dlg(this);
        dlg.setMessageBox(QObject::tr("Error"), QObject::tr("Registration failed due to system error!"));
        dlg.exec();
        
    } catch (...) {
        printf("UNKNOWN EXCEPTION in slotRegFaceButton\n");
        QApplication::restoreOverrideCursor();
        d->m_pRegFaceButton->setEnabled(true);
        
        OperationTipsFrm dlg(this);
        dlg.setMessageBox(QObject::tr("Error"), QObject::tr("Registration failed due to unknown error!"));
        dlg.exec();
    }
    
    // Clear hint text and update UI
    d->mHintText.clear();
    d->m_pCameraPicFrm->setShowImage(QImage());
    d->m_pCameraPicFrm->hide();
    
    // Force garbage collection
    QApplication::processEvents();
    printf("=== FACE REGISTRATION END ===\n");
}

// Helper function for duplicate user checks
bool AddUserFrm::checkForDuplicateUser(const QString& employeeId, const QString& icCard)
{
    // ID Card duplicate check
    QSqlQuery checkQuery(QSqlDatabase::database("isc_arcsoft_face"));
    checkQuery.prepare("SELECT personid FROM person WHERE idcardnum = ? LIMIT 1");
    checkQuery.bindValue(0, employeeId);
    
    if (!checkQuery.exec()) {
        QMessageBox::critical(this, QObject::tr("Error"), QObject::tr("Database error occurred!"));
        return true; // Treat as duplicate to prevent registration
    }
    
    if (checkQuery.next()) {
        QMessageBox::warning(this, QObject::tr("Tips"), QObject::tr("ID Card already exists in database!"));
        return true;
    }
    
    // Card number duplicate check (only if not empty or default)
    if (!icCard.isEmpty() && icCard != "000000") {
        QSqlQuery checkCardQuery(QSqlDatabase::database("isc_arcsoft_face"));
        checkCardQuery.prepare("SELECT personid FROM person WHERE iccardnum = ? AND iccardnum != '000000' AND iccardnum != '' LIMIT 1");
        checkCardQuery.bindValue(0, icCard);
        
        if (!checkCardQuery.exec()) {
            QMessageBox::critical(this, QObject::tr("Error"), QObject::tr("Database error occurred!"));
            return true;
        }
        
        if (checkCardQuery.next()) {
            QMessageBox::warning(this, QObject::tr("Tips"), QObject::tr("Card number already exists in database!"));
            return true;
        }
    }
    
    return false; // No duplicates found
}

// Helper function for face duplication check
QString AddUserFrm::checkFaceDuplication(bool isModifyingPerson)
{
    Q_D(AddUserFrm);
    
    if (d->mFaceFeature.isEmpty()) {
        return QString(); // No face feature to check
    }
    
    // OPTIMIZED: Only check first 50 users to prevent memory overload
    QSqlQuery checkFaceQuery(QSqlDatabase::database("isc_arcsoft_face"));
    checkFaceQuery.prepare("SELECT personid, uuid, name, idcardnum, feature FROM person WHERE feature IS NOT NULL AND feature != '' LIMIT 50");
    
    if (!checkFaceQuery.exec()) {
        printf("Face similarity check failed: %s\n", checkFaceQuery.lastError().text().toStdString().c_str());
        return QString(); // Skip check on error
    }
    
    while (checkFaceQuery.next()) {
        QString existingUuid = checkFaceQuery.value("uuid").toString();
        QString existingName = checkFaceQuery.value("name").toString();
        QString existingIdCard = checkFaceQuery.value("idcardnum").toString();
        QByteArray dbFeature = checkFaceQuery.value("feature").toByteArray();
        
        // Skip current person if modifying
        if (isModifyingPerson && existingUuid == mPerson.uuid) {
            continue;
        }
        
        if (!dbFeature.isEmpty()) {
            double similarity = ((BaiduFaceManager *)qXLApp->GetAlgoFaceManager())->getFaceFeatureCompare_baidu(
                (unsigned char*)d->mFaceFeature.data(),
                d->mFaceFeature.size(),
                (unsigned char*)dbFeature.data(),
                dbFeature.size()
            );
            
            if (similarity > 0.95) {
                return QString("Face already exists!\n\nSimilarity: %1%\nMatches with: %2\nID Card: %3")
                    .arg(QString::number(similarity * 100, 'f', 2))
                    .arg(existingName.isEmpty() ? "Unknown" : existingName)
                    .arg(existingIdCard.isEmpty() ? "Not specified" : existingIdCard);
            }
        }
    }
    
    return QString(); // No duplicates found
}

void AddUserFrm::slotCaptureFingerButton()
{
    Q_D(AddUserFrm);
    
    printf("\n==========================================\n");
    printf("FINGERPRINT CAPTURE STARTED\n");
    printf("==========================================\n");
    
    // Validate employee ID first
    QString employeeId = d->m_pIDCardEdit->text().trimmed();
    if (employeeId.isEmpty()) {
        printf("[ERROR] Employee ID is empty!\n");
        QMessageBox::warning(this, QObject::tr("Warning"), 
                           QObject::tr("Please enter Employee ID first!"));
        return;
    }
    
    printf("[DEBUG] Employee ID: %s\n", employeeId.toStdString().c_str());
    
    // Update UI
    d->m_pFingerStatusLabel->setText(QObject::tr("Initializing sensor..."));
    d->m_pFingerStatusLabel->setStyleSheet("QLabel { color: blue; font-size: 13px; }");
    d->m_pCaptureFingerButton->setEnabled(false);
    QApplication::processEvents();
    
    // Get fingerprint manager
    FingerprintManager* fpManager = qXLApp->GetFingerprintManager();
    
    // Initialize sensor if needed
    if (!fpManager->isSensorReady()) {
        printf("[DEBUG] Initializing fingerprint sensor...\n");
        if (!fpManager->initFingerprintSensor()) {
            printf("[ERROR] Sensor initialization FAILED!\n");
            d->m_pFingerStatusLabel->setText(QObject::tr("Sensor initialization failed!"));
            d->m_pFingerStatusLabel->setStyleSheet("QLabel { color: red; font-size: 13px; }");
            d->m_pCaptureFingerButton->setEnabled(true);
            return;
        }
        printf("[SUCCESS] Sensor initialized\n");
    }
    
    uint16_t sensorFingerId = RegisteredFacesDB::getNextAvailableFingerId();
    
    printf("[DEBUG] Generated SEQUENTIAL Sensor Finger ID: %d for Employee: %s\n", 
           sensorFingerId, employeeId.toStdString().c_str());
    
    // Update UI
    d->m_pFingerStatusLabel->setText(QObject::tr("Starting enrollment..."));
    QApplication::processEvents();
    
    printf("\n[ENROLLMENT] Starting enrollment process...\n");
    
    // Clear previous data
    d->mFingerTemplate.clear();
    
    // ✅ STEP 1: Enroll fingerprint to sensor
    bool enrollmentSuccess = fpManager->startEnrollment(sensorFingerId);
    
    if (!enrollmentSuccess) {
        printf("[ERROR] Fingerprint enrollment FAILED!\n");
        d->mFingerHintText = QObject::tr("Failed to enroll fingerprint");
        d->m_pFingerStatusLabel->setText(d->mFingerHintText);
        d->m_pFingerStatusLabel->setStyleSheet("QLabel { color: red; font-size: 13px; }");
        d->m_pCaptureFingerButton->setEnabled(true);
        return;
    }
    
    printf("[SUCCESS] Fingerprint enrolled at sensor ID: %d\n", sensorFingerId);
    
    // ✅ STEP 2: Download template from sensor for backup
    QByteArray templateBackup;
    d->m_pFingerStatusLabel->setText(QObject::tr("Downloading template..."));
    QApplication::processEvents();
    
    bool downloadSuccess = fpManager->downloadFingerprintTemplate(sensorFingerId, templateBackup);
    
    printf("\n[DOWNLOAD] Template download attempt:\n");
    printf("  - Download success: %s\n", downloadSuccess ? "YES" : "NO");
    printf("  - Template size: %d bytes\n", templateBackup.size());
    printf("  - Template isEmpty: %s\n", templateBackup.isEmpty() ? "YES" : "NO");
    
    if (!templateBackup.isEmpty()) {
        printf("  - Template preview (HEX): %s...\n", templateBackup.left(32).toHex().constData());
    }
    fflush(stdout);
    
    // ✅ IMPORTANT: Even if download fails, enrollment succeeded - sensor has the template
    if (!downloadSuccess || templateBackup.isEmpty()) {
        printf("[WARNING] Template download failed, but enrollment succeeded\n");
        printf("[INFO] Sensor has the fingerprint at ID %d\n", sensorFingerId);
        printf("[INFO] Will create data structure with sensor ID only (no backup)\n");
        fflush(stdout);
    } else {
        printf("[SUCCESS] Template downloaded: %d bytes\n", templateBackup.size());
        fflush(stdout);
    }
    
    // ✅ STEP 3: Create combined data structure
    // ALWAYS store sensor ID, even if template download failed
    // Store: [finger_id (2 bytes)][template_size (2 bytes)][template_data (variable)]
    QByteArray fingerprintData;
    QDataStream stream(&fingerprintData, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_9);  // ✅ Set consistent version
    
    stream << sensorFingerId;                         // uint16_t (2 bytes) - ALWAYS present
    stream << (uint16_t)templateBackup.size();        // uint16_t (2 bytes) - Will be 0 if no backup
    
    if (!templateBackup.isEmpty()) {
        fingerprintData.append(templateBackup);       // Append raw HEX data (if available)
    }
    
    d->mFingerTemplate = fingerprintData;
    
    printf("\n[DATA STRUCTURE] Fingerprint data created:\n");
    printf("  - Total size: %d bytes\n", fingerprintData.size());
    printf("  - Header (4 bytes): [ID=%d][Size=%d]\n", sensorFingerId, templateBackup.size());
    printf("  - Template backup: %d bytes\n", templateBackup.size());
    printf("  - First 16 bytes (HEX): %s\n", fingerprintData.left(16).toHex().constData());
    fflush(stdout);
    
    // ✅ Verification: Try to read back the data
    printf("\n[VERIFICATION] Reading back stored data:\n");
    QDataStream verifyStream(d->mFingerTemplate);
    verifyStream.setVersion(QDataStream::Qt_5_9);
    
    uint16_t readId = 0;
    uint16_t readSize = 0;
    verifyStream >> readId >> readSize;
    
    printf("  - Read ID: %d (expected %d) %s\n", readId, sensorFingerId, readId == sensorFingerId ? "✓" : "✗");
    printf("  - Read Size: %d (expected %d) %s\n", readSize, templateBackup.size(), readSize == templateBackup.size() ? "✓" : "✗");
    printf("  - Data size after header: %d bytes\n", d->mFingerTemplate.size() - 4);
    
    if (d->mFingerTemplate.size() > 4) {
        QByteArray extractedTemplate = d->mFingerTemplate.mid(4);
        printf("  - Extracted template size: %d bytes\n", extractedTemplate.size());
        printf("  - Extracted matches original: %s\n", extractedTemplate == templateBackup ? "✓ YES" : "✗ NO");
        
        if (!extractedTemplate.isEmpty()) {
            printf("  - Extracted preview: %s...\n", extractedTemplate.left(32).toHex().constData());
        }
    } else {
        printf("  - No template backup stored (sensor-only mode)\n");
    }
    fflush(stdout);
    
    // ✅ Set success message
    QString statusMsg;
    if (templateBackup.isEmpty()) {
        statusMsg = QString("✓ Enrolled to sensor!\nID: %1\n(No backup template)").arg(sensorFingerId);
        d->mFingerHintText = QObject::tr("Fingerprint enrolled (sensor only)");
    } else {
        statusMsg = QString("✓ Enrolled & Backed up!\nID: %1\n(%2 bytes)").arg(sensorFingerId).arg(templateBackup.size());
        d->mFingerHintText = QObject::tr("Fingerprint enrolled successfully!");
    }
    
    d->m_pFingerStatusLabel->setText(statusMsg);
    d->m_pFingerStatusLabel->setStyleSheet("QLabel { color: green; font-size: 13px; font-weight: bold; }");
    
    // Enable registration button (we can register even without backup)
    bool canRegister = !d->m_pNameEdit->text().isEmpty() && 
                      !d->m_pIDCardEdit->text().isEmpty();
    d->m_pRegFingerButton->setEnabled(canRegister);
    
    d->m_pCaptureFingerButton->setEnabled(true);
    
    printf("==========================================\n");
    printf("FINGERPRINT CAPTURE COMPLETED\n");
    printf("  - Can register: %s\n", canRegister ? "YES" : "NO");
    printf("  - Stored data size: %d bytes\n", d->mFingerTemplate.size());
    printf("  - Has backup template: %s\n", templateBackup.isEmpty() ? "NO (sensor only)" : "YES");
    printf("==========================================\n\n");
    fflush(stdout);
    
    this->update();
}

void AddUserFrm::slotRegFingerButton()
{
    Q_D(AddUserFrm);
    
    printf("\n==========================================\n");
    printf("FINGERPRINT REGISTRATION TO DATABASE\n");
    printf("==========================================\n");
    
    d->m_pRegFingerButton->setEnabled(false);
    
    QString employeeId = d->m_pIDCardEdit->text().trimmed();
    QString userName = d->m_pNameEdit->text().trimmed();
    
    if (employeeId.isEmpty() || userName.isEmpty()) {
        QMessageBox::warning(this, QObject::tr("Warning"), 
                           QObject::tr("Please fill in Name and Employee ID!"));
        d->m_pRegFingerButton->setEnabled(true);
        return;
    }
    
    if (d->mFingerTemplate.isEmpty()) {
        QMessageBox::warning(this, QObject::tr("Warning"), 
                           QObject::tr("Please capture fingerprint first!"));
        d->m_pRegFingerButton->setEnabled(true);
        return;
    }
    
    // ========== DEBUG: Check stored data structure ==========
    printf("\n[DEBUG] Stored fingerprint data analysis:\n");
    printf("  - Total size: %d bytes\n", d->mFingerTemplate.size());
    printf("  - First 16 bytes (HEX): %s\n", d->mFingerTemplate.left(16).toHex().constData());
    fflush(stdout);
    
    if (d->mFingerTemplate.size() < 4) {
        printf("[ERROR] Data too small (only %d bytes) - should be at least 4!\n", d->mFingerTemplate.size());
        QMessageBox::critical(this, QObject::tr("Error"), 
                            QObject::tr("Invalid fingerprint data!\n\nPlease capture again."));
        d->m_pRegFingerButton->setEnabled(true);
        return;
    }
    
    // Extract sensor ID and template size from stored data
    QDataStream readStream(d->mFingerTemplate);
    readStream.setVersion(QDataStream::Qt_5_9);  // Ensure consistent version
    
    uint16_t sensorFingerId = 0;
    uint16_t templateSize = 0;
    readStream >> sensorFingerId >> templateSize;
    
    printf("[DEBUG] Extracted header:\n");
    printf("  - Sensor ID: %d\n", sensorFingerId);
    printf("  - Template size: %d bytes\n", templateSize);
    printf("  - Expected template bytes: %d\n", d->mFingerTemplate.size() - 4);
    fflush(stdout);
    
    // ✅ Extract the HEX template (if available)
    QByteArray hexTemplate;
    
    if (d->mFingerTemplate.size() > 4) {
        hexTemplate = d->mFingerTemplate.mid(4);  // Skip first 4 bytes (ID + size)
        printf("[DEBUG] HEX template extracted:\n");
        printf("  - HEX size: %d bytes\n", hexTemplate.size());
        
        if (!hexTemplate.isEmpty()) {
            printf("  - HEX preview: %s...\n", hexTemplate.left(32).toHex().constData());
        } else {
            printf("  - HEX is empty (sensor-only mode)\n");
        }
    } else {
        printf("[DEBUG] No template backup (data is exactly 4 bytes - header only)\n");
    }
    fflush(stdout);
    
    // ✅ IMPORTANT: We can still register even without template backup!
    // The sensor has the fingerprint at sensorFingerId
    if (hexTemplate.isEmpty()) {
        printf("[INFO] No template backup available\n");
        printf("[INFO] This is OK - sensor has fingerprint at ID %d\n", sensorFingerId);
        printf("[INFO] Will store sensor ID only (no MQTT publish for backup-less enrollment)\n");
        fflush(stdout);
    }
    
    QApplication::setOverrideCursor(Qt::WaitCursor);
    
    RegisteredFacesDB* faceDB = RegisteredFacesDB::GetInstance();
    QString uuid = faceDB->findUuidByEmployeeId(employeeId);
    
    if (uuid.isEmpty()) {
        QApplication::restoreOverrideCursor();
        QMessageBox::critical(this, QObject::tr("Error"), 
                            QObject::tr("User not found! Please register user first."));
        d->m_pRegFingerButton->setEnabled(true);
        return;
    }
    
    // Store complete fingerprint data (ID + template backup if available)
    bool dbSuccess = faceDB->UpdatePersonFingerprint(uuid, d->mFingerTemplate);
    
    // Also update finger_id column separately
    bool idSuccess = faceDB->UpdatePersonFingerId(uuid, sensorFingerId);
    
    QApplication::restoreOverrideCursor();
    
    if (dbSuccess && idSuccess) {
        printf("[SUCCESS] Fingerprint registered - Employee: %s, UUID: %s, Sensor ID: %d\n", 
               userName.toStdString().c_str(), uuid.toStdString().c_str(), sensorFingerId);
        
        d->m_pFingerStatusLabel->setText(QObject::tr("✓ Registered!\nSensor ID: %1").arg(sensorFingerId));
        d->m_pFingerStatusLabel->setStyleSheet("QLabel { color: green; font-size: 13px; font-weight: bold; }");
        
        // ========== AUTO-PUBLISH FINGERPRINT DATA VIA MQTT ==========
        // ONLY publish if we have template backup. We do this BEFORE the dialog because dlg.exec() blocks the thread!
        if (!hexTemplate.isEmpty()) {
            printf("\n");
            printf("================================================================================\n");
            printf("MQTT FINGERPRINT PUBLISHING: Starting for employee %s\n", employeeId.toStdString().c_str());
            printf("================================================================================\n");
            
            // Get MQTT manager instance
            printf("\n[STEP 2] Getting MQTT manager instance...\n");
            MqttHeartbeatManager* mqttManager = MqttHeartbeatManager::GetInstance();
            
            if (mqttManager) {
                // Get tenant ID
                printf("\n[STEP 3] Getting tenant ID from config...\n");
                QString tenantId = mqttManager->getTenantIdFromConfig();
                printf("  - Tenant ID: '%s'\n", tenantId.toStdString().c_str());
                fflush(stdout);
                
                // Get assignedTo — use personalModuleId from mPerson (set by modifyRecord)
                printf("\n[STEP 4] Getting assignedTo (personalModuleId)...\n");
                int assignedTo = mPerson.personalModuleId;  // directly from server-synced data
                printf("  - mPerson.personalModuleId = %d\n", assignedTo);
                if (assignedTo == 0) {
                    // Fallback: look up in RAM by idcard for device-enrolled users
                    assignedTo = mqttManager->getAssignedToForUser(employeeId);
                    printf("  - Fallback getAssignedToForUser('%s') = %d\n",
                           employeeId.toStdString().c_str(), assignedTo);
                }
                fflush(stdout);
                
                // Publish to MQTT (HEX will be converted to Base64 internally)
                printf("\n[STEP 5] Publishing fingerprint to MQTT...\n");
                printf("  - HEX input size: %d bytes\n", hexTemplate.size());
                fflush(stdout);
                
                bool mqttSuccess = mqttManager->publishFingerprintDataFromHex(
                    hexTemplate,     // HEX template (will be converted to Base64)
                    tenantId,        // From heartbeat
                    employeeId,      // Employee ID
                    sensorFingerId,  // Finger ID from sensor
                    assignedTo       // User ID
                );
                
                printf("\n[STEP 5 RESULT] publishFingerprintDataFromHex() returned: %s\n", 
                       mqttSuccess ? "TRUE (SUCCESS)" : "FALSE (FAILED)");
                fflush(stdout);
                
                if (mqttSuccess) {
                    printf("\n");
                    printf("========================================================================\n");
                    printf("SUCCESS: ✓✓✓ Fingerprint data published to MQTT for employee %s\n", 
                           employeeId.toStdString().c_str());
                    printf("========================================================================\n");
                } else {
                    printf("\n");
                    printf("========================================================================\n");
                    printf("ERROR: ✗✗✗ Failed to publish fingerprint data to MQTT for %s\n", 
                           employeeId.toStdString().c_str());
                    printf("========================================================================\n");
                }
                fflush(stdout);
            }
            
            printf("\n");
            printf("================================================================================\n");
            printf("MQTT FINGERPRINT PUBLISHING: Complete\n");
            printf("================================================================================\n");
            fflush(stdout);
        } else {
            printf("\n");
            printf("================================================================================\n");
            printf("MQTT FINGERPRINT PUBLISHING: Skipped (no template backup)\n");
            printf("================================================================================\n");
            printf("INFO: Fingerprint is enrolled at sensor ID %d\n", sensorFingerId);
            printf("INFO: No backup template available for MQTT publishing\n");
            printf("INFO: User can still authenticate using the sensor\n");
            printf("================================================================================\n");
            fflush(stdout);
        }
        
        // NOW show the dialog!
        OperationTipsFrm dlg(this);
        dlg.setMessageBox(QObject::tr("Success"), 
                        QObject::tr("Fingerprint registered for 「%1」!\n\nSensor ID: %2")
                        .arg(userName).arg(sensorFingerId));
        dlg.exec();
        // ============================================================
        
        d->mFingerTemplate.clear();
        d->mFingerHintText.clear();
        
    } else {
        printf("[ERROR] Database storage FAILED!\n");
        fflush(stdout);
        
        QMessageBox::critical(this, QObject::tr("Error"), 
                            QObject::tr("Failed to save to database!"));
    }
    
    d->m_pRegFingerButton->setEnabled(!d->mFingerTemplate.isEmpty());
    
    printf("==========================================\n\n");
    fflush(stdout);
    
    this->update();
}

// ============================================================================
// STEP 7: MODIFY clearFormAndResetUI() - RESET FINGERPRINT UI
// ============================================================================

void AddUserFrm::clearFormAndResetUI()
{
    Q_D(AddUserFrm);
    
    // Clear existing fields
    d->m_pNameEdit->clear();
    d->m_pIDCardEdit->clear();
    d->m_pCardEdit->clear();
    d->mFaceFeature.clear();
    d->mCapturedFaceImage = QImage();  // Clear captured face image
    d->m_pRegFaceButton->setEnabled(false);
    d->mHintText.clear();
    
    // NEW: Clear fingerprint data
    d->mFingerTemplate.clear();
    d->m_pRegFingerButton->setEnabled(false);
    d->mFingerHintText.clear();
    d->m_pFingerStatusLabel->setText(QObject::tr("No fingerprint captured"));
    d->m_pFingerStatusLabel->setStyleSheet("QLabel { color: #888; font-size: 13px; }");
    
    this->update();
}

// Fixed slotReturnSuperiorClicked function
void AddUserFrm::slotReturnSuperiorClicked()
{
    Q_D(AddUserFrm);
    d->m_pCameraPicFrm->setShowImage(QImage());
    d->m_pCameraPicFrm->hide();
    
    if (d->m_pSettingMenuTitleFrm->getTitleText()==QObject::tr("ModifyPerson"))
    {
        d->m_pSettingMenuTitleFrm->setTitleText(QObject::tr("AddPerson"));
        
        // Re-enable editing for all fields when going back to Add Person mode
        d->m_pNameEdit->setReadOnly(false);
        d->m_pIDCardEdit->setReadOnly(false);
        d->m_pCardEdit->setReadOnly(false);
        
        // Reset the style
        d->m_pNameEdit->setStyleSheet("");
        d->m_pIDCardEdit->setStyleSheet("");
        d->m_pCardEdit->setStyleSheet("");
    }

    emit sigShowFaceHomeFrm();

    UserViewFrm::GetInstance()->setModifyFlag(0);    
    
    d->m_pNameEdit->clear();
    d->m_pIDCardEdit->clear();
    d->m_pCardEdit->clear();
    
    mPerson = {0};
    mModify= 0;
}

void AddUserFrm::slotTextChanged(const QString &text)
{
    Q_D(AddUserFrm);
    if(text.isEmpty() || d->m_pCameraPicFrm->getImgisNull())
    {
        d->m_pRegFaceButton->setEnabled(false);
    }else if(!text.isEmpty() && !d->m_pCameraPicFrm->getImgisNull() &&  d->mHintText == QObject::tr("FaceDataAcquisitOk"))//äººè„¸é‡‡é›†OK!!!
    {
        d->m_pRegFaceButton->setEnabled(true);
    }
}

void AddUserFrm::slotPersonInfo(const QString &aName ,const QString &asex ,const QString &aCard,const QString &cardNo)
{
    Q_D(AddUserFrm);

    mPerson.name = aName;
    mPerson.sex = asex; 
    mPerson.idcard= aCard;
    mPerson.iccard= cardNo;
}

void AddUserFrm::slotDeleteFingerButton()
{
    Q_D(AddUserFrm);  // <-- ADD THIS LINE
    
    QString employeeId = d->m_pIDCardEdit->text().trimmed();
    if (employeeId.isEmpty()) {
        QMessageBox::warning(this, tr("Warning"), tr("Please enter Employee ID"));
        return;
    }
    
    // Get user from database
    RegisteredFacesDB* faceDB = RegisteredFacesDB::GetInstance();
    QString uuid = faceDB->findUuidByEmployeeId(employeeId);
    
    if (uuid.isEmpty()) {
        QMessageBox::warning(this, tr("Warning"), tr("User not found"));
        return;
    }
    
    // Get sensor finger ID from database
    uint16_t sensorFingerId = faceDB->getFingerId(uuid);
    
    if (sensorFingerId == 0) {
        QMessageBox::information(this, tr("Info"), tr("No fingerprint enrolled for this user"));
        return;
    }
    
    // Confirm deletion
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, tr("Confirm"), 
                                  tr("Delete fingerprint for %1?\n\nSensor ID: %2")
                                  .arg(d->m_pNameEdit->text()).arg(sensorFingerId),
                                  QMessageBox::Yes | QMessageBox::No);
    
    if (reply != QMessageBox::Yes) {
        return;
    }
    
    // Delete from sensor
    FingerprintManager* fpManager = qXLApp->GetFingerprintManager();
    
    if (fpManager->deleteFingerprintTemplate(sensorFingerId)) {
        // Delete from database
        bool dbSuccess = faceDB->clearFingerprint(uuid);
        
        if (dbSuccess) {
            QMessageBox::information(this, tr("Success"), 
                                    tr("Fingerprint deleted successfully"));
            
            d->m_pFingerStatusLabel->setText(tr("No fingerprint enrolled"));
            d->m_pDeleteFingerButton->setEnabled(false);
        } else {
            QMessageBox::warning(this, tr("Warning"), 
                                tr("Deleted from sensor but database update failed"));
        }
    } else {
        QMessageBox::critical(this, tr("Error"), 
                             tr("Failed to delete fingerprint from sensor"));
    }
}

void AddUserFrm::hideEvent(QHideEvent *event)
{
    Q_D(AddUserFrm);
    d->mHintText.clear();
    d->m_pRegFaceButton->setEnabled(false);
    QWidget::hideEvent(event);
}

void AddUserFrm::paintEvent(QPaintEvent *event)
{
    Q_D(AddUserFrm);
    QFont fnt = this->font();
    fnt.setPixelSize(30);
    QFontMetrics fm(fnt);
    int fmw = fm.width(d->mHintText);

    QPainter painter(this);
    painter.setPen(Qt::red);
    painter.setFont(fnt);
    painter.drawText((this->width()/2) - (fmw/2), 180, d->mHintText);
    QWidget::paintEvent(event);

    emit sigModifyUser(); 

#ifdef SCREENCAPTURE  //ScreenCapture 
    printf(">>>>%s,%s,%d,mPath=%s, mDraw=%d\n",__FILE__,__func__,__LINE__, mPath.toStdString().c_str(), mDraw); 
    if (!mPath.isEmpty() && mDraw)
    {
        mDraw = false;
        Q_UNUSED(event);  
        QPainter painter2(this);  
        QPixmap pix;
        pix.load(mPath);
        painter2.drawPixmap(0,0,800,1280,pix);//0,0,600,800 0,0,800,1280
    printf(">>>>%s,%s,%d\n",__FILE__,__func__,__LINE__);        
        grab().save(QString("/mnt/user/screenshot/painterAddUserFrm.png"),"png"); 
    }
#endif     
}

bool AddUserFrm::eventFilter(QObject *obj, QEvent *event)
{
    QLineEdit *lineEdit = qobject_cast<QLineEdit*>(obj);
    if (lineEdit) {
        if (event->type() == QEvent::MouseButtonPress) {
            // Get the parent frame
            QFrame *formFrame = findChild<QFrame*>();
            
            // Move form up and show keyboard immediately
            if (!formFrame->property("isMovedUp").toBool()) {
                // Calculate new position
                QRect startGeometry = formFrame->geometry();
                int keyboardHeight = 600;
                
                QPoint globalPos = lineEdit->mapToGlobal(lineEdit->rect().bottomLeft());
                int screenHeight = QApplication::primaryScreen()->geometry().height();
                int offset = 0;
                
                if (globalPos.y() > (screenHeight - keyboardHeight - 50)) {
                    offset = globalPos.y() - (screenHeight - keyboardHeight - 50);
                }

                // Create animation
                QPropertyAnimation *animation = new QPropertyAnimation(formFrame, "geometry");
                animation->setDuration(300);
                
                QRect endGeometry = startGeometry;
                endGeometry.moveTop(startGeometry.y() - offset);
                
                animation->setStartValue(startGeometry);
                animation->setEndValue(endGeometry);
                animation->setEasingCurve(QEasingCurve::OutCubic);
                
                // Store original position
                formFrame->setProperty("originalY", startGeometry.y());
                formFrame->setProperty("isMovedUp", true);
                
                // Start animation and immediately show keyboard
                animation->start(QAbstractAnimation::DeleteWhenStopped);
                
                // Force focus and show keyboard
                lineEdit->setFocus();
                
                // For embedded Linux, we can try to force show the virtual keyboard
                system("killall -9 keyboard");
                system("/usr/bin/keyboard &");
            }
            
            return true; // Handle the event
        }
        else if (event->type() == QEvent::FocusOut) {
            QFrame *formFrame = findChild<QFrame*>();
            if (formFrame && formFrame->property("isMovedUp").toBool()) {
                // Move form back down
                QPropertyAnimation *animation = new QPropertyAnimation(formFrame, "geometry");
                animation->setDuration(300);
                
                QRect currentGeometry = formFrame->geometry();
                QRect endGeometry = currentGeometry;
                endGeometry.moveTop(formFrame->property("originalY").toInt());
                
                animation->setStartValue(currentGeometry);
                animation->setEndValue(endGeometry);
                animation->setEasingCurve(QEasingCurve::OutCubic);
                animation->start(QAbstractAnimation::DeleteWhenStopped);
                
                formFrame->setProperty("isMovedUp", false);
                
                // Hide keyboard
                system("killall -9 keyboard");
            }
        }
    }
    
    return QWidget::eventFilter(obj, event);
}
#ifdef SCREENCAPTURE  //ScreenCapture
void AddUserFrm::mouseDoubleClickEvent(QMouseEvent* event)
{
    printf(">>>>%s,%s,%d\n",__FILE__,__func__,__LINE__); 
    //grab().save(QString("/mnt/user/screenshot/%1.png").arg(this->metaObject()->className()),"png");    
    mPath = ((BaiduFaceManager *)qXLApp->GetAlgoFaceManager())->getCurFaceImgPath();
    mDraw = true;
    if (!mPath.isEmpty() && mDraw)
    {
       // mDraw = false;
        Q_UNUSED(event);  
        QPainter painter(this);  
        QPixmap pix;
        pix.load(mPath);
        painter.drawPixmap(0,0,800,1280,pix);//0,0,600,800 0,0,800,1280
    printf(">>>>%s,%s,%d\n",__FILE__,__func__,__LINE__);        
        grab().save(QString("/mnt/user/screenshot/painterAddUserFrm.png"),"png"); 
    }    
}	
#endif
