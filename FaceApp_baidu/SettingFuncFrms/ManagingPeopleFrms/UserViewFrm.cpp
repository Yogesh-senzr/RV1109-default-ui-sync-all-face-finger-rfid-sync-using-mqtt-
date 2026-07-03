#include "UserViewFrm.h"
#include "Delegate/wholedelegate.h"
#include "pagenavigator.h"

#ifdef Q_OS_LINUX
#include "USB/UsbObserver.h"
#endif

#include "UserViewModel.h"
#include "DB/RegisteredFacesDB.h"
#include "OperationTipsFrm.h"
#include "Threads/ParsePersonXlsx.h"
#include "Application/FaceApp.h"

#include "Helper/myhelper.h"
#include "AddUserFrm.h"
#include "FaceHomeFrms/FaceHomeFrm.h"
#include "FaceMainFrm.h"
#include "HttpServer/ConnHttpServerThread.h"

#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QMenu>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QDateTime>
#include <QSqlDriver>
#include <QDebug>
#include <QElapsedTimer>
#include <QtConcurrent/QtConcurrent>
#include <QListWidget>
#include <QStandardItemModel>
#include <QProgressDialog>
#include <QMessageBox>
#include <QSqlError>
#include <QAction>
#include <QStackedWidget>
#include <QFrame>
#include <QGraphicsEffect> // Add this for QGraphicsDropShadowEffect

#define PAGE (16) // Changed from 15 to 10 users per page
#define DateTime QDateTime::currentDateTime().toString("yyyy-MM-dd")

// Structure to hold user data
struct UserData {
    QString name;
    QString sex;
    QString iccardnum;
    QString idcardnum;
    QString createtime;
    QString personid;
    QString uuid;
    bool hasFaceData;
    int fingerId;        // -1 = no fingerprint
    int personalModuleId; // server-assigned module ID (assignedTo for MQTT publish)
};

class UserViewFrmPrivate
{
    Q_DECLARE_PUBLIC(UserViewFrm)
public:
    UserViewFrmPrivate(UserViewFrm *dd);
private:
    void InitUI();
    void InitData();
    void InitConnect();
    enum FilterOption {
        All,
        WithFace,
        NoFace
    };
private:
    void AddUserToList(const struct UserData &userData);
    void SelectData(const QString &sql);
    void ApplyCurrentFilter();
    void LoadFaceDataInBackground();   
private:
    void ClearListData();
    void ShowUserDetails(int index);
    void CreateDetailView();
private:
    class FaceHomeFrm *m_pFaceHomeFrm;
private:
    class QStackedWidget *m_pStackedWidget;     
private:
    class QLineEdit *m_pInputDataEdit;
    class QPushButton *m_pQueryButton;
    class QPushButton *m_pExportButton;
    class QPushButton *m_pFilterButton;
    class QMenu *m_pFilterMenu;
    class QAction *m_pFilterAllAction;
    class QAction *m_pFilterFaceAction;
    class QAction *m_pFilterNoFaceAction;
    FilterOption m_currentFilterOption;
    class QProgressDialog *m_pProgressDialog; 
private:
    class QListWidget *m_pUserListWidget;
    class QWidget *m_pDetailView;
    class QLabel *m_pDetailNameLabel;
    class QLabel *m_pDetailSexLabel;
    class QLabel *m_pDetailCardNoLabel;
    class QLabel *m_pDetailIdCardLabel;
    class QLabel *m_pDetailCreateTimeLabel;
    class QLabel *m_pDetailPersonIdLabel;
    class QLabel *m_pDetailUuidLabel;
    class QLabel *m_pDetailFaceDataLabel;
    class QLabel *m_pDetailFingerIdLabel;
    class QPushButton *m_pAddFaceButton;
    class QPushButton *m_pEditFaceButton; 
    class QPushButton *m_pBackButton;
    class QPushButton *m_pRefreshButton; 

private:
    QList<struct UserData> m_userDataList;
    QList<struct UserData> m_filteredUserDataList;
    int m_currentDetailIndex;
private:
    class PageNavigator *m_pPageNavigator;
    class QLabel *m_pUserCountLabel; // Added for total user count display
private:
    UserViewFrm *const q_ptr;
};

UserViewFrmPrivate::UserViewFrmPrivate(UserViewFrm *dd)
    : q_ptr(dd)
{
    this->InitUI();
    this->InitData();
    this->InitConnect();
}

UserViewFrm::UserViewFrm(QWidget *parent)
    : SettingBaseFrm(parent)
    , d_ptr(new UserViewFrmPrivate(this))
{

}

UserViewFrm::~UserViewFrm()
{

}


void UserViewFrmPrivate::InitUI()
{
    m_pInputDataEdit = new QLineEdit; // 信息录入框
    m_pQueryButton = new QPushButton; // 查询
    m_pExportButton = new QPushButton; // 导出
    
    // Add filter dropdown button
    m_pFilterButton = new QPushButton; // 过滤
    m_pFilterMenu = new QMenu(m_pFilterButton);
    
    // Create filter options with smaller font
    QFont filterFont;
    filterFont.setPointSize(7); // Font size set to 7 as requested

    m_pFilterAllAction = m_pFilterMenu->addAction(QObject::tr("All Data"));
    m_pFilterAllAction->setFont(filterFont);

    m_pFilterFaceAction = m_pFilterMenu->addAction(QObject::tr("Face Data"));
    m_pFilterFaceAction->setFont(filterFont);

    m_pFilterNoFaceAction = m_pFilterMenu->addAction(QObject::tr("No Face"));
    m_pFilterNoFaceAction->setFont(filterFont);

    // Set stylesheet for filter menu actions
    m_pFilterMenu->setStyleSheet(
        "QMenu { background-color: white; border: 1px solid #c0c0c0; }"
        "QMenu::item { padding: 5px 20px; font-size: 7pt; }" // Font size 7pt
        "QMenu::item:selected { background-color: #2196F3; color: white; }" // Hover effect
        "QMenu::item[userData='selected'] { background-color: #4CAF50; color: white; font-weight: bold; }" // Selected action
    );

    // Add shadow effect to filter menu
    QGraphicsDropShadowEffect *shadowEffect = new QGraphicsDropShadowEffect(m_pFilterMenu);
    shadowEffect->setBlurRadius(10);
    shadowEffect->setColor(QColor(0, 0, 0, 160));
    shadowEffect->setOffset(2, 2);
    m_pFilterMenu->setGraphicsEffect(shadowEffect);

    // Explicitly set the menu for the filter button
    m_pFilterButton->setMenu(m_pFilterMenu);
    
    m_pPageNavigator = new PageNavigator;
    
    // Create list widget instead of table
    m_pUserListWidget = new QListWidget;
    m_pUserListWidget->setFrameShape(QFrame::NoFrame);
    m_pUserListWidget->setStyleSheet("QListWidget { background-color: white; border-radius: 5px; }"
                                    "QListWidget::item { border-bottom: 1px solid #c0c0c0; padding: 0px; margin: 0px; }"
                                    "QListWidget::item:selected { background-color: #e0e0e0; color: black; }");
    m_pUserListWidget->setSpacing(0); // Remove spacing between items
    m_pUserListWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // Disable vertical scrollbar
    
    // Create detail view
    m_pDetailView = new QWidget;
    CreateDetailView();
    
    // Create stacked widget to switch between list and details
    m_pStackedWidget = new QStackedWidget;
    m_pStackedWidget->addWidget(m_pUserListWidget);
    m_pStackedWidget->addWidget(m_pDetailView);
    
    m_pInputDataEdit->setContextMenuPolicy(Qt::NoContextMenu);

    m_pUserCountLabel = new QLabel(QObject::tr("Total Users: 0"));
    m_pUserCountLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    QFont countFont = m_pUserCountLabel->font();
    countFont.setPointSize(10);
    m_pUserCountLabel->setFont(countFont);

    QVBoxLayout *vlayout = new QVBoxLayout(q_func());
    vlayout->setSpacing(0);
    vlayout->setContentsMargins(20, 0, 20, 10);

    QHBoxLayout *layout = new QHBoxLayout;
    layout->setSpacing(10);
    layout->addWidget(m_pInputDataEdit);
    layout->addWidget(m_pQueryButton);
    layout->addWidget(m_pFilterButton);
    layout->addWidget(m_pExportButton);

    vlayout->addLayout(layout);
    vlayout->addSpacing(5);
    vlayout->addWidget(m_pStackedWidget, 1); // Set stretch factor to make it fill available space
    vlayout->addSpacing(5);
    vlayout->addWidget(m_pUserCountLabel);
    vlayout->addWidget(m_pPageNavigator);
}


void UserViewFrmPrivate::CreateDetailView()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(m_pDetailView);
    
    // Header with title and refresh button
    QHBoxLayout *headerLayout = new QHBoxLayout;
    QLabel *titleLabel = new QLabel(QObject::tr("User Details"));
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(16);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    
    // Add refresh button in the right corner
    m_pRefreshButton = new QPushButton;
    m_pRefreshButton->setIcon(QIcon(":/Images/refresh.png"));
    m_pRefreshButton->setFixedSize(32, 32);
    m_pRefreshButton->setStyleSheet("border: none; background: transparent;");
    m_pRefreshButton->setToolTip(QObject::tr("Refresh User Data"));
    
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(m_pRefreshButton);
    
    // Back button below title (left aligned)
    QHBoxLayout *backButtonLayout = new QHBoxLayout;
    m_pBackButton = new QPushButton(QObject::tr("Back"));
    m_pBackButton->setFixedWidth(100);
    backButtonLayout->addWidget(m_pBackButton);
    backButtonLayout->addStretch();
    
    // Create detail fields - switched to 1-column layout
    QGridLayout *detailsLayout = new QGridLayout;
    detailsLayout->setVerticalSpacing(10);
    
    // Create labels for user information
    QLabel *nameTitle = new QLabel(QObject::tr("Name:"));
    QLabel *sexTitle = new QLabel(QObject::tr("Sex:"));
    QLabel *cardNoTitle = new QLabel(QObject::tr("Card No:"));
    QLabel *idCardTitle = new QLabel(QObject::tr("ID Card:"));
    QLabel *createTimeTitle = new QLabel(QObject::tr("Created:"));
    QLabel *personIdTitle = new QLabel(QObject::tr("Person ID:"));
    QLabel *uuidTitle = new QLabel(QObject::tr("UUID:"));
    QLabel *faceDataTitle = new QLabel(QObject::tr("Face Data:"));
    QLabel *fingerIdTitle = new QLabel(QObject::tr("Finger ID:"));
    // Make titles bold
    QString titleStyle = "font-weight: bold;";
    nameTitle->setStyleSheet(titleStyle);
    sexTitle->setStyleSheet(titleStyle);
    cardNoTitle->setStyleSheet(titleStyle);
    idCardTitle->setStyleSheet(titleStyle);
    createTimeTitle->setStyleSheet(titleStyle);
    personIdTitle->setStyleSheet(titleStyle);
    uuidTitle->setStyleSheet(titleStyle);
    faceDataTitle->setStyleSheet(titleStyle);
    fingerIdTitle->setStyleSheet(titleStyle);
    
    // Create value labels
    m_pDetailNameLabel = new QLabel();
    m_pDetailSexLabel = new QLabel();
    m_pDetailCardNoLabel = new QLabel();
    m_pDetailIdCardLabel = new QLabel();
    m_pDetailCreateTimeLabel = new QLabel();
    m_pDetailPersonIdLabel = new QLabel();
    m_pDetailUuidLabel = new QLabel();
    m_pDetailFaceDataLabel = new QLabel();
    m_pDetailFingerIdLabel = new QLabel();
    // Add to grid layout - now in a single column
    detailsLayout->addWidget(nameTitle, 0, 0);
    detailsLayout->addWidget(m_pDetailNameLabel, 0, 1);
    
    detailsLayout->addWidget(sexTitle, 1, 0);
    detailsLayout->addWidget(m_pDetailSexLabel, 1, 1);
    
    detailsLayout->addWidget(cardNoTitle, 2, 0);
    detailsLayout->addWidget(m_pDetailCardNoLabel, 2, 1);
    
    detailsLayout->addWidget(idCardTitle, 3, 0);
    detailsLayout->addWidget(m_pDetailIdCardLabel, 3, 1);
    
    detailsLayout->addWidget(createTimeTitle, 4, 0);
    detailsLayout->addWidget(m_pDetailCreateTimeLabel, 4, 1);
    
    detailsLayout->addWidget(personIdTitle, 5, 0);
    detailsLayout->addWidget(m_pDetailPersonIdLabel, 5, 1);
    
    detailsLayout->addWidget(uuidTitle, 6, 0);
    detailsLayout->addWidget(m_pDetailUuidLabel, 6, 1);
    
    detailsLayout->addWidget(faceDataTitle, 7, 0);
    detailsLayout->addWidget(m_pDetailFaceDataLabel, 7, 1);

    detailsLayout->addWidget(fingerIdTitle, 8, 0);
    detailsLayout->addWidget(m_pDetailFingerIdLabel, 8, 1);
    
    // Set column stretch to make value column wider
    detailsLayout->setColumnStretch(1, 1);
    
    // Create a horizontal layout for buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout;
    
    // Add Face button
    m_pAddFaceButton = new QPushButton(QObject::tr("Add Face"));
    m_pAddFaceButton->setFixedHeight(40);
    m_pAddFaceButton->setStyleSheet("background-color:rgb(33, 243, 51); color: white; border-radius: 4px;");
    
    // Edit Face button (replaces delete button)
    m_pEditFaceButton = new QPushButton(QObject::tr("Edit Face"));
    m_pEditFaceButton->setFixedHeight(40);
    m_pEditFaceButton->setStyleSheet("background-color: #FF9800; color: white; border-radius: 4px;");
    
    // Add buttons to layout
    buttonLayout->addWidget(m_pAddFaceButton);
    buttonLayout->addWidget(m_pEditFaceButton);
    
    // Add layouts to main layout
    mainLayout->addLayout(headerLayout);
    mainLayout->addLayout(backButtonLayout); // Changed from addWidget to addLayout
    mainLayout->addSpacing(10);
    mainLayout->addLayout(detailsLayout);
    mainLayout->addSpacing(20);
    mainLayout->addLayout(buttonLayout);
    mainLayout->addStretch();
}


void UserViewFrmPrivate::InitData()
{
    m_pQueryButton->setText(QObject::tr("Search")); // 查询
    m_pExportButton->setText(QObject::tr("Export")); // 导出
    m_pFilterButton->setText(QObject::tr("Filter")); // 过滤 (static text)

    // Reduce the size of the input data box
    m_pInputDataEdit->setFixedWidth(200);  // Reduced width
    m_pInputDataEdit->setPlaceholderText(QObject::tr("Name/CardNo/IdCard")); // 姓名/卡号/身份证
    
    m_pQueryButton->setFixedSize(80, 45);
    m_pExportButton->setFixedSize(80, 45);
    m_pFilterButton->setFixedSize(110, 50);
    
    // Set default filter option to All
    m_currentFilterOption = FilterOption::All;
    m_pFilterAllAction->setData("selected"); // Mark as selected
}

// Fixed version of InitConnect function
void UserViewFrmPrivate::InitConnect()
{
    QObject::connect(m_pQueryButton, &QPushButton::clicked, q_func(), &UserViewFrm::slotQueryButton);
    QObject::connect(m_pPageNavigator, &PageNavigator::currentPageChanged, q_func(), &UserViewFrm::slotCurrentPageChanged);
    QObject::connect(m_pExportButton, &QPushButton::clicked, q_func(), &UserViewFrm::slotExportButton);
    QObject::connect(m_pUserListWidget, &QListWidget::itemClicked, 
                    [this](QListWidgetItem *item) {
                        q_func()->slotUserItemClicked(m_pUserListWidget->row(item));
                    });
    QObject::connect(m_pBackButton, &QPushButton::clicked, q_func(), &UserViewFrm::slotBackToListView);
    QObject::connect(m_pAddFaceButton, &QPushButton::clicked, q_func(), &UserViewFrm::slotAddFaceButton);
    QObject::connect(m_pEditFaceButton, &QPushButton::clicked, q_func(), &UserViewFrm::slotEditFaceButton); // Changed from delete to edit face
    
    // Connect filter actions
    QObject::connect(m_pFilterAllAction, SIGNAL(triggered()), 
                    q_func(), SLOT(slotFilterActionTriggered()));
    QObject::connect(m_pFilterFaceAction, SIGNAL(triggered()), 
                    q_func(), SLOT(slotFilterActionTriggered()));
    QObject::connect(m_pFilterNoFaceAction, SIGNAL(triggered()), 
                    q_func(), SLOT(slotFilterActionTriggered()));
    QObject::connect(m_pRefreshButton, &QPushButton::clicked, q_func(), &UserViewFrm::slotRefreshUserData);
}

void UserViewFrm::slotRefreshUserData()
{
    Q_D(UserViewFrm);
    
    if (d->m_currentDetailIndex < 0 || d->m_currentDetailIndex >= d->m_userDataList.size())
        return;
    
    const UserData &userData = d->m_userDataList.at(d->m_currentDetailIndex);
    QString employeeId = userData.idcardnum;
    
    qDebug() << "DEBUG: slotRefreshUserData - Refreshing user:" << userData.name << "ID:" << employeeId;
    
    // Disable refresh button during sync
    d->m_pRefreshButton->setEnabled(false);
    d->m_pRefreshButton->setToolTip(QObject::tr("Syncing..."));
    
    // Show visual feedback
    d->m_pDetailNameLabel->setText(userData.name + " (Syncing...)");
    
    // Get the ConnHttpServerThread instance directly - this is the correct way
    ConnHttpServerThread* syncThread = ConnHttpServerThread::GetInstance();
    if (syncThread) {
        // Connect to completion signal (one-time connection)
        connect(syncThread, &ConnHttpServerThread::userSyncCompleted,
                this, &UserViewFrm::onUserSyncCompleted,
                Qt::UniqueConnection);
        
        // Trigger individual user sync
        syncThread->syncIndividualUser(employeeId);
    } else {
        qDebug() << "ERROR: Could not get ConnHttpServerThread instance";
        // Re-enable button on error
        d->m_pRefreshButton->setEnabled(true);
        d->m_pRefreshButton->setToolTip(QObject::tr("Refresh User Data"));
        d->m_pDetailNameLabel->setText(userData.name);
    }
}

void UserViewFrm::onUserSyncCompleted(const QString& employeeId, bool success)
{
    Q_D(UserViewFrm);
    
    qDebug() << "DEBUG: onUserSyncCompleted - EmployeeId:" << employeeId << "Success:" << success;
    
    // Re-enable refresh button
    d->m_pRefreshButton->setEnabled(true);
    d->m_pRefreshButton->setToolTip(QObject::tr("Refresh User Data"));
    
    if (success) {
        // Refresh the user details from database
        refreshCurrentUserDetails();
        
        // Show brief success message
        QTimer::singleShot(1000, this, [this]() {
            OperationTipsFrm dlg(this);
            dlg.setMessageBox(QObject::tr("Success"), 
                              QObject::tr("User data refreshed successfully"), 
                              QObject::tr("Ok"), QString(), 1);
            dlg.exec();
        });
    } else {
        // Show error feedback
        OperationTipsFrm dlg(this);
        dlg.setMessageBox(QObject::tr("Error"), 
                          QObject::tr("Failed to refresh user data"), 
                          QObject::tr("Ok"), QString(), 1);
        dlg.exec();
        
        // Restore original name on failure
        if (d->m_currentDetailIndex >= 0 && d->m_currentDetailIndex < d->m_userDataList.size()) {
            const UserData &userData = d->m_userDataList.at(d->m_currentDetailIndex);
            d->m_pDetailNameLabel->setText(userData.name);
        }
    }
}

void UserViewFrm::refreshCurrentUserDetails()
{
    Q_D(UserViewFrm);
    
    if (d->m_currentDetailIndex < 0 || d->m_currentDetailIndex >= d->m_userDataList.size())
        return;
    
    const UserData &currentUser = d->m_userDataList.at(d->m_currentDetailIndex);
    QString uuid = currentUser.uuid;
    
    qDebug() << "DEBUG: refreshCurrentUserDetails - Refreshing UUID:" << uuid;
    
    // Query fresh data from database
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    query.prepare("SELECT * FROM person WHERE uuid = ?");
    query.bindValue(0, uuid);
    
    if (query.exec() && query.next()) {
        // Update the user data in our list
        UserData updatedData;
        updatedData.name = query.value("name").toString();
        updatedData.sex = query.value("sex").toString();
        updatedData.iccardnum = query.value("iccardnum").toString();
        updatedData.idcardnum = query.value("idcardnum").toString();
        updatedData.createtime = query.value("createtime").toString();
        updatedData.personid = query.value("personid").toString();
        updatedData.uuid = query.value("uuid").toString();
        
        // Check if user has face data
        QList<PERSONS_t> persons = RegisteredFacesDB::GetInstance()->GetPersonDataByPersonUUIDFromRAM(uuid);
        updatedData.hasFaceData = (persons.size() > 0 && !persons[0].feature.isEmpty());
        
        // Update the data in our lists
        d->m_userDataList[d->m_currentDetailIndex] = updatedData;
        
        // Update the UI
        d->ShowUserDetails(d->m_currentDetailIndex);
        
        qDebug() << "DEBUG: refreshCurrentUserDetails - Updated user details for:" << updatedData.name;
    } else {
        qDebug() << "ERROR: refreshCurrentUserDetails - Failed to query updated user data";
    }
}

// New implementation for Edit Face button
void UserViewFrm::slotEditFaceButton()
{
    Q_D(UserViewFrm);
    
    if (d->m_currentDetailIndex < 0 || d->m_currentDetailIndex >= d->m_userDataList.size())
        return;
    
    const UserData &userData = d->m_userDataList.at(d->m_currentDetailIndex);
    
    // Check if user has face data to edit
    if (!userData.hasFaceData) {
        OperationTipsFrm dlg(this);
        dlg.setMessageBox(QObject::tr("Warning"), 
                          QObject::tr("No face data found for this user. Please add face data first."), 
                          QObject::tr("Ok"), QString(), 1);
        dlg.exec();
        return;
    }
    
    // Pass the information to the AddUserFrm for face editing
    AddUserFrm::GetInstance()->modifyRecord(
        userData.name, 
        userData.idcardnum, 
        userData.iccardnum, 
        userData.sex, 
        userData.personid, 
        userData.uuid,
        userData.personalModuleId  // pass server-assigned module ID for MQTT publish
    );
    
    // Show the modify person interface for face editing
    emit sigShowFrm(QObject::tr("ModifyPerson"));
}

// Add this new slot implementation to UserViewFrm.cpp
void UserViewFrm::slotFilterActionTriggered()
{
    Q_D(UserViewFrm);
    
    QAction *action = qobject_cast<QAction*>(sender());
    if (!action) return;
    
    // Reset styles for all actions
    d->m_pFilterAllAction->setData(QVariant()); // Clear custom data
    d->m_pFilterFaceAction->setData(QVariant());
    d->m_pFilterNoFaceAction->setData(QVariant());
    
    // Set selected action
    if (action == d->m_pFilterAllAction) {
        d->m_currentFilterOption = UserViewFrmPrivate::FilterOption::All;
        d->m_pFilterAllAction->setData("selected"); // Mark as selected
    } else if (action == d->m_pFilterFaceAction) {
        d->m_currentFilterOption = UserViewFrmPrivate::FilterOption::WithFace;
        d->m_pFilterFaceAction->setData("selected");
    } else if (action == d->m_pFilterNoFaceAction) {
        d->m_currentFilterOption = UserViewFrmPrivate::FilterOption::NoFace;
        d->m_pFilterNoFaceAction->setData("selected");
    } else {
        return; // Unknown action
    }
    
    // Re-run the query to apply the filter
    slotQueryButton();
}

void UserViewFrmPrivate::ApplyCurrentFilter()
{
    QElapsedTimer totalTimer;
    totalTimer.start();
    qDebug() << "=== ApplyCurrentFilter START ===";
    qDebug() << "Current filter option:" << m_currentFilterOption;
    qDebug() << "Total users to filter:" << m_userDataList.size();
    
    // TIMING 1: Clear lists
    QElapsedTimer clearTimer;
    clearTimer.start();
    m_pUserListWidget->clear();
    m_filteredUserDataList.clear();
    qDebug() << "Clear filter lists took:" << clearTimer.elapsed() << "ms";
    
    // TIMING 2: Filter logic
    QElapsedTimer filterLogicTimer;
    filterLogicTimer.start();
    int filteredCount = 0;
    
    for (UserData &userData : m_userDataList) {
        QElapsedTimer singleFilterTimer;
        singleFilterTimer.start();
        
        bool shouldAdd = false;
        
        switch (m_currentFilterOption) {
            case FilterOption::All:
                shouldAdd = true;
                break;
            case FilterOption::WithFace:
            case FilterOption::NoFace:
                // This is where face data checking would happen if needed
                QList<PERSONS_t> persons = RegisteredFacesDB::GetInstance()->GetPersonDataByPersonUUIDFromRAM(userData.uuid);
                bool hasFaceData = (persons.size() > 0 && !persons[0].feature.isEmpty());
                userData.hasFaceData = hasFaceData;
                shouldAdd = (m_currentFilterOption == FilterOption::WithFace) ? hasFaceData : !hasFaceData;
                
                if (filteredCount < 5) {
                    qDebug() << "Face data check for user" << userData.name << "took:" << singleFilterTimer.elapsed() << "ms";
                }
                break;
        }
        
        if (shouldAdd) {
            m_filteredUserDataList.append(userData);
            filteredCount++;
        }
    }
    
    qDebug() << "Filter logic for" << m_userDataList.size() << "users took:" << filterLogicTimer.elapsed() << "ms";
    qDebug() << "Filtered result count:" << filteredCount;
    
    // TIMING 3: Pagination setup
    QElapsedTimer paginationTimer;
    paginationTimer.start();
    int pageCount = filteredCount <= PAGE ? 1 : (filteredCount / PAGE + (filteredCount % PAGE != 0 ? 1 : 0));
    m_pPageNavigator->setMaxPage(pageCount);
    qDebug() << "Pagination setup took:" << paginationTimer.elapsed() << "ms";
    
    // TIMING 4: Display current page
    QElapsedTimer displayTimer;
    displayTimer.start();
    int currentPage = m_pPageNavigator->getCurrentPage();
    int startIndex = (currentPage - 1) * PAGE;
    int endIndex = qMin(startIndex + PAGE, m_filteredUserDataList.size());
    
    m_pUserListWidget->clear();
    for (int i = startIndex; i < endIndex; ++i) {
        QElapsedTimer addUserTimer;
        addUserTimer.start();
        AddUserToList(m_filteredUserDataList.at(i));
        
        if (i < 5) {
            qDebug() << "AddUserToList for item" << i << "took:" << addUserTimer.elapsed() << "ms";
        }
    }
    qDebug() << "Display" << (endIndex - startIndex) << "users took:" << displayTimer.elapsed() << "ms";
    
    // TIMING 5: Switch to list widget
    QElapsedTimer switchTimer;
    switchTimer.start();
    m_pStackedWidget->setCurrentWidget(m_pUserListWidget);
    qDebug() << "Switch to list widget took:" << switchTimer.elapsed() << "ms";
    
    qDebug() << "TOTAL ApplyCurrentFilter time:" << totalTimer.elapsed() << "ms";
    qDebug() << "=== ApplyCurrentFilter END ===";
}

void UserViewFrmPrivate::AddUserToList(const UserData &userData)
{
    QElapsedTimer timer;
    timer.start();
    
    // Create a widget for the list item
    QWidget *itemWidget = new QWidget;
    QHBoxLayout *layout = new QHBoxLayout(itemWidget);
    
    // Create user info label with larger font and better clarity
    QLabel *userLabel = new QLabel(QString("%1 - %2").arg(userData.name).arg(userData.idcardnum));
    QFont font = userLabel->font();
    font.setPointSize(14);
    font.setBold(true);
    userLabel->setFont(font);
    
    // Create info icon button
    QPushButton *infoButton = new QPushButton;
    infoButton->setIcon(QIcon(":/Images/ListWidgetTmpRight.png"));
    infoButton->setFixedSize(32, 32);
    infoButton->setStyleSheet("border: none; background: transparent;");
    
    // Add widgets to layout
    layout->addWidget(userLabel);
    layout->addStretch();
    layout->addWidget(infoButton);
    layout->setContentsMargins(15, 5, 15, 5);
    
    // Create list item
    QListWidgetItem *item = new QListWidgetItem(m_pUserListWidget);
    item->setSizeHint(QSize(0, 65));
    
    // Set the widget as the item widget
    m_pUserListWidget->setItemWidget(item, itemWidget);
    
    qDebug() << "AddUserToList for" << userData.name << "took:" << timer.elapsed() << "ms";
}

void UserViewFrmPrivate::SelectData(const QString &sql)
{
    QElapsedTimer totalTimer;
    totalTimer.start();
    qDebug() << "=== SelectData START ===";
    qDebug() << "SQL Query:" << sql;
    
    // TIMING 1: Database query execution
    QElapsedTimer queryTimer;
    queryTimer.start();
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    query.prepare(sql);
    bool queryResult = query.exec();
    qDebug() << "Database query preparation and execution took:" << queryTimer.elapsed() << "ms";
    qDebug() << "Query execution result:" << queryResult;
    if (!queryResult) {
        qDebug() << "Query error:" << query.lastError().text();
    }

    // TIMING 2: Clear existing data
    QElapsedTimer clearTimer;
    clearTimer.start();
    m_userDataList.clear();
    m_pUserListWidget->clear();
    qDebug() << "Clear data took:" << clearTimer.elapsed() << "ms";

    // TIMING 3: Process query results
    QElapsedTimer processTimer;
    processTimer.start();
    int userCount = 0;
    
    while (query.next())
    {
        QElapsedTimer singleUserTimer;
        singleUserTimer.start();
        
        UserData userData;
        userData.name = query.value("name").toString();
        userData.sex = query.value("Sex").toString();
        userData.iccardnum = query.value("iccardnum").toString();
        userData.idcardnum = query.value("idcardnum").toString();
        userData.createtime = query.value("createtime").toString();
        userData.personid = query.value("personid").toString();
        userData.uuid = query.value("uuid").toString();
        userData.hasFaceData = false; // Skip face data check for now
        userData.fingerId = -1;       // -1 = no fingerprint
        userData.personalModuleId = query.value("personalModuleId").toInt(); // load server-assigned ID
        m_userDataList.append(userData);
        userCount++;
        
        // Log timing for first few users and every 50th user
        if (userCount <= 5 || userCount % 50 == 0) {
            qDebug() << "Processing user" << userCount << "took:" << singleUserTimer.elapsed() << "ms - Name:" << userData.name;
        }
    }
    
    qDebug() << "Processing" << userCount << "users took:" << processTimer.elapsed() << "ms";
    qDebug() << "Average time per user:" << (userCount > 0 ? processTimer.elapsed() / userCount : 0) << "ms";

    // TIMING 4: Apply current filter
    QElapsedTimer filterTimer;
    filterTimer.start();
    ApplyCurrentFilter();
    qDebug() << "ApplyCurrentFilter took:" << filterTimer.elapsed() << "ms";

    // TIMING 5: Update user count label
    QElapsedTimer labelTimer;
    labelTimer.start();
    m_pUserCountLabel->setText(QObject::tr("Total Users: %1").arg(m_filteredUserDataList.size()));
    qDebug() << "Update user count label took:" << labelTimer.elapsed() << "ms";
    
    qDebug() << "TOTAL SelectData time:" << totalTimer.elapsed() << "ms";
    qDebug() << "=== SelectData END ===";
}

void UserViewFrmPrivate::LoadFaceDataInBackground()
{
    // Qt 5.9.4 compatible version without lambda
    for (int i = 0; i < m_userDataList.size(); ++i) {
        QList<PERSONS_t> persons = RegisteredFacesDB::GetInstance()->GetPersonDataByPersonUUIDFromRAM(m_userDataList[i].uuid);
        bool hasFaceData = (persons.size() > 0 && !persons[0].feature.isEmpty());
        
        // Update face data status
        m_userDataList[i].hasFaceData = hasFaceData;
        
        // Update UI every 10 items to avoid blocking
        if (i % 10 == 0) {
            // Qt 5.9.4 compatible way to update UI from background thread
            QTimer::singleShot(0, q_ptr, SLOT(slotUpdateUserList()));
        }
    }
}

void UserViewFrmPrivate::ClearListData()
{
    m_pUserListWidget->clear();
    m_userDataList.clear();
}

void UserViewFrmPrivate::ShowUserDetails(int index)
{
    if (index < 0 || index >= m_userDataList.size())
        return;
    
    m_currentDetailIndex = index;
    UserData &userData = m_userDataList[index]; // Note: non-const reference
    
    // LAZY LOAD: Check face data only when viewing details
    if (!userData.hasFaceData) { // Only check if not already checked
        QList<PERSONS_t> persons = RegisteredFacesDB::GetInstance()->GetPersonDataByPersonUUIDFromRAM(userData.uuid);
        userData.hasFaceData = (persons.size() > 0 && !persons[0].feature.isEmpty());
    }
    
    // Set values to labels
    m_pDetailNameLabel->setText(userData.name);
    m_pDetailSexLabel->setText(userData.sex);
    m_pDetailCardNoLabel->setText(userData.iccardnum);
    m_pDetailIdCardLabel->setText(userData.idcardnum);
    m_pDetailCreateTimeLabel->setText(userData.createtime);
    m_pDetailPersonIdLabel->setText(userData.personid);
    m_pDetailUuidLabel->setText(userData.uuid);
    m_pDetailFaceDataLabel->setText(userData.hasFaceData ? QObject::tr("Yes") : QObject::tr("No"));
QString fingerIdText = QObject::tr("No");
if (userData.fingerId == -1) {
    // Lazy load: only query if not yet loaded
    QList<PERSONS_t> persons = RegisteredFacesDB::GetInstance()->GetPersonDataByPersonUUIDFromRAM(userData.uuid);
    if (persons.size() > 0) {
        userData.fingerId = persons[0].finger_id;
    } else {
        userData.fingerId = 0; // Mark as checked but no data
    }
}

if (userData.fingerId > 0) {
    fingerIdText = QString::number(userData.fingerId);
}
m_pDetailFingerIdLabel->setText(fingerIdText);
    // Enable/disable buttons based on face data
    m_pAddFaceButton->setEnabled(!userData.hasFaceData);
    m_pEditFaceButton->setEnabled(userData.hasFaceData);
    
    if (userData.hasFaceData) {
        m_pAddFaceButton->setStyleSheet("background-color: #9e9e9e; color: white; border-radius: 4px;");
        m_pEditFaceButton->setStyleSheet("background-color: #FF9800; color: white; border-radius: 4px;");
    } else {
        m_pAddFaceButton->setStyleSheet("background-color: #2196F3; color: white; border-radius: 4px;");
        m_pEditFaceButton->setStyleSheet("background-color: #9e9e9e; color: white; border-radius: 4px;");
    }
    
    // Switch to detail view
    m_pStackedWidget->setCurrentWidget(m_pDetailView);
}

static inline QString PackLikeselect(const QString &text)
{
    QString strSql;
    strSql = QString("select * from person where name like '%%1").arg(text);
    strSql.append("%' or ");
    strSql.append(QString("idcardnum like '%%1").arg(text));
    strSql.append("%' or ");
    strSql.append(QString("iccardnum like '%%1").arg(text));
    strSql.append("%'");
    return strSql;
}

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

static inline int SelectDataCount(const QString &sql)
{
    int count = 0;
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));

    query.exec(sql);
    if (query.driver()->hasFeature(QSqlDriver::QuerySize))
    {
        count = query.size();
    }
    else
    {
        count = queryRowCount(query);
    }

    int page = 0;
    if (count <= PAGE)
    {
        page = 1;
    }
    else
    {
        page = count / PAGE;
        if ((count % PAGE) != 0)++page;
    }
    return page;
}

void UserViewFrm::slotUserItemClicked(int row)
{
    Q_D(UserViewFrm);
    
    // Get the current page using the getter method getCurrentPage()
    int currentPage = d->m_pPageNavigator->getCurrentPage();
    
    // Calculate the absolute index in m_filteredUserDataList (not m_userDataList)
    int filteredIndex = (currentPage - 1) * PAGE + row;
    
    // Make sure the index is valid for filtered data
    if (filteredIndex < 0 || filteredIndex >= d->m_filteredUserDataList.size())
        return;
    
    // Get the filtered user data
    const UserData &filteredUser = d->m_filteredUserDataList.at(filteredIndex);
    
    // Find the corresponding index in the original m_userDataList
    int originalIndex = -1;
    for (int i = 0; i < d->m_userDataList.size(); ++i) {
        if (d->m_userDataList.at(i).uuid == filteredUser.uuid) {
            originalIndex = i;
            break;
        }
    }
    
    // Show the details using the original index
    if (originalIndex >= 0) {
        d->ShowUserDetails(originalIndex);
    }
}

void UserViewFrm::slotBackToListView()
{
    Q_D(UserViewFrm);
    d->m_pStackedWidget->setCurrentWidget(d->m_pUserListWidget);
}

void UserViewFrm::slotAddFaceButton()
{
    Q_D(UserViewFrm);
    
    if (d->m_currentDetailIndex < 0 || d->m_currentDetailIndex >= d->m_userDataList.size())
        return;
    
    const UserData &userData = d->m_userDataList.at(d->m_currentDetailIndex);
    
    // Pass the information to the AddUserFrm for modification
    AddUserFrm::GetInstance()->modifyRecord(
        userData.name, 
        userData.idcardnum, 
        userData.iccardnum, 
        userData.sex, 
        userData.personid, 
        userData.uuid,
        userData.personalModuleId  // pass server-assigned module ID for MQTT publish
    );
    
    // Show the modify person interface
    emit sigShowFrm(QObject::tr("ModifyPerson"));
}


void UserViewFrm::slotCurrentPageChanged(const int page)
{
    Q_D(UserViewFrm);
    d->m_pQueryButton->setFocus();
    d->m_pUserListWidget->clear();
    
    // Calculate start and end indices
    int startIndex = (page - 1) * PAGE;
    int endIndex = qMin(startIndex + PAGE, d->m_filteredUserDataList.size());
    
    // Add items for current page
    for (int i = startIndex; i < endIndex; ++i) {
        d->AddUserToList(d->m_filteredUserDataList.at(i));
    }
}

void UserViewFrm::slotQueryButton()
{
    QElapsedTimer totalTimer;
    totalTimer.start();
    qDebug() << "=== slotQueryButton START ===";
    
    Q_D(UserViewFrm);
    
    // TIMING 1: Clear data
    QElapsedTimer clearTimer;
    clearTimer.start();
    d->ClearListData();
    qDebug() << "ClearListData took:" << clearTimer.elapsed() << "ms";

    // TIMING 2: Build SQL query
    QElapsedTimer sqlBuildTimer;
    sqlBuildTimer.start();
    QString text = d->m_pInputDataEdit->text();
    QString selSQL, selSQLCnt;
    selSQLCnt = ::PackLikeselect(text);
    selSQL = selSQLCnt + QString(" order by personid desc");
    qDebug() << "Build SQL took:" << sqlBuildTimer.elapsed() << "ms";
    qDebug() << "Final SQL:" << selSQL;

    // TIMING 3: Execute SelectData
    QElapsedTimer selectDataTimer;
    selectDataTimer.start();
    d->SelectData(selSQL);
    qDebug() << "SelectData took:" << selectDataTimer.elapsed() << "ms";

    // TIMING 4: Switch to list view
    QElapsedTimer switchTimer;
    switchTimer.start();
    d->m_pStackedWidget->setCurrentWidget(d->m_pUserListWidget);
    qDebug() << "Switch to list view took:" << switchTimer.elapsed() << "ms";
    
    qDebug() << "TOTAL slotQueryButton time:" << totalTimer.elapsed() << "ms";
    qDebug() << "=== slotQueryButton END ===";
}

// Add debug timing to setEnter (if this is called when opening the form)
void UserViewFrm::setEnter()
{
    QElapsedTimer totalTimer;
    totalTimer.start();
    qDebug() << "=== setEnter START ===";
    
    Q_D(UserViewFrm);
    d->m_pQueryButton->setFocus();
    static bool Init = false;
    if(!Init)
    {
        Init = true;
        QObject::connect(qXLApp->GetParsePersonXlsx(), &ParsePersonXlsx::sigExportProgressShell, this, &UserViewFrm::slotExportProgressShell);
        QObject::connect(this, &UserViewFrm::sigExportPersons, qXLApp->GetParsePersonXlsx(), &ParsePersonXlsx::slotExportPersons);
    }
    
    // Set the filter to All Data on first entry
    d->m_currentFilterOption = UserViewFrmPrivate::FilterOption::All;
    d->m_pFilterButton->setText(QObject::tr("Filter"));
    d->m_pFilterAllAction->setData("selected");
    d->m_pFilterFaceAction->setData(QVariant());
    d->m_pFilterNoFaceAction->setData(QVariant());
    
    QElapsedTimer queryTimer;
    queryTimer.start();
    this->slotQueryButton();
    qDebug() << "slotQueryButton in setEnter took:" << queryTimer.elapsed() << "ms";
    
    qDebug() << "TOTAL setEnter time:" << totalTimer.elapsed() << "ms";
    qDebug() << "=== setEnter END ===";
}

void UserViewFrm::slotUpdateUserList()
{
    Q_D(UserViewFrm);
    slotQueryButton(); // This will refresh the list with current filter
}

void UserViewFrm::slotExportButton()
{
    Q_D(UserViewFrm);
#ifdef Q_OS_LINUX
    //检查U盘是否插入
    bool usbState = UsbObserver::GetInstance()->isUsbStroagePlugin();
    if(usbState)
    {
        d->m_pExportButton->setEnabled(false);
        d->m_pExportButton->setText(QObject::tr("Export..."));//导出中...
        d->m_pProgressDialog = new QProgressDialog(this);
        QFont font("YSong18030", 12);
        d->m_pProgressDialog->setFont(font);
        d->m_pProgressDialog->setWindowModality(Qt::WindowModal);
        d->m_pProgressDialog->setAttribute(Qt::WA_DeleteOnClose);        
        d->m_pProgressDialog->setMinimumDuration(5);
        d->m_pProgressDialog->setWindowTitle(QObject::tr("PlsWaiting"));//请稍等
        d->m_pProgressDialog->setLabelText(QObject::tr("Export..."));//导出中...
        d->m_pProgressDialog->setCancelButtonText(NULL);        
        emit sigExportPersons();
    }else
    {
        OperationTipsFrm dlg(this);
       // dlg.setMessageBox(QObject::tr("温馨提示"), QObject::tr("请插入U盘"), QObject::tr("确定"), QString(), 1);
        dlg.setMessageBox(QObject::tr("Tips"), QObject::tr("PlsInsertUDisk"), QObject::tr("Ok"), QString(), 1);       
    }
#else
    d->m_pExportButton->setEnabled(false);
    d->m_pExportButton->setText(QObject::tr("Export..."));
    emit sigExportPersons();
#endif
}

void UserViewFrm::slotExportProgressShell(const bool dealstate, const bool savestate, const int total, const int dealcnt)
{
    Q_D(UserViewFrm);
    Q_UNUSED(savestate);
    Q_UNUSED(total);
    Q_UNUSED(dealcnt);
    if(dealstate)
    {
        int icnt =0;
        
        d->m_pProgressDialog->setModal(true);             
        d->m_pProgressDialog->show();        
        if (total<20)
        {            
            icnt =100 / total;
        
            d->m_pProgressDialog->setRange(0, 100);    
            d->m_pProgressDialog->setValue(dealcnt*icnt);
            
            //msleep(500);        
            QTime dieTime = QTime::currentTime().addMSecs(500);//500*1000, 2

            while( QTime::currentTime() < dieTime )
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
                
        } else 
        {
            d->m_pProgressDialog->setRange(0, total);    
            d->m_pProgressDialog->setValue(dealcnt);                        
        }
                
        if (total == dealcnt)
        {                        
            d->m_pExportButton->setEnabled(true);
            d->m_pExportButton->setText(QObject::tr("Export"));           
            d->m_pProgressDialog->close();
        }
    }
}

void UserViewFrm::setModifyFlag(int value)
{  
    Q_D(UserViewFrm);   
    this->slotQueryButton();     
    mModifyFlag = value;

    d->ClearListData();

    QString text = d->m_pInputDataEdit->text();
    QString selSQL, selSQLCnt;
    if (text.isEmpty())
    {
        selSQLCnt ="SELECT *FROM person ";
    }
    else
    {
        selSQLCnt = ::PackLikeselect(text);
    }
    d->m_pPageNavigator->setMaxPage(SelectDataCount(selSQLCnt));
    selSQL = selSQLCnt + QString(" order by personid desc LIMIT 0,%1").arg(PAGE);
    d->SelectData(selSQL);    

    //this->showNormal();
    //this->show();
    this->update();
    this->repaint();    
    this->resize(this->size());
    this->adjustSize();
    QApplication::processEvents(); 
}

#ifdef SCREENCAPTURE  //ScreenCapture 
void UserViewFrm::mouseDoubleClickEvent(QMouseEvent* event)
{
    grab().save(QString("/mnt/user/screenshot/%1.png").arg(this->metaObject()->className()),"png");    
}
#endif