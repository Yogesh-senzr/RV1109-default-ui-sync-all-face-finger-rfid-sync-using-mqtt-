#include "MqttHeartbeatManager.h"
#include <QDebug>
#include <stdio.h>
#include <QString>
#include <QDateTime>
#include <QSqlQuery>
#include <QSqlError>
#include <QRegExp>
#include <algorithm>
#include <QUuid>
#include <curl/curl.h>
#include <json-cpp/json.h>
#include "Helper/myhelper.h"
#include "Version.h"
#include "BaiduFace/BaiduFaceManager.h"
#include "DB/RegisteredFacesDB.h"
#include "Application/FaceApp.h"
#include "Config/ReadConfig.h"
#include "FingerprintManager.h"
#include <QBuffer>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>
#include <ctime>

#define MQTT_BROKER "mqtt.fieldseasy.com"
#define MQTT_PORT 1883
#define MQTT_USERNAME "iot-device"
#define MQTT_PASSWORD "Senzr123"

// ============================================================================
// HEARTBEAT CONFIGURATION
// ============================================================================
#define HEARTBEAT_INTERVAL 30000  // 30 seconds in milliseconds


// Get current time as string
static std::string getTime()
{
    time_t timep;
    time(&timep);
    char tmp[64];
    strftime(tmp, sizeof(tmp), "%Y%m%d%H%M%S", localtime(&timep));
    return tmp;
}

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    if (!contents || !userp) {
        qDebug() << "WriteCallback: NULL pointer received";
        return 0;
    }
    
    size_t realsize = size * nmemb;
    
    // Check for reasonable size (50MB limit instead of 10MB)
    if (realsize > 50 * 1024 * 1024) {
        qDebug() << "WriteCallback: Data too large:" << realsize << "bytes";
        return 0;
    }
    
    try {
        std::string* str = static_cast<std::string*>(userp);
        
        // Check current size
        if (!str) {
            qDebug() << "WriteCallback: Invalid string pointer";
            return 0;
        }
        
        // Check if we can append
        if (str->size() + realsize > 50 * 1024 * 1024) {
            qDebug() << "WriteCallback: Buffer would exceed limit";
            return 0;
        }
        
        // Reserve space to avoid reallocations
        str->reserve(str->size() + realsize + 1024);
        
        // Append data
        str->append(static_cast<char*>(contents), realsize);
        
        return realsize;
        
    } catch (const std::bad_alloc& e) {
        qDebug() << "WriteCallback: Memory allocation failed:" << e.what();
        return 0;
    } catch (const std::exception& e) {
        qDebug() << "WriteCallback: Exception:" << e.what();
        return 0;
    } catch (...) {
        qDebug() << "WriteCallback: Unknown exception";
        return 0;
    }
}

S3SyncWorker::S3SyncWorker(MqttHeartbeatManager* manager)
    : QObject(nullptr)
    , m_manager(manager)
{
}

S3SyncWorker::~S3SyncWorker()
{
}

void S3SyncWorker::doSync()
{
    qDebug() << "S3_WORKER: Starting sync in worker thread";
    
    // Add delay to ensure everything is initialized
    QThread::msleep(15000);  // 15 seconds
    
    bool success = false;
    if (m_manager) {
        try {
        } catch (const std::exception& e) {
            qDebug() << "S3_WORKER: Exception in sync:" << e.what();
            success = false;
        } catch (...) {
            qDebug() << "S3_WORKER: Unknown exception in sync";
            success = false;
        }
    }
    
    qDebug() << "S3_WORKER: Sync finished, success:" << success;
    emit syncFinished(success);
}


// Initialize static members
MqttHeartbeatManager* MqttHeartbeatManager::m_instance = nullptr;
QMutex MqttHeartbeatManager::m_mutex;

// ============================================================================
// S3 SYNCHRONIZATION HELPER FUNCTIONS
// ============================================================================
bool MqttHeartbeatManager::downloadJsonFromUrl(const QString& url, std::string& jsonContent)
{
    qDebug() << "MQTT_DEBUG: Downloading JSON from URL:" << url;
    printf("MQTT_PRINTF: Downloading JSON from URL: %s\n", url.toStdString().c_str());
    fflush(stdout);
    
    jsonContent.clear();
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        qDebug() << "MQTT_DEBUG: Failed to initialize CURL";
        return false;
    }
    
    std::string readBuffer;
    readBuffer.reserve(50 * 1024 * 1024);
    
    curl_easy_setopt(curl, CURLOPT_URL, url.toStdString().c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    
    CURLcode res = curl_easy_perform(curl);
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_easy_cleanup(curl);
    
    qDebug() << "MQTT_DEBUG: HTTP response code:" << http_code << "Size:" << readBuffer.length();
    
    if (res != CURLE_OK || http_code != 200 || readBuffer.empty()) {
        qDebug() << "MQTT_DEBUG: Download failed - CURL:" << curl_easy_strerror(res);
        return false;
    }
    
    jsonContent = readBuffer;
    qDebug() << "MQTT_DEBUG: Successfully downloaded JSON -" << jsonContent.length() << "bytes";
    
    return true;
}


qint64 MqttHeartbeatManager::parseISO8601ToTimestamp(const QString& dateTimeStr)
{
    // Parse ISO 8601 format: "2025-12-19T13:49:19+00:00"
    QDateTime dateTime = QDateTime::fromString(dateTimeStr, Qt::ISODate);
    
    if (!dateTime.isValid()) {
        // Try alternate format without timezone
        dateTime = QDateTime::fromString(dateTimeStr, "yyyy-MM-ddTHH:mm:ss");
    }
    
    if (dateTime.isValid()) {
        return dateTime.toSecsSinceEpoch();
    }
    
    qDebug() << "S3_SYNC: Failed to parse datetime string:" << dateTimeStr;
    return 0;
}


bool MqttHeartbeatManager::updateUserFromS3Data(const QString& employeeId, 
                                                 const QString& name,
                                                 const QString& faceEmbeddingUrl,
                                                 qint64 lastModified,
                                                 int personalModuleId)  // ✅ ADD PARAMETER
{
   qDebug() << "S3_SYNC: Updating user" << employeeId << "with S3 data";
    printf("S3_SYNC: Updating user %s with S3 data\n", employeeId.toStdString().c_str());
    fflush(stdout);

    RegisteredFacesDB* db = RegisteredFacesDB::GetInstance();
    if (!db) {
        qDebug() << "S3_SYNC: Database not available";
        return false;
    }

    // Get current user data
    QList<PERSONS_t> allPersons = db->GetAllPersonFromRAM();
    PERSONS_t targetUser;
    bool userFound = false;

    for (const auto& person : allPersons) {
        if (person.idcard == employeeId) {
            targetUser = person;
            userFound = true;
            break;
        }
    }

    if (!userFound) {
        qDebug() << "S3_SYNC: User" << employeeId << "not found in database";
        return false;
    }

    // Download and process face embedding if URL is provided
    if (!faceEmbeddingUrl.isEmpty()) {
        qDebug() << "S3_SYNC: Downloading face embedding from:" << faceEmbeddingUrl;
        printf("S3_SYNC: Downloading face embedding from: %s\n", faceEmbeddingUrl.toStdString().c_str());
        fflush(stdout);

        // Download face image
        std::string imageData;
        if (downloadFaceImage(faceEmbeddingUrl, imageData)) {
            // Save to temporary file
            QString tempDir = "/tmp";
            QString tempFilePath = tempDir + "/face_" + employeeId + "_" + 
                                  QString::number(QDateTime::currentMSecsSinceEpoch()) + ".jpg";
            
            QFile tempFile(tempFilePath);
            if (tempFile.open(QIODevice::WriteOnly)) {
                tempFile.write(imageData.c_str(), imageData.length());
                tempFile.close();

                qDebug() << "S3_SYNC: Saved face image to:" << tempFilePath;
                printf("S3_SYNC: Saved face image to: %s\n", tempFilePath.toStdString().c_str());
                fflush(stdout);

                // Process face with Baidu algorithm
                if (qXLApp && qXLApp->GetAlgoFaceManager()) {
                        BaiduFaceManager* faceManager = (BaiduFaceManager*)qXLApp->GetAlgoFaceManager();
                    int faceNum = 0;
                    double threshold = 0;
                    QByteArray faceFeature;
                    
                    int result = faceManager->RegistPerson(tempFilePath, faceNum, threshold, faceFeature);
                    
                    if (result == 0 && faceNum > 0) {
                        qDebug() << "S3_SYNC: Successfully processed face image";
                        printf("S3_SYNC: Successfully processed face image\n");
                        fflush(stdout);

                        // Update user with new face feature
                        // Update user with new face feature AND personalModuleId
        int updateResult = db->UpdatePersonToDBAndRAM(
            targetUser.uuid,
            name.isEmpty() ? "" : name.toStdString().c_str(),
            "",  // idCard
            "",  // icCard
            "",  // sex
            "",  // department
            "",  // timeOfAccess
            tempFilePath,  // face image path
            QByteArray(),  // faceEmbedding
            targetUser.attendanceMode,
            targetUser.tenantId,
            targetUser.id,
            targetUser.status,
            personalModuleId  // ✅ PASS IT HERE
        );

                        if (updateResult > 0) {
                            qDebug() << "S3_SYNC: Successfully updated user in database";
                            printf("S3_SYNC: Successfully updated user in database\n");
                            fflush(stdout);
                            
                            // ========== AUTO-SEND FACE DATA VIA MQTT ==========
                            qDebug() << "S3_SYNC: Auto-publishing face data to MQTT for updated user:" << employeeId;
                            
                            // Load the face image and convert to base64
                            QImage faceImage(tempFilePath);
                            if (!faceImage.isNull()) {
                                // Get tenant and assignedTo from config or user data
                                QString tenantId = getTenantIdFromConfig();
                                int assignedTo = getAssignedToForUser(employeeId);
                                
                                // Publish face data to MQTT
                                bool mqttSuccess = publishFaceDataFromImage(faceImage, tenantId, employeeId, assignedTo);
                                
                                if (mqttSuccess) {
                                    qDebug() << "S3_SYNC: âœ“ Face data published to MQTT for:" << employeeId;
                                } else {
                                    qDebug() << "S3_SYNC: âš  Failed to publish face data to MQTT for:" << employeeId;
                                }
                            } else {
                                qDebug() << "S3_SYNC: âš  Cannot publish to MQTT - face image is null";
                            }
                            // ===================================================
                            
                            // Clean up temp file
                            tempFile.remove();
                            return true;
                        }
                    } else {
                        qDebug() << "S3_SYNC: Failed to process face image";
                        printf("S3_SYNC: Failed to process face image\n");
                        fflush(stdout);
                    }
                }
                
                // Clean up temp file
                tempFile.remove();
            }
        }
    } else if ((!name.isEmpty() && targetUser.name != name) || targetUser.personalModuleId != personalModuleId) {
        // Only update name or personalModuleId if face embedding not provided
        int updateResult = db->UpdatePersonToDBAndRAM(
            targetUser.uuid,
            name.toStdString().c_str(),
            "",
            "",
            "",
            "",
            "",
            "",
            QByteArray(),
            targetUser.attendanceMode,
            targetUser.tenantId,
            targetUser.id,
            targetUser.status,
            personalModuleId  // ✅ PASS IT HERE
        );


        if (updateResult > 0) {
            qDebug() << "S3_SYNC: Successfully updated user name in database";
            printf("S3_SYNC: Successfully updated user name in database\n");
            fflush(stdout);
            return true;
        }
    }

    return false;
}


/**
 * Add new user to database with data from S3
 */
bool MqttHeartbeatManager::addNewUserFromS3Data(const QString& empId,
                                                 const QString& name,
                                                 const QString& faceEmbedding,
                                                 qint64 lastModified,
                                                 int personalModuleId)  // ✅ ADD PARAMETER
{
    qDebug() << "S3_SYNC: Adding new user" << empId << "from S3 data";
    printf("S3_SYNC: Adding new user %s from S3 data\n", empId.toStdString().c_str());
    fflush(stdout);

    RegisteredFacesDB* db = RegisteredFacesDB::GetInstance();
    if (!db) {
        qDebug() << "S3_SYNC: Database not available";
        return false;
    }

    // Generate UUID for new user
    QString uuid = QUuid::createUuid().toString();
    uuid = uuid.mid(1, uuid.length() - 2); // Remove curly braces

    // Process face embedding if provided
    QByteArray faceFeature;  // This will hold the processed face feature
    QString tempFilePath;
    
    if (!faceEmbedding.isEmpty()) {
        qDebug() << "S3_SYNC: Processing face embedding for new user:" << empId;
        printf("S3_SYNC: Processing face embedding for new user: %s\n", empId.toStdString().c_str());
        fflush(stdout);

        // Decode base64 face embedding
        QByteArray embeddingBytes = QByteArray::fromBase64(faceEmbedding.toLatin1());
        
        if (!embeddingBytes.isEmpty()) {
            QString tempDir = "/tmp";
            tempFilePath = tempDir + "/face_" + empId + "_" + 
                          QString::number(QDateTime::currentMSecsSinceEpoch()) + ".jpg";
            
            QFile tempFile(tempFilePath);
            if (tempFile.open(QIODevice::WriteOnly)) {
                tempFile.write(embeddingBytes);
                tempFile.close();

                qDebug() << "S3_SYNC: Saved face embedding to:" << tempFilePath;
                printf("S3_SYNC: Saved face embedding to: %s\n", tempFilePath.toStdString().c_str());
                fflush(stdout);

                // Process face with Baidu algorithm to get the face feature
                if (qXLApp && qXLApp->GetAlgoFaceManager()) {
                    BaiduFaceManager* faceManager = (BaiduFaceManager*)qXLApp->GetAlgoFaceManager();
                    
                    int faceNum = 0;
                    double threshold = 0;
                    
                    // This extracts the face feature into faceFeature variable
                    int result = faceManager->RegistPerson(tempFilePath, faceNum, threshold, faceFeature);
                    
                    if (result == 0 && faceNum > 0) {
                        qDebug() << "S3_SYNC: Successfully processed face embedding for new user";
                        qDebug() << "S3_SYNC: Face feature size:" << faceFeature.size() << "bytes";
                        printf("S3_SYNC: Successfully processed face embedding for new user\n");
                        printf("S3_SYNC: Face feature size: %d bytes\n", faceFeature.size());
                        fflush(stdout);
                    } else {
                        qDebug() << "S3_SYNC: Failed to process face embedding for new user, result:" << result;
                        printf("S3_SYNC: Failed to process face embedding for new user, result: %d\n", result);
                        fflush(stdout);
                        // Clear faceFeature so we add user without face
                        faceFeature.clear();
                    }
                } else {
                    qDebug() << "S3_SYNC: qXLApp or face manager not available";
                    printf("S3_SYNC: qXLApp or face manager not available\n");
                    fflush(stdout);
                }
                
                // Clean up temp file
                tempFile.remove();
            } else {
                qDebug() << "S3_SYNC: Failed to save face embedding to temp file";
                printf("S3_SYNC: Failed to save face embedding to temp file\n");
                fflush(stdout);
            }
        }
    }

    // Add user to database with or without face feature
    // RegPersonToDBAndRAM signature: (uuid, name, idCard, icCard, sex, department, timeOfAccess, faceFeature)
    qDebug() << "S3_SYNC: Adding user to database - UUID:" << uuid << "Name:" << name << "empId:" << empId;
    if (!faceFeature.isEmpty()) {
        qDebug() << "S3_SYNC: Adding with face feature (" << faceFeature.size() << "bytes)";
        printf("S3_SYNC: Adding with face feature (%d bytes)\n", faceFeature.size());
    } else {
        qDebug() << "S3_SYNC: Adding without face feature";
        printf("S3_SYNC: Adding without face feature\n");
    }
    fflush(stdout);
    
    int addResult = db->RegPersonToDBAndRAM(
        uuid,
        name,
        empId,
        "",
        "",
        "",
        "",
        faceFeature,
        QByteArray(),
        "jpg",
        "",
        "",
        "",
        "",
        personalModuleId  // ✅ PASS IT HERE
    );

    if (addResult > 0) {
        qDebug() << "S3_SYNC: Successfully added new user to database";
        printf("S3_SYNC: Successfully added new user to database\n");
        fflush(stdout);
        
        // ========== AUTO-SEND FACE DATA VIA MQTT ==========
        // Only send if face feature was successfully processed
        if (!faceFeature.isEmpty() && !tempFilePath.isEmpty()) {
            qDebug() << "S3_SYNC: Auto-publishing face data to MQTT for new user:" << empId;
            
            // Load the face image and convert to base64
            QImage faceImage(tempFilePath);
            if (!faceImage.isNull()) {
                // Get tenant and assignedTo from config or user data
                QString tenantId = getTenantIdFromConfig();
                int assignedTo = getAssignedToForUser(empId);
                
                // Publish face data to MQTT
                bool mqttSuccess = publishFaceDataFromImage(faceImage, tenantId, empId, assignedTo);
                
                if (mqttSuccess) {
                    qDebug() << "S3_SYNC: âœ“ Face data published to MQTT for new user:" << empId;
                } else {
                    qDebug() << "S3_SYNC: âš  Failed to publish face data to MQTT for new user:" << empId;
                }
            } else {
                qDebug() << "S3_SYNC: âš  Cannot publish to MQTT - face image is null";
            }
        } else {
            qDebug() << "S3_SYNC: No face feature to publish to MQTT for:" << empId;
        }
        // ===================================================
        
        return true;
    } else {
        qDebug() << "S3_SYNC: Failed to add new user to database, result:" << addResult;
        printf("S3_SYNC: Failed to add new user to database, result: %d\n", addResult);
        fflush(stdout);
        return false;
    }
}



bool MqttHeartbeatManager::downloadFaceImage(const QString& imageUrl, std::string& imageData)
{
    qDebug() << "S3_SYNC: Downloading face image from:" << imageUrl;
    printf("S3_SYNC: Downloading face image from: %s\n", imageUrl.toStdString().c_str());
    fflush(stdout);

    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if (!curl) {
        qDebug() << "S3_SYNC: Failed to initialize CURL for image download";
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, imageUrl.toStdString().c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        qDebug() << "S3_SYNC: CURL request failed for image:" << curl_easy_strerror(res);
        printf("S3_SYNC: CURL request failed for image: %s\n", curl_easy_strerror(res));
        fflush(stdout);
        curl_easy_cleanup(curl);
        return false;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (http_code != 200) {
        qDebug() << "S3_SYNC: HTTP error code for image:" << http_code;
        printf("S3_SYNC: HTTP error code for image: %ld\n", http_code);
        fflush(stdout);
        return false;
    }

    imageData = readBuffer;
    qDebug() << "S3_SYNC: Successfully downloaded face image, Size:" << imageData.length() << "bytes";
    printf("S3_SYNC: Successfully downloaded face image, Size: %zu bytes\n", imageData.length());
    fflush(stdout);

    return true;
}

// ============================================================================
// ORIGINAL MQTT IMPLEMENTATION (PRESERVED)
// ============================================================================

// Singleton instance getter
MqttHeartbeatManager* MqttHeartbeatManager::GetInstance()
{
    if (m_instance == nullptr)
    {
        QMutexLocker locker(&m_mutex);
        if (m_instance == nullptr)
        {
            qDebug() << "MQTT_DEBUG: Creating new MqttHeartbeatManager instance";
            m_instance = new MqttHeartbeatManager();
        }
    }
    return m_instance;
}

MqttHeartbeatManager::MqttHeartbeatManager(QObject *parent)
    : QObject(parent)
    , m_faceMosq(nullptr)            // FIX: was uninitialized
    , m_faceMqttConnected(false)     // FIX: was uninitialized
    , m_mosq(nullptr)
    , m_port(MQTT_PORT)
    , m_connected(false)             // FIX: was uninitialized — caused publishConfigRequest to return immediately
    , m_heartbeatTimer(nullptr)
    , m_s3SyncTimer(nullptr)
    , m_heartbeatInterval(HEARTBEAT_INTERVAL / 1000)
    , m_syncThread(nullptr)
    , m_syncWorker(nullptr)
    , m_syncInProgress(false)
    , m_configRequestInProgress(false)
    , m_tenantId("")
{
    qDebug() << "MQTT_DEBUG: MqttHeartbeatManager constructor called";
    
    int init_result = mosquitto_lib_init();
    qDebug() << "MQTT_DEBUG: mosquitto_lib_init result:" << init_result;
    
    m_deviceSn = myHelper::getCpuSerial();
    qDebug() << "MQTT_DEBUG: Device SN set to:" << m_deviceSn;
    
    // Load local JSON timestamps from database
    loadLocalJsonTimestamps();
    
    // Initialize pending files list (empty)
    m_pendingConfigFiles.clear();
    
    qDebug() << "MQTT_DEBUG: MqttHeartbeatManager constructor completed";
}

// ============================================================================
// Step 4: Update MqttHeartbeatManager Destructor
// ============================================================================

MqttHeartbeatManager::~MqttHeartbeatManager()
{
    qDebug() << "MQTT_DEBUG: MqttHeartbeatManager destructor called";
    
    // Stop sync thread
    if (m_syncThread) {
        qDebug() << "MQTT_DEBUG: Stopping sync thread";
        m_syncThread->quit();
        if (!m_syncThread->wait(5000)) {
            qDebug() << "MQTT_DEBUG: Sync thread did not stop, terminating";
            m_syncThread->terminate();
            m_syncThread->wait();
        }
        delete m_syncWorker;
        delete m_syncThread;
    }
    
    // Stop timers
    if (m_heartbeatTimer) {
        m_heartbeatTimer->stop();
        delete m_heartbeatTimer;
    }
    
    if (m_s3SyncTimer) {
        m_s3SyncTimer->stop();
        delete m_s3SyncTimer;
    }
    
    // Disconnect MQTT
    if (m_mosq) {
        mosquitto_loop_stop(m_mosq, false);
        mosquitto_disconnect(m_mosq);
        mosquitto_destroy(m_mosq);
    }
    
    mosquitto_lib_cleanup();
}

void MqttHeartbeatManager::loadLocalJsonTimestamps()
{
    qDebug() << "MQTT_DEBUG: Loading local JSON timestamps from database";
    
    RegisteredFacesDB* db = RegisteredFacesDB::GetInstance();
    if (!db) {
        qDebug() << "MQTT_DEBUG: Failed to get database instance";
        return;
    }
    
    QString employeeTimestamp;
    QString faceTimestamp;
    QString rfidTimestamp;
    QString fingerprintTimestamp;  // ✅ NEW
    QString fourDoorTimestamp;     // ✅ NEW
    
    // Try to get all timestamps
    if (db->GetAllJsonTimestamps(employeeTimestamp, faceTimestamp, rfidTimestamp, 
                                  fingerprintTimestamp, fourDoorTimestamp)) {
        m_localJsonTimestamps["employees.json"] = employeeTimestamp;
        m_localJsonTimestamps["faces.json"] = faceTimestamp;
        m_localJsonTimestamps["rfid.json"] = rfidTimestamp;
        m_localJsonTimestamps["fingerprints.json"] = fingerprintTimestamp;          // ✅
        m_localJsonTimestamps["four_door_controller.json"] = fourDoorTimestamp;    // ✅
        
        qDebug() << "MQTT_DEBUG: Loaded local timestamps:";
        qDebug() << "MQTT_DEBUG:   employees.json:" << employeeTimestamp;
        qDebug() << "MQTT_DEBUG:   faces.json:" << faceTimestamp;
        qDebug() << "MQTT_DEBUG:   rfid.json:" << rfidTimestamp;
        qDebug() << "MQTT_DEBUG:   fingerprint.json:" << fingerprintTimestamp;           // ✅
        qDebug() << "MQTT_DEBUG:   four_door_controller.json:" << fourDoorTimestamp;     // ✅
    } else {
        // Fallback to old method for backward compatibility
        if (db->GetJsonLastModifiedTimestamps(employeeTimestamp, faceTimestamp)) {
            m_localJsonTimestamps["employees.json"] = employeeTimestamp;
            m_localJsonTimestamps["faces.json"] = faceTimestamp;
            
            qDebug() << "MQTT_DEBUG: Loaded legacy timestamps (employees, faces only)";
        }
        
        // Try individual timestamp retrievals
        if (db->GetRfidJsonTimestamp(rfidTimestamp)) {
            m_localJsonTimestamps["rfid.json"] = rfidTimestamp;
            qDebug() << "MQTT_DEBUG:   rfid.json:" << rfidTimestamp;
        }
        
        if (db->GetFingerprintJsonTimestamp(fingerprintTimestamp)) {          // ✅
            m_localJsonTimestamps["fingerprints.json"] = fingerprintTimestamp;
            qDebug() << "MQTT_DEBUG:   fingerprint.json:" << fingerprintTimestamp;
        }
        
        if (db->GetFourDoorJsonTimestamp(fourDoorTimestamp)) {                // ✅
            m_localJsonTimestamps["four_door_controller.json"] = fourDoorTimestamp;
            qDebug() << "MQTT_DEBUG:   four_door_controller.json:" << fourDoorTimestamp;
        }
    }
}

// Parse URL method
bool MqttHeartbeatManager::parseUrl(const QString& url, QString& host, int& port)
{
    qDebug() << "MQTT_DEBUG: Parsing URL:" << url;
    
    // Remove quotes if present
    QString cleanUrl = url;
    if (cleanUrl.startsWith("\"") && cleanUrl.endsWith("\"")) {
        cleanUrl = cleanUrl.mid(1, cleanUrl.length() - 2);
        qDebug() << "MQTT_DEBUG: Removed quotes from URL:" << cleanUrl;
    }
    
    // Check for mqtt:// prefix
    if (!cleanUrl.startsWith("mqtt://")) {
        qDebug() << "MQTT_DEBUG: URL does not start with mqtt://";
        return false;
    }
    
    // Extract host and optional port
    QString hostPort = cleanUrl.mid(7); // Remove "mqtt://"
    
    // Split host and port
    QStringList parts = hostPort.split(":");
    
    if (parts.isEmpty() || parts[0].isEmpty()) {
        qDebug() << "MQTT_DEBUG: Invalid host in URL";
        return false;
    }
    
    host = parts[0];
    
    // Check for port
    if (parts.size() > 1) {
        bool ok;
        int parsedPort = parts[1].toInt(&ok);
        if (ok) {
            port = parsedPort;
        } else {
            qDebug() << "MQTT_DEBUG: Invalid port in URL:" << parts[1];
            port = 1883; // Default MQTT port
        }
    } else {
        port = 1883; // Default MQTT port
    }
    
    qDebug() << "MQTT_DEBUG: Successfully parsed URL - Host:" << host << "Port:" << port;
    return true;
}

// Initialize MQTT connection
bool MqttHeartbeatManager::initialize()
{
    qDebug() << "MQTT_PRINTF: Initializing MQTT connection";
    
    // FIX: Appended "_hb" to client ID because PostPersonRecordThread uses "rv1109_" + m_deviceSn.
    // If two connections use the exact same client ID, the broker will constantly disconnect one to connect the other,
    // causing an infinite reconnect storm (reason code 7).
    QString clientId = QString("rv1109_%1_hb").arg(m_deviceSn);
    qDebug() << "MQTT_DEBUG: Client ID set to:" << clientId;
    
    m_mosq = mosquitto_new(clientId.toStdString().c_str(), true, this);
    if (!m_mosq) {
        qDebug() << "MQTT_DEBUG: Failed to create mosquitto instance";
        return false;
    }
    
    qDebug() << "MQTT_DEBUG: Setting username and password";
    mosquitto_username_pw_set(m_mosq, MQTT_USERNAME, MQTT_PASSWORD);
    
    qDebug() << "MQTT_DEBUG: Setting callbacks";
    mosquitto_connect_callback_set(m_mosq, onConnect);
    mosquitto_disconnect_callback_set(m_mosq, onDisconnect);  // FIX: was missing — without this m_connected never resets on drop
    mosquitto_message_callback_set(m_mosq, onMessage);
    mosquitto_log_callback_set(m_mosq, onLog);
    
    qDebug() << "MQTT_DEBUG: Connecting to broker" << MQTT_BROKER << ":" << MQTT_PORT;
    int connect_result = mosquitto_connect(m_mosq, MQTT_BROKER, MQTT_PORT, 60);
    if (connect_result != MOSQ_ERR_SUCCESS) {
        qDebug() << "MQTT_DEBUG: Failed to connect:" << mosquitto_strerror(connect_result);
        return false;
    }
    
    qDebug() << "MQTT_DEBUG: Starting network loop";
    mosquitto_loop_start(m_mosq);
    
    // Start heartbeat timer
    qDebug() << "MQTT_DEBUG: Starting heartbeat timer";
    m_heartbeatTimer = new QTimer(this);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &MqttHeartbeatManager::publishHeartbeat);
    m_heartbeatTimer->start(HEARTBEAT_INTERVAL);
    
    // Fire the first heartbeat immediately (after 1 second) so the user doesn't have to wait 30 seconds
    QTimer::singleShot(1000, this, &MqttHeartbeatManager::publishHeartbeat);
    
    // REMOVED: Config check timer - we'll check on heartbeat response only
    // No more periodic config_request publishing
    
    qDebug() << "MQTT_DEBUG: MQTT initialization completed successfully";
    
    return true;
}

void MqttHeartbeatManager::onSyncFinished(bool success)
{
    QMutexLocker locker(&m_syncMutex);
    m_syncInProgress = false;
    
    if (success) {
        qDebug() << "MQTT_DEBUG: S3 sync completed successfully";
    } else {
        qDebug() << "MQTT_DEBUG: S3 sync failed";
    }
}

void MqttHeartbeatManager::onS3SyncTimer()
{
    QMutexLocker locker(&m_syncMutex);
    
    if (m_syncInProgress) {
        qDebug() << "MQTT_DEBUG: Sync already in progress, skipping";
        return;
    }
    
    qDebug() << "MQTT_DEBUG: Triggering periodic S3 sync";
    m_syncInProgress = true;
    
    // Trigger sync in worker thread
    QMetaObject::invokeMethod(m_syncWorker, "doSync", Qt::QueuedConnection);
}

// Set heartbeat interval
void MqttHeartbeatManager::setHeartbeatInterval(int seconds)
{
    qDebug() << "MQTT_DEBUG: Setting heartbeat interval to" << seconds << "seconds";
    m_heartbeatInterval = seconds;
    if (m_heartbeatTimer)
    {
        m_heartbeatTimer->setInterval(seconds * 1000);
    }
}

// Heartbeat timer callback
void MqttHeartbeatManager::onHeartbeatTimer()
{
    if (!m_connected)
    {
        qDebug() << "MQTT_DEBUG: Heartbeat timer fired but not connected, skipping";
        return;
    }
    
    publishHeartbeat();
}

// [REST OF THE ORIGINAL CODE CONTINUES - All callback functions, publishing methods, etc. remain unchanged]
// I'll include the key callback functions below:

// Connection callback
void MqttHeartbeatManager::onConnect(struct mosquitto* mosq, void* obj, int reason_code)
{
    MqttHeartbeatManager* manager = static_cast<MqttHeartbeatManager*>(obj);
    qDebug() << "MQTT_DEBUG: Connect callback received with reason code:" << reason_code;
    printf("MQTT_PRINTF: Connect callback received with reason code: %d\n", reason_code);
    fflush(stdout);
    
    if (reason_code == 0)
    {
        qDebug() << "MQTT_DEBUG: Successfully connected to MQTT broker";
        printf("MQTT_PRINTF: Successfully connected to MQTT broker\n");
        fflush(stdout);
        
        manager->m_connected = true;
        
        // Subscribe to heartbeat_response topic
        std::string heartbeatResponseTopic = "devices/controller/" + manager->m_deviceSn.toStdString() + "/heartbeat_response";
        qDebug() << "MQTT_DEBUG: Subscribing to:" << QString::fromStdString(heartbeatResponseTopic);
        
        int rc = mosquitto_subscribe(mosq, nullptr, heartbeatResponseTopic.c_str(), 0);
        if (rc == MOSQ_ERR_SUCCESS) {
            qDebug() << "MQTT_DEBUG: Successfully subscribed to heartbeat_response";
        }
        
        // Subscribe to config_response topic
        std::string configResponseTopic = "devices/controller/" + manager->m_deviceSn.toStdString() + "/config_response";
        qDebug() << "MQTT_DEBUG: Subscribing to:" << QString::fromStdString(configResponseTopic);
        printf("MQTT_PRINTF: Subscribing to config_response: %s\n", configResponseTopic.c_str());
        fflush(stdout);
        
        rc = mosquitto_subscribe(mosq, nullptr, configResponseTopic.c_str(), 0);
        if (rc == MOSQ_ERR_SUCCESS) {
            qDebug() << "MQTT_DEBUG: Successfully subscribed to config_response";
            printf("MQTT_PRINTF: Successfully subscribed to config_response\n");
            fflush(stdout);
        }
    }
    else
    {
        qDebug() << "MQTT_DEBUG: Connection failed with reason code:" << reason_code;
        manager->m_connected = false;
    }
}

QStringList MqttHeartbeatManager::getChangedJsonFiles(const Json::Value& heartbeatFiles)
{
    QStringList changedFiles;
    
    qDebug() << "MQTT_DEBUG: Checking which JSON files have changed";
    
    if (!heartbeatFiles.isObject()) {
        qDebug() << "MQTT_DEBUG: heartbeatFiles is not an object";
        return changedFiles;
    }
    
    // Check employees.json
    if (heartbeatFiles.isMember("employees.json")) {
        Json::Value employeesJson = heartbeatFiles["employees.json"];
        if (employeesJson.isMember("last_modified")) {
            QString serverTimestamp = QString::fromStdString(employeesJson["last_modified"].asString());
            QString localTimestamp = m_localJsonTimestamps.value("employees.json", "");
            
            qDebug() << "MQTT_DEBUG: employees.json - Server:" << serverTimestamp << "Local:" << localTimestamp;
            
            // Only add if changed AND not already pending
            if ((localTimestamp.isEmpty() || serverTimestamp > localTimestamp) && 
                !m_pendingConfigFiles.contains("employees.json")) {
                changedFiles.append("employees.json");
                qDebug() << "MQTT_DEBUG: employees.json has changed - WILL REQUEST";
            } else if (m_pendingConfigFiles.contains("employees.json")) {
                qDebug() << "MQTT_DEBUG: employees.json already pending - SKIP";
            } else {
                qDebug() << "MQTT_DEBUG: employees.json unchanged - SKIP";
            }
        }
    }
    
    // Check faces.json
    if (heartbeatFiles.isMember("faces.json")) {
        Json::Value facesJson = heartbeatFiles["faces.json"];
        if (facesJson.isMember("last_modified")) {
            QString serverTimestamp = QString::fromStdString(facesJson["last_modified"].asString());
            QString localTimestamp = m_localJsonTimestamps.value("faces.json", "");
            
            qDebug() << "MQTT_DEBUG: faces.json - Server:" << serverTimestamp << "Local:" << localTimestamp;
            
            if ((localTimestamp.isEmpty() || serverTimestamp > localTimestamp) && 
                !m_pendingConfigFiles.contains("faces.json")) {
                changedFiles.append("faces.json");
                qDebug() << "MQTT_DEBUG: faces.json has changed - WILL REQUEST";
            } else if (m_pendingConfigFiles.contains("faces.json")) {
                qDebug() << "MQTT_DEBUG: faces.json already pending - SKIP";
            } else {
                qDebug() << "MQTT_DEBUG: faces.json unchanged - SKIP";
            }
        }
    }
    
    // Check doors.json
    if (heartbeatFiles.isMember("doors.json")) {
        Json::Value doorsJson = heartbeatFiles["doors.json"];
        if (doorsJson.isMember("last_modified")) {
            QString serverTimestamp = QString::fromStdString(doorsJson["last_modified"].asString());
            QString localTimestamp = m_localJsonTimestamps.value("doors.json", "");
            
            qDebug() << "MQTT_DEBUG: doors.json - Server:" << serverTimestamp << "Local:" << localTimestamp;
            
            if ((localTimestamp.isEmpty() || serverTimestamp > localTimestamp) && 
                !m_pendingConfigFiles.contains("doors.json")) {
                changedFiles.append("doors.json");
                qDebug() << "MQTT_DEBUG: doors.json has changed - WILL REQUEST";
            } else if (m_pendingConfigFiles.contains("doors.json")) {
                qDebug() << "MQTT_DEBUG: doors.json already pending - SKIP";
            } else {
                qDebug() << "MQTT_DEBUG: doors.json unchanged - SKIP";
            }
        }
    }
    
    // Check access_levels.json
    if (heartbeatFiles.isMember("access_levels.json")) {
        Json::Value accessJson = heartbeatFiles["access_levels.json"];
        if (accessJson.isMember("last_modified")) {
            QString serverTimestamp = QString::fromStdString(accessJson["last_modified"].asString());
            QString localTimestamp = m_localJsonTimestamps.value("access_levels.json", "");
            
            qDebug() << "MQTT_DEBUG: access_levels.json - Server:" << serverTimestamp << "Local:" << localTimestamp;
            
            if ((localTimestamp.isEmpty() || serverTimestamp > localTimestamp) && 
                !m_pendingConfigFiles.contains("access_levels.json")) {
                changedFiles.append("access_levels.json");
                qDebug() << "MQTT_DEBUG: access_levels.json has changed - WILL REQUEST";
            } else if (m_pendingConfigFiles.contains("access_levels.json")) {
                qDebug() << "MQTT_DEBUG: access_levels.json already pending - SKIP";
            } else {
                qDebug() << "MQTT_DEBUG: access_levels.json unchanged - SKIP";
            }
        }
    }
    
    // Check rfid.json
    if (heartbeatFiles.isMember("rfid.json")) {
        Json::Value rfidJson = heartbeatFiles["rfid.json"];
        if (rfidJson.isMember("last_modified")) {
            QString serverTimestamp = QString::fromStdString(rfidJson["last_modified"].asString());
            QString localTimestamp = m_localJsonTimestamps.value("rfid.json", "");
            
            qDebug() << "MQTT_DEBUG: rfid.json - Server:" << serverTimestamp << "Local:" << localTimestamp;
            
            if ((localTimestamp.isEmpty() || serverTimestamp > localTimestamp) && 
                !m_pendingConfigFiles.contains("rfid.json")) {
                changedFiles.append("rfid.json");
                qDebug() << "MQTT_DEBUG: rfid.json has changed - WILL REQUEST";
            } else if (m_pendingConfigFiles.contains("rfid.json")) {
                qDebug() << "MQTT_DEBUG: rfid.json already pending - SKIP";
            } else {
                qDebug() << "MQTT_DEBUG: rfid.json unchanged - SKIP";
            }
        }
    }

    // ✅ CHECK FINGERPRINT.JSON
    if (heartbeatFiles.isMember("fingerprints.json")) {
        Json::Value fingerprintJson = heartbeatFiles["fingerprints.json"];
        if (fingerprintJson.isMember("last_modified")) {
            QString serverTimestamp = QString::fromStdString(fingerprintJson["last_modified"].asString());
            QString localTimestamp = m_localJsonTimestamps.value("fingerprints.json", "");
            
            qDebug() << "MQTT_DEBUG: fingerprint.json - Server:" << serverTimestamp << "Local:" << localTimestamp;
            
            if ((localTimestamp.isEmpty() || serverTimestamp > localTimestamp) && 
                !m_pendingConfigFiles.contains("fingerprints.json")) {
                changedFiles.append("fingerprints.json");
                qDebug() << "MQTT_DEBUG: fingerprint.json has changed - WILL REQUEST";
            }
        }
    }
    
    // ✅ CHECK FOUR_DOOR_CONTROLLER.JSON
    if (heartbeatFiles.isMember("four_door_controller.json")) {
        Json::Value fourDoorJson = heartbeatFiles["four_door_controller.json"];
        if (fourDoorJson.isMember("last_modified")) {
            QString serverTimestamp = QString::fromStdString(fourDoorJson["last_modified"].asString());
            QString localTimestamp = m_localJsonTimestamps.value("four_door_controller.json", "");
            
            qDebug() << "MQTT_DEBUG: four_door_controller.json - Server:" << serverTimestamp << "Local:" << localTimestamp;
            
            if ((localTimestamp.isEmpty() || serverTimestamp > localTimestamp) && 
                !m_pendingConfigFiles.contains("four_door_controller.json")) {
                changedFiles.append("four_door_controller.json");
                qDebug() << "MQTT_DEBUG: four_door_controller.json has changed - WILL REQUEST";
            }
        }
    }
    
    qDebug() << "MQTT_DEBUG: Total NEW changed files:" << changedFiles.size();
    if (changedFiles.isEmpty()) {
        qDebug() << "MQTT_DEBUG: No NEW changes - NO config_request will be sent";
    } else {
        qDebug() << "MQTT_DEBUG: NEW changed files:" << changedFiles.join(", ");
    }
    
    return changedFiles;
}

// Disconnect callback
void MqttHeartbeatManager::onDisconnect(struct mosquitto* mosq, void* obj, int reason_code)
{
    MqttHeartbeatManager* manager = static_cast<MqttHeartbeatManager*>(obj);
    
    qDebug() << "MQTT_DEBUG: Disconnected from broker with reason code:" << reason_code;
    printf("MQTT_PRINTF: Disconnected from broker with reason code: %d\n", reason_code);
    fflush(stdout);
    
    manager->m_connected = false;
    
    if (reason_code != 0)
    {
        qDebug() << "MQTT_DEBUG: Attempting to reconnect to broker";
        printf("MQTT_PRINTF: Attempting to reconnect to broker\n");
        fflush(stdout);
        
        int rc = mosquitto_reconnect(mosq);
        if (rc == MOSQ_ERR_SUCCESS)
        {
            qDebug() << "MQTT_DEBUG: Reconnection initiated successfully";
            printf("MQTT_PRINTF: Reconnection initiated successfully\n");
            fflush(stdout);
        }
        else
        {
            qDebug() << "MQTT_DEBUG: Failed to initiate reconnection:" << rc << " - " << mosquitto_strerror(rc);
            printf("MQTT_PRINTF: Failed to initiate reconnection: %d - %s\n", rc, mosquitto_strerror(rc));
            fflush(stdout);
        }
    }
}

// Publish callback
void MqttHeartbeatManager::onPublish(struct mosquitto* mosq, void* obj, int mid)
{
    qDebug() << "MQTT_DEBUG: Message published successfully with ID:" << mid;
    printf("MQTT_PRINTF: Message published successfully with ID: %d\n", mid);
    fflush(stdout);
}

// Message callback
void MqttHeartbeatManager::onMessage(struct mosquitto* mosq, void* obj, const struct mosquitto_message* message)
{
    MqttHeartbeatManager* manager = static_cast<MqttHeartbeatManager*>(obj);
    
    qDebug() << "MQTT_DEBUG: Message received on topic:" << message->topic;
    
    if (message->payloadlen > 0) {
        std::string payload(static_cast<char*>(message->payload), message->payloadlen);
        QString payloadStr = QString::fromStdString(payload);
        
        std::string topic(message->topic);
        
        // Route to appropriate handler
        if (topic.find("/heartbeat_response") != std::string::npos) {
            manager->handleHeartbeatResponse(payloadStr);
        } else if (topic.find("/config_response") != std::string::npos) {
            manager->handleConfigResponse(payloadStr);
        } else if (topic.find("/command") != std::string::npos) {
            manager->handleCommand(payloadStr);
        }
    }
}


// Log callback
void MqttHeartbeatManager::onLog(struct mosquitto* mosq, void* obj, int level, const char* str)
{
    switch (level) {
        case MOSQ_LOG_INFO:
            qDebug() << "MQTT_INFO:" << str;
            break;
        case MOSQ_LOG_NOTICE:
            qDebug() << "MQTT_NOTICE:" << str;
            break;
        case MOSQ_LOG_WARNING:
            qDebug() << "MQTT_WARNING:" << str;
            break;
        case MOSQ_LOG_ERR:
            qDebug() << "MQTT_ERROR:" << str;
            break;
        case MOSQ_LOG_DEBUG:
            qDebug() << "MQTT_DEBUG_LIB:" << str;
            break;
        default:
            qDebug() << "MQTT_LOG [" << level << "]:" << str;
    }
}

// Publish heartbeat
bool MqttHeartbeatManager::publishHeartbeat()
{
    if (!m_mosq || !m_connected)
    {
        qDebug() << "MQTT_DEBUG: Cannot publish heartbeat, not connected to broker";
        return false;
    }
    
    // Create topic string - Using controller format
    std::string topic = "devices/controller/" + m_deviceSn.toStdString() + "/heartbeat";
    
    // Create JSON message with new format
    Json::Value json;
    json["msg_type"] = "heartbeat";
    json["sn"] = m_deviceSn.toStdString();
    json["timestamp"] = getTime();  // Format: YYYYMMDDHHMMSS
    
    // Convert to JSON string
    Json::FastWriter fast_writer;
    std::string jsonStr = fast_writer.write(json);
    
    // Remove trailing newline
    if (!jsonStr.empty() && jsonStr[jsonStr.length() - 1] == '\n')
    {
        jsonStr = jsonStr.substr(0, jsonStr.length() - 1);
    }
    
    qDebug() << "MQTT_DEBUG: Publishing heartbeat to topic:" << QString::fromStdString(topic);
    printf("MQTT_PRINTF: Publishing heartbeat: %s\n", jsonStr.c_str());
    fflush(stdout);
    
    // Publish message with QoS 0 (fire and forget)
    int rc = mosquitto_publish(m_mosq, nullptr, topic.c_str(), jsonStr.length(), jsonStr.c_str(), 0, false);
    if (rc != MOSQ_ERR_SUCCESS)
    {
        qDebug() << "MQTT_DEBUG: Failed to publish heartbeat:" << rc << " - " << mosquitto_strerror(rc);
        printf("MQTT_PRINTF: Failed to publish heartbeat: %d - %s\n", rc, mosquitto_strerror(rc));
        fflush(stdout);
        return false;
    }
    
    // Send a full config_request along with the FIRST heartbeat on boot
    static bool isFirstHeartbeat = true;
    if (isFirstHeartbeat) {
        isFirstHeartbeat = false;
        qDebug() << "MQTT_DEBUG: FIRST HEARTBEAT - Forcing config_request for all files";
        QStringList allFiles;
        allFiles << "employees.json" << "faces.json" << "doors.json" << "access_levels.json" << "rfid.json" << "fingerprints.json" << "four_door_controller.json";
        publishConfigRequest(allFiles);
    }
    
    return true;
}

bool MqttHeartbeatManager::publishConfigRequest(const QStringList& files)
{
    if (!m_mosq || !m_connected) {
        qDebug() << "MQTT_DEBUG: Cannot publish config_request, not connected";
        return false;
    }
    
    if (files.isEmpty()) {
        qDebug() << "MQTT_DEBUG: No files to request, skipping config_request";
        return false;
    }
    
    // FIX: Expire stale pending entries (no config_response after 60 seconds)
    // Without this, a lost config_response permanently blocks future sync
    qint64 now = QDateTime::currentSecsSinceEpoch();
    QStringList stale;
    for (auto it = m_pendingConfigExpiry.begin(); it != m_pendingConfigExpiry.end(); ++it) {
        if (now - it.value() > 60) {
            stale.append(it.key());
        }
    }
    for (const QString& f : stale) {
        qDebug() << "MQTT_DEBUG: Clearing stale pending entry (>60s):" << f;
        printf("MQTT_PRINTF: config_request for '%s' timed out - will retry\n",
               f.toStdString().c_str());
        m_pendingConfigFiles.removeAll(f);
        m_pendingConfigExpiry.remove(f);
    }
    
    // Add these files to pending list to prevent duplicate requests
    qint64 sendTime = QDateTime::currentSecsSinceEpoch();
    for (const QString& file : files) {
        if (!m_pendingConfigFiles.contains(file)) {
            m_pendingConfigFiles.append(file);
            m_pendingConfigExpiry[file] = sendTime;  // record when we sent it
        }
    }
    
    qDebug() << "MQTT_DEBUG: Pending config files:" << m_pendingConfigFiles.join(", ");
    printf("MQTT_PRINTF: Pending config files: %s\n", m_pendingConfigFiles.join(", ").toStdString().c_str());
    fflush(stdout);
    
    std::string topic = "devices/controller/" + m_deviceSn.toStdString() + "/config_request";
    
    Json::Value json;
    json["msg_type"] = "config_request";
    json["sn"] = m_deviceSn.toStdString();
    json["timestamp"] = getTime();
    
    // Add files array
    Json::Value filesArray(Json::arrayValue);
    for (const QString& file : files) {
        filesArray.append(file.toStdString());
    }
    json["files"] = filesArray;
    
    Json::FastWriter writer;
    std::string jsonStr = writer.write(json);
    
    // Remove trailing newline
    if (!jsonStr.empty() && jsonStr[jsonStr.length() - 1] == '\n') {
        jsonStr = jsonStr.substr(0, jsonStr.length() - 1);
    }
    
    qDebug() << "MQTT_DEBUG: Publishing config_request (ONE-TIME):" << QString::fromStdString(jsonStr);
    printf("MQTT_PRINTF: Publishing config_request (ONE-TIME): %s\n", jsonStr.c_str());
    fflush(stdout);
    
    // QoS 1: broker must confirm delivery — QoS 0 was silent-fail if broker wasn't ready
    int rc = mosquitto_publish(m_mosq, nullptr, topic.c_str(), jsonStr.length(), jsonStr.c_str(), 1, false);
    if (rc != MOSQ_ERR_SUCCESS) {
        qDebug() << "MQTT_DEBUG: Failed to publish config_request:" << mosquitto_strerror(rc);
        
        // Remove from pending list on failure
        for (const QString& file : files) {
            m_pendingConfigFiles.removeAll(file);
        }
        return false;
    }
    
    return true;
}

// Handle incoming command
void MqttHeartbeatManager::handleCommand(const QString& payload)
{
    qDebug() << "MQTT_DEBUG: Handling command:" << payload;
    printf("MQTT_PRINTF: Handling command: %s\n", payload.toStdString().c_str());
    fflush(stdout);
    
    // Parse JSON payload
    Json::Value root;
    Json::Reader reader;
    
    if (!reader.parse(payload.toStdString(), root))
    {
        qDebug() << "MQTT_DEBUG: Failed to parse command JSON";
        printf("MQTT_PRINTF: Failed to parse command JSON\n");
        fflush(stdout);
        return;
    }
    
    // Extract command type
    std::string commandType = root.get("command", "").asString();
    qDebug() << "MQTT_DEBUG: Command type:" << QString::fromStdString(commandType);
    printf("MQTT_PRINTF: Command type: %s\n", commandType.c_str());
    fflush(stdout);
    
    // Handle different command types
    if (commandType == "sync_users")
    {
        qDebug() << "MQTT_DEBUG: Received sync_users command, triggering S3 synchronization";
        printf("MQTT_PRINTF: Received sync_users command, triggering S3 synchronization\n");
        fflush(stdout);
    }
    else if (commandType == "update_face")
    {
        // Extract employee ID and face URL
        std::string employeeId = root.get("employee_id", "").asString();
        std::string faceUrl = root.get("face_url", "").asString();
        
        if (!employeeId.empty() && !faceUrl.empty())
        {
            qDebug() << "MQTT_DEBUG: Received update_face command for employee:" << QString::fromStdString(employeeId);
            printf("MQTT_PRINTF: Received update_face command for employee: %s\n", employeeId.c_str());
            fflush(stdout);
            
            qint64 lastModified = QDateTime::currentSecsSinceEpoch();
             int personalModuleId = 0; // or fixed value

updateUserFromS3Data(QString::fromStdString(employeeId),
                     "",
                     QString::fromStdString(faceUrl),
                     lastModified,
                     personalModuleId);
        }
    }
    else
    {
        qDebug() << "MQTT_DEBUG: Unknown command type:" << QString::fromStdString(commandType);
        printf("MQTT_PRINTF: Unknown command type: %s\n", commandType.c_str());
        fflush(stdout);
    }
}

void MqttHeartbeatManager::handleHeartbeatResponse(const QString& payload)
{
    qDebug() << "MQTT_DEBUG: ========== HEARTBEAT RESPONSE HANDLER ==========";
    printf("MQTT_PRINTF: ========== HEARTBEAT RESPONSE HANDLER ==========\n");
    fflush(stdout);
    
    Json::Value root;
    Json::Reader reader;
    
    if (!reader.parse(payload.toStdString(), root)) {
        qDebug() << "MQTT_DEBUG: Failed to parse heartbeat response";
        return;
    }
    
    // Extract tenant_id
    if (root.isMember("tenant_id")) {
        QString tenantId = QString::fromStdString(root["tenant_id"].asString());
        if (tenantId.startsWith("\"") && tenantId.endsWith("\"")) {
            tenantId = tenantId.mid(1, tenantId.length() - 2);
        }
        m_tenantId = tenantId;
        qDebug() << "MQTT_DEBUG: Stored tenant_id:" << m_tenantId;
        
        // Update UI with tenant name
        ConnHttpServerThread::GetInstance()->updateTenantName(m_tenantId);
    }
    
    // Check config_files or files section
    Json::Value configFiles;
    if (root.isMember("config_files")) {
        configFiles = root["config_files"];
    } else if (root.isMember("files")) {
        configFiles = root["files"];
    } else {
        qDebug() << "MQTT_DEBUG: No config_files or files array in heartbeat response";
        return;
    }
    
    if (!configFiles.isObject() && !configFiles.isArray()) {
        qDebug() << "MQTT_DEBUG: config_files is neither an object nor an array";
        return;
    }
    
    // Get ONLY NEW changed files (excludes files already pending)
    QStringList changedFiles = getChangedJsonFiles(configFiles);
    
    // ONLY publish config_request if there are NEW changes
    if (!changedFiles.isEmpty()) {
        qDebug() << "MQTT_DEBUG: Found" << changedFiles.size() << "NEW changed file(s):" << changedFiles.join(", ");
        printf("MQTT_PRINTF: Publishing config_request for NEW changes ONLY: %s\n", 
               changedFiles.join(", ").toStdString().c_str());
        fflush(stdout);
        
        // Publish config_request ONLY for NEW changed files (ONE-TIME)
        publishConfigRequest(changedFiles);
    } else {
        if (!m_pendingConfigFiles.isEmpty()) {
            qDebug() << "MQTT_DEBUG: No NEW changes - Still waiting for sync of:" << m_pendingConfigFiles.join(", ");
            printf("MQTT_PRINTF: No NEW changes - Still waiting for sync of: %s\n", 
                   m_pendingConfigFiles.join(", ").toStdString().c_str());
        } else {
            qDebug() << "MQTT_DEBUG: All files are up to date - NO config_request needed";
            printf("MQTT_PRINTF: All files up to date - NO config_request needed\n");
        }
        fflush(stdout);
    }
    
    qDebug() << "MQTT_DEBUG: ========== HEARTBEAT RESPONSE COMPLETE ==========";
}

// Check if synchronization is needed based on timestamps
void MqttHeartbeatManager::checkIfSyncNeeded(const QString& serverEmployeeTimestamp, 
                                             const QString& serverFaceTimestamp)
{
    qDebug() << "MQTT_DEBUG: Checking if sync is needed";
    printf("MQTT_PRINTF: Checking if sync is needed\n");
    fflush(stdout);
    
    RegisteredFacesDB* db = RegisteredFacesDB::GetInstance();
    if (!db)
    {
        qDebug() << "MQTT_DEBUG: Failed to get RegisteredFacesDB instance for sync check";
        printf("MQTT_PRINTF: Failed to get RegisteredFacesDB instance for sync check\n");
        fflush(stdout);
        return;
    }
    
    // Get current timestamps from database
    QString localEmployeeTimestamp;
    QString localFaceTimestamp;
    
    if (db->GetJsonLastModifiedTimestamps(localEmployeeTimestamp, localFaceTimestamp))
    {
        qDebug() << "MQTT_DEBUG: Comparing timestamps:";
        qDebug() << "MQTT_DEBUG:   Local employees.json:" << localEmployeeTimestamp;
        qDebug() << "MQTT_DEBUG:   Server employees.json:" << serverEmployeeTimestamp;
        qDebug() << "MQTT_DEBUG:   Local faces.json:" << localFaceTimestamp;
        qDebug() << "MQTT_DEBUG:   Server faces.json:" << serverFaceTimestamp;
        
        printf("MQTT_PRINTF: Comparing timestamps:\n");
        printf("MQTT_PRINTF:   Local employees.json: %s\n", localEmployeeTimestamp.toStdString().c_str());
        printf("MQTT_PRINTF:   Server employees.json: %s\n", serverEmployeeTimestamp.toStdString().c_str());
        printf("MQTT_PRINTF:   Local faces.json: %s\n", localFaceTimestamp.toStdString().c_str());
        printf("MQTT_PRINTF:   Server faces.json: %s\n", serverFaceTimestamp.toStdString().c_str());
        fflush(stdout);
        
        // Check if server has newer data (ISO 8601 format can be compared as strings)
        bool employeesNeedSync = (!serverEmployeeTimestamp.isEmpty() && 
                                  serverEmployeeTimestamp > localEmployeeTimestamp);
        bool facesNeedSync = (!serverFaceTimestamp.isEmpty() && 
                             serverFaceTimestamp > localFaceTimestamp);
        
        if (employeesNeedSync || facesNeedSync)
        {
            qDebug() << "MQTT_DEBUG: Synchronization needed:";
            printf("MQTT_PRINTF: Synchronization needed:\n");
            
            if (employeesNeedSync)
            {
                qDebug() << "MQTT_DEBUG:   - employees.json is outdated";
                printf("MQTT_PRINTF:   - employees.json is outdated\n");
            }
            if (facesNeedSync)
            {
                qDebug() << "MQTT_DEBUG:   - faces.json is outdated";
                printf("MQTT_PRINTF:   - faces.json is outdated\n");
            }
            fflush(stdout);
            
            // Trigger synchronization
            qDebug() << "MQTT_DEBUG: Triggering S3 synchronization...";
            printf("MQTT_PRINTF: Triggering S3 synchronization...\n");
            fflush(stdout);
            
        }
        else
        {
            qDebug() << "MQTT_DEBUG: All JSON files are up to date, no sync needed";
            printf("MQTT_PRINTF: All JSON files are up to date, no sync needed\n");
            fflush(stdout);
        }
    }
    else
    {
        qDebug() << "MQTT_DEBUG: Could not retrieve local timestamps, triggering sync to be safe";
        printf("MQTT_PRINTF: Could not retrieve local timestamps, triggering sync to be safe\n");
        fflush(stdout);
        
        // If we can't get local timestamps, trigger sync to be safe
    }
}

void MqttHeartbeatManager::handleConfigResponse(const QString& payload)
{
    qDebug() << "MQTT_DEBUG: ========== CONFIG RESPONSE HANDLER ==========";
    printf("MQTT_PRINTF: ========== CONFIG RESPONSE HANDLER ==========\n");
    printf("MQTT_PRINTF: Payload: %s\n", payload.toStdString().c_str());
    fflush(stdout);
    
    QMutexLocker locker(&m_configMutex);
    m_configRequestInProgress = false;
    
    Json::Value root;
    Json::Reader reader;
    
    if (!reader.parse(payload.toStdString(), root)) {
        qDebug() << "MQTT_DEBUG: Failed to parse config_response JSON";
        return;
    }
    
    if (!root.isMember("files") || !root["files"].isObject()) {
        qDebug() << "MQTT_DEBUG: No files section in config_response";
        return;
    }
    
    Json::Value files = root["files"];
    Json::Value::Members fileNames = files.getMemberNames();
    
    qDebug() << "MQTT_DEBUG: Received" << fileNames.size() << "file(s) in config_response";
    
    for (const auto& fileName : fileNames) {
        Json::Value fileInfo = files[fileName];
        
        if (!fileInfo.isMember("url") || !fileInfo.isMember("last_modified")) {
            qDebug() << "MQTT_DEBUG: Skipping" << QString::fromStdString(fileName) << "- missing url or last_modified";
            continue;
        }
        
        QString url = QString::fromStdString(fileInfo["url"].asString());
        QString lastModified = QString::fromStdString(fileInfo["last_modified"].asString());
        QString status = QString::fromStdString(fileInfo.get("status", "").asString());
        
        if (status != "available") {
            qDebug() << "MQTT_DEBUG: Skipping" << QString::fromStdString(fileName) << "- status:" << status;
            continue;
        }
        
        qDebug() << "MQTT_DEBUG: Processing:" << QString::fromStdString(fileName);
        qDebug() << "MQTT_DEBUG:   URL:" << url;
        qDebug() << "MQTT_DEBUG:   Last Modified:" << lastModified;
        
        // Download and sync this JSON file
        syncSpecificJson(QString::fromStdString(fileName), url, lastModified);
    }
    
    qDebug() << "MQTT_DEBUG: ========== CONFIG RESPONSE COMPLETE ==========";
}

bool MqttHeartbeatManager::syncSpecificJson(const QString& filename, const QString& jsonUrl, const QString& lastModified)
{
    qDebug() << "MQTT_DEBUG: Syncing" << filename << "from URL";
    printf("MQTT_PRINTF: Syncing %s from URL\n", filename.toStdString().c_str());
    fflush(stdout);
    
    // Download JSON content
    std::string jsonContent;
    if (!downloadJsonFromUrl(jsonUrl, jsonContent)) {
        qDebug() << "MQTT_DEBUG: Failed to download" << filename;
        return false;
    }
    
    // Parse JSON
    Json::Value jsonRoot;
    Json::Reader reader;
    if (!reader.parse(jsonContent, jsonRoot)) {
        qDebug() << "MQTT_DEBUG: Failed to parse" << filename;
        return false;
    }
    
    bool success = false;
    
    if (filename == "employees.json") {
        success = syncEmployeesJson(jsonRoot, lastModified);
    } 
    else if (filename == "faces.json") {
        success = syncFacesJson(jsonRoot, lastModified);
    }
    else if (filename == "rfid.json") {
        success = syncRfidJson(jsonRoot, lastModified);
    }
    // ✅ ADD FINGERPRINT.JSON HANDLER
    else if (filename == "fingerprints.json") {
        success = syncFingerprintJson(jsonRoot, lastModified);
    }
    // ✅ ADD FOUR_DOOR_CONTROLLER.JSON HANDLER
    else if (filename == "four_door_controller.json") {
        success = syncFourDoorControllerJson(jsonRoot, lastModified);
    }
    else if (filename == "doors.json") {
        // Store timestamp even if not processed
        m_localJsonTimestamps["doors.json"] = lastModified;
        qDebug() << "MQTT_DEBUG: Updated doors.json timestamp to" << lastModified;
        success = true;
    } 
    else if (filename == "access_levels.json") {
        // Store timestamp even if not processed
        m_localJsonTimestamps["access_levels.json"] = lastModified;
        qDebug() << "MQTT_DEBUG: Updated access_levels.json timestamp to" << lastModified;
        success = true;
    }
    else {
        qDebug() << "MQTT_DEBUG: Unknown file type:" << filename;
    }
    
    // IMPORTANT: Remove from pending list after successful sync
    if (success) {
        m_pendingConfigFiles.removeAll(filename);
        m_pendingConfigExpiry.remove(filename);  // FIX: clear expiry tracking too
        qDebug() << "MQTT_DEBUG: Removed" << filename << "from pending list";
        qDebug() << "MQTT_DEBUG: Remaining pending files:" << m_pendingConfigFiles.join(", ");
        printf("MQTT_PRINTF: Removed %s from pending list\n", filename.toStdString().c_str());
        printf("MQTT_PRINTF: Remaining pending files: %s\n", m_pendingConfigFiles.join(", ").toStdString().c_str());
        fflush(stdout);
    }
    
    return success;
}


extern int g_jsonUserCount;

bool MqttHeartbeatManager::syncEmployeesJson(const Json::Value& jsonRoot, const QString& serverLastModified)
{
    qDebug() << "MQTT_DEBUG: Syncing employees.json - checking for changed users";
    
    if (!jsonRoot.isArray()) {
        qDebug() << "MQTT_DEBUG: employees.json is not an array";
        return false;
    }
    
    g_jsonUserCount = jsonRoot.size();
    
    RegisteredFacesDB* db = RegisteredFacesDB::GetInstance();
    if (!db) {
        qDebug() << "MQTT_DEBUG: Database not available";
        return false;
    }
    
    QList<PERSONS_t> localUsers = db->GetAllPersonFromRAM();
    QMap<QString, PERSONS_t> localUserMap;
    for (const auto& user : localUsers) {
        if (!user.idcard.isEmpty()) {
            localUserMap[user.idcard] = user;
        }
    }
    
    int updatedCount = 0;
    int addedCount = 0;
    int skippedCount = 0;
    
    for (int i = 0; i < jsonRoot.size(); i++) {
        Json::Value employee = jsonRoot[i];
        
        if (!employee.isObject()) {
            skippedCount++;
            continue;
        }
        
        QString empId = QString::fromStdString(employee.get("empId", "").asString());
        QString name = QString::fromStdString(employee.get("name", "").asString());
        QString userLastModified = QString::fromStdString(employee.get("lastModified", "").asString());
        
        // ✅ EXTRACT personalModuleId from JSON
        int personalModuleId = employee.get("personalModuleId", 0).asInt();
        
        qDebug() << "MQTT_DEBUG: Employee" << empId << "personalModuleId:" << personalModuleId;
        
        if (empId.isEmpty()) {
            skippedCount++;
            continue;
        }
        
        // Check if user exists
        if (localUserMap.contains(empId)) {
            PERSONS_t localUser = localUserMap[empId];
            
            if (!userLastModified.isEmpty()) {
                qint64 serverTime = parseISO8601ToTimestamp(userLastModified);
                qint64 localTime = parseISO8601ToTimestamp(localUser.employee_json_last_modified);
                
                if (serverTime > localTime) {
                    qDebug() << "MQTT_DEBUG: Updating user" << empId << "- server is newer";
                    // ✅ PASS personalModuleId to update function
                    if (updateUserFromS3Data(empId, name, "", serverTime, personalModuleId)) {
                        updatedCount++;
                    }
                } else {
                    qDebug() << "MQTT_DEBUG: Skipping user" << empId << "- local is up to date";
                    skippedCount++;
                }
            } else {
                skippedCount++;
            }
        } else {
            // New user
            qDebug() << "MQTT_DEBUG: Adding new user" << empId;
            qint64 timestamp = parseISO8601ToTimestamp(userLastModified);
            // ✅ PASS personalModuleId to add function
            if (addNewUserFromS3Data(empId, name, "", timestamp, personalModuleId)) {
                addedCount++;
            }
        }
    }
    
    qDebug() << "MQTT_DEBUG: Employees sync complete - Added:" << addedCount 
             << "Updated:" << updatedCount << "Skipped:" << skippedCount;
    
    // Update local timestamp
    m_localJsonTimestamps["employees.json"] = serverLastModified;
    db->UpdateJsonLastModifiedTimestamps(serverLastModified, m_localJsonTimestamps.value("faces.json", ""));
    
    return true;
}

extern int g_jsonFaceCount;

bool MqttHeartbeatManager::syncFacesJson(const Json::Value& jsonRoot, const QString& serverLastModified)
{
    qDebug() << "MQTT_DEBUG: Syncing faces.json - checking for changed faces";
    
    if (!jsonRoot.isArray()) {
        qDebug() << "MQTT_DEBUG: faces.json is not an array";
        return false;
    }
    
    g_jsonFaceCount = jsonRoot.size();
    
    RegisteredFacesDB* db = RegisteredFacesDB::GetInstance();
    if (!db) {
        qDebug() << "MQTT_DEBUG: Database not available";
        return false;
    }
    
    int updatedCount = 0;
    int skippedCount = 0;
    
    for (int i = 0; i < jsonRoot.size(); i++) {
        Json::Value face = jsonRoot[i];
        
        if (!face.isObject()) {
            skippedCount++;
            continue;
        }
        
        QString empId = QString::fromStdString(face.get("empid", "").asString());
        QString embedding = QString::fromStdString(face.get("embedding", "").asString());
        QString faceLastModified = QString::fromStdString(face.get("last_modified", "").asString());
        int personalModuleId = 0;

        if (empId.isEmpty() || embedding.isEmpty()) {
            skippedCount++;
            continue;
        }
        
        // Check if this face was modified recently
        if (!faceLastModified.isEmpty()) {
            qint64 faceTime = parseISO8601ToTimestamp(faceLastModified);
            qint64 localTime = parseISO8601ToTimestamp(m_localJsonTimestamps.value("faces.json", ""));
            
            if (faceTime > localTime) {
                qDebug() << "MQTT_DEBUG: Updating face for" << empId;
                if (updateUserFromS3Data(empId, "", embedding, faceTime, personalModuleId)) {
                    updatedCount++;
                }
            } else {
                skippedCount++;
            }
        }
    }
    
    qDebug() << "MQTT_DEBUG: Faces sync complete - Updated:" << updatedCount << "Skipped:" << skippedCount;
    
    // Update local timestamp
    m_localJsonTimestamps["faces.json"] = serverLastModified;
    db->UpdateJsonLastModifiedTimestamps(m_localJsonTimestamps.value("employees.json", ""), serverLastModified);
    
    return true;
}

bool MqttHeartbeatManager::syncRfidJson(const Json::Value& jsonRoot, const QString& serverLastModified)
{
    qDebug() << "MQTT_DEBUG: ========== SYNCING RFID.JSON (FLAT ARRAY FORMAT) ==========";
    qDebug() << "MQTT_DEBUG: Server Last Modified:" << serverLastModified;
    printf("MQTT_PRINTF: Syncing RFID.json - Server timestamp: %s\n", serverLastModified.toStdString().c_str());
    fflush(stdout);
    
    if (!jsonRoot.isArray()) {
        qDebug() << "MQTT_DEBUG: rfid.json is not an array";
        return false;
    }
    
    RegisteredFacesDB* db = RegisteredFacesDB::GetInstance();
    if (!db) {
        qDebug() << "MQTT_DEBUG: Database not available";
        return false;
    }
    
    // Get local RFID timestamp
    QString localRfidTimestamp = m_localJsonTimestamps.value("rfid.json", "");
    qint64 localTime = parseISO8601ToTimestamp(localRfidTimestamp);
    qint64 serverTime = parseISO8601ToTimestamp(serverLastModified);
    
    qDebug() << "MQTT_DEBUG: Local RFID timestamp:" << localRfidTimestamp;
    qDebug() << "MQTT_DEBUG: Local time (epoch):" << localTime;
    qDebug() << "MQTT_DEBUG: Server time (epoch):" << serverTime;
    
    // Check if we need to update
    if (serverTime <= localTime && !localRfidTimestamp.isEmpty()) {
        qDebug() << "MQTT_DEBUG: Server RFID data is not newer - skipping entire sync";
        printf("MQTT_PRINTF: RFID data is up to date - no sync needed\n");
        fflush(stdout);
        return true;
    }
    
    // Build a map of Employee ID -> List of RFID cards
    QMap<QString, QStringList> employeeRfidMap;
    int totalCards = jsonRoot.size();
    int assignedCards = 0;
    int unassignedCards = 0;
    int invalidCards = 0;
    
    qDebug() << "MQTT_DEBUG: Processing" << totalCards << "RFID cards from JSON";
    
    for (int i = 0; i < jsonRoot.size(); i++) {
        QString rfidEntry = QString::fromStdString(jsonRoot[i].asString());
        
        if (rfidEntry.isEmpty()) {
            invalidCards++;
            continue;
        }
        
        // Parse format: "HEX_CARD:EMPLOYEE_ID" or "HEX_CARD:-"
        QStringList parts = rfidEntry.split(":");
        if (parts.size() != 2) {
            qDebug() << "MQTT_DEBUG: Invalid RFID format:" << rfidEntry;
            invalidCards++;
            continue;
        }
        
        QString cardHex = parts[0].trimmed();
        
        // The server sends the RFID in hex format with the last two digits being access level and door config.
        // We must remove these last 2 digits to get the actual RFID hex.
        if (cardHex.length() > 2) {
            cardHex = cardHex.left(cardHex.length() - 2);
        }
        
        // Convert the HEX string to a decimal digit string, as the reader outputs decimal digits
        bool ok = false;
        qint64 decimalValue = cardHex.toLongLong(&ok, 16);
        if (ok) {
            cardHex = QString::number(decimalValue);
        }
        
        QString employeeId = parts[1].trimmed();
        
        // Skip invalid/zero cards
        if (cardHex.isEmpty() || cardHex == "00000000") {
            invalidCards++;
            continue;
        }
        
        // Check if card is assigned to an employee
        if (employeeId == "-" || employeeId.isEmpty()) {
            unassignedCards++;
            qDebug() << "MQTT_DEBUG:   Card" << cardHex << "is unassigned";
            continue;
        }
        
        // Add card to employee's list
        if (!employeeRfidMap.contains(employeeId)) {
            employeeRfidMap[employeeId] = QStringList();
        }
        employeeRfidMap[employeeId].append(cardHex);
        assignedCards++;
        
        qDebug() << "MQTT_DEBUG:   Card" << cardHex << "→ Employee" << employeeId;
    }
    
    qDebug() << "MQTT_DEBUG: ========== RFID PROCESSING SUMMARY ==========";
    qDebug() << "MQTT_DEBUG: Total cards processed:" << totalCards;
    qDebug() << "MQTT_DEBUG: Assigned cards:" << assignedCards;
    qDebug() << "MQTT_DEBUG: Unassigned cards:" << unassignedCards;
    qDebug() << "MQTT_DEBUG: Invalid/Zero cards:" << invalidCards;
    qDebug() << "MQTT_DEBUG: Unique employees with RFID:" << employeeRfidMap.size();
    
    printf("MQTT_PRINTF: RFID Summary - Total: %d, Assigned: %d, Unassigned: %d, Employees: %d\n",
           totalCards, assignedCards, unassignedCards, employeeRfidMap.size());
    fflush(stdout);
    
    // Load all users from database for validation
    QList<PERSONS_t> localUsers = db->GetAllPersonFromRAM();
    QMap<QString, PERSONS_t> localUserMap;
    for (const auto& user : localUsers) {
        if (!user.idcard.isEmpty()) {
            localUserMap[user.idcard] = user;
        }
    }
    
    // First pass: Clear all existing RFID cards (since we're doing a full sync)
    qDebug() << "MQTT_DEBUG: Clearing existing RFID data for fresh sync...";
    db->ClearAllRfidCards();
    
    // Update RFID cards for each employee
    int updatedCount = 0;
    int notFoundCount = 0;
    
    QMapIterator<QString, QStringList> it(employeeRfidMap);
    while (it.hasNext()) {
        it.next();
        QString employeeId = it.key();
        QStringList rfidCards = it.value();
        
        qDebug() << "MQTT_DEBUG: Processing employee" << employeeId << "with" << rfidCards.size() << "cards";
        
        // Check if employee exists
        if (!localUserMap.contains(employeeId)) {
            qDebug() << "MQTT_DEBUG: WARNING - Employee" << employeeId << "not found in database";
            notFoundCount++;
            continue;
        }
        
        // Update RFID cards for this employee
        if (updateUserRfidCards(employeeId, rfidCards)) {
            updatedCount++;
            printf("MQTT_PRINTF: Updated RFID for %s with %d cards: %s\n", 
                   employeeId.toStdString().c_str(), 
                   rfidCards.size(),
                   rfidCards.join(",").toStdString().c_str());
            fflush(stdout);
        }
    }
    
    qDebug() << "MQTT_DEBUG: ========== RFID SYNC COMPLETE ==========";
    qDebug() << "MQTT_DEBUG: Employees updated:" << updatedCount;
    qDebug() << "MQTT_DEBUG: Employees not found:" << notFoundCount;
    
    printf("MQTT_PRINTF: RFID sync complete - Updated: %d, Not Found: %d\n", 
           updatedCount, notFoundCount);
    fflush(stdout);
    
    // Update local timestamp
    m_localJsonTimestamps["rfid.json"] = serverLastModified;
    
    // Store the updated timestamp in database
    db->UpdateRfidJsonTimestamp(serverLastModified);
    
    return true;
}

bool MqttHeartbeatManager::updateUserRfidCards(const QString& employeeId, const QStringList& rfidCards)
{
    qDebug() << "MQTT_DEBUG: Updating RFID cards for employee" << employeeId;
    
    RegisteredFacesDB* db = RegisteredFacesDB::GetInstance();
    if (!db) {
        qDebug() << "MQTT_DEBUG: Database not available";
        return false;
    }
    
    // Get current user data to verify existence
    QList<PERSONS_t> allPersons = db->GetAllPersonFromRAM();
    bool userFound = false;
    
    for (const auto& person : allPersons) {
        if (person.idcard == employeeId) {
            userFound = true;
            break;
        }
    }
    
    if (!userFound) {
        qDebug() << "MQTT_DEBUG: Employee" << employeeId << "not found in database";
        return false;
    }
    
    // Format RFID cards: store comma-separated hex values
    QString cardNumbers = rfidCards.join(",");
    
    qDebug() << "MQTT_DEBUG: Storing" << rfidCards.size() << "RFID cards:" << cardNumbers;
    
    // Update database
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    query.prepare("UPDATE person SET iccardnum = ? WHERE idcardnum = ?");
    query.bindValue(0, cardNumbers);
    query.bindValue(1, employeeId);
    
    if (!query.exec()) {
        QSqlError error = query.lastError();
        qDebug() << "MQTT_DEBUG: Failed to update RFID in database:" << error.text();
        return false;
    }
    
    qDebug() << "MQTT_DEBUG: Successfully updated RFID for" << employeeId;
    return true;
}

QString MqttHeartbeatManager::hexToBase64ForNetwork(const QByteArray& hexData)
{
    qDebug() << "NETWORK_CONVERT: HEX → Base64 (for transmission)";
    qDebug() << "  Input HEX size:" << hexData.size() << "bytes";
    
    QString base64String = QString(hexData.toBase64());
    
    qDebug() << "  Output Base64:" << base64String.length() << "chars";
    return base64String;
}

QByteArray MqttHeartbeatManager::base64FromNetworkToHex(const QString& base64String)
{
    qDebug() << "NETWORK_CONVERT: Base64 → HEX (from transmission)";
    qDebug() << "  Input Base64:" << base64String.length() << "chars";
    
    QString cleanBase64 = base64String;
    cleanBase64.remove(QRegExp("\\s"));
    
    QByteArray hexData = QByteArray::fromBase64(cleanBase64.toLatin1());
    
    qDebug() << "  Output HEX size:" << hexData.size() << "bytes";
    return hexData;
}

bool MqttHeartbeatManager::enrollFingerprintFromSensor(const QString& employeeId, 
                                                       uint16_t fingerId)
{
    qDebug() << "═══════════════════════════════════════════════════════";
    qDebug() << "ENROLLMENT: Starting for employee" << employeeId << "fingerId" << fingerId;
    qDebug() << "═══════════════════════════════════════════════════════";
    
    RegisteredFacesDB* db = RegisteredFacesDB::GetInstance();
    if (!db) {
        qDebug() << "ENROLLMENT: ERROR - Database not available";
        return false;
    }
    
    // ========== STEP 1: Get HEX from Sensor ==========
    qDebug() << "ENROLLMENT: [Step 1] Downloading HEX from sensor...";
    
    static FingerprintManager* g_fpManager = nullptr;
if (!g_fpManager) {
    g_fpManager = new FingerprintManager();
}
FingerprintManager* fpManager = g_fpManager;
    if (!fpManager) {
        qDebug() << "ENROLLMENT: ERROR - FingerprintManager not available";
        return false;
    }
    
    QByteArray hexTemplate;  // Binary/HEX data
    if (!fpManager->downloadFingerprintTemplate(fingerId, hexTemplate)) {
        qDebug() << "ENROLLMENT: ERROR - Failed to download from sensor";
        return false;
    }
    
    qDebug() << "ENROLLMENT: ✓ Downloaded HEX:" << hexTemplate.size() << "bytes";
    qDebug() << "ENROLLMENT:   HEX preview:" << hexTemplate.left(16).toHex();
    
    // ========== STEP 2: Store HEX Directly in Database (NO CONVERSION) ==========
    qDebug() << "ENROLLMENT: [Step 2] Storing HEX in database...";
    
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    
    // Check if exists
    query.prepare("SELECT COUNT(*) FROM fingerprints WHERE idcardnum = ? AND finger_id = ?");
    query.bindValue(0, employeeId);
    query.bindValue(1, fingerId);
    
    if (!query.exec() || !query.next()) {
        qDebug() << "ENROLLMENT: ERROR - Database query failed:" << query.lastError().text();
        return false;
    }
    
    bool exists = query.value(0).toInt() > 0;
    
    if (exists) {
        // Update existing
        qDebug() << "ENROLLMENT:   Updating existing entry...";
        query.prepare("UPDATE fingerprints SET template = ?, updated_at = CURRENT_TIMESTAMP "
                     "WHERE idcardnum = ? AND finger_id = ?");
        query.bindValue(0, hexTemplate);  // ✅ Store as HEX (BLOB)
        query.bindValue(1, employeeId);
        query.bindValue(2, fingerId);
    } else {
        // Insert new
        qDebug() << "ENROLLMENT:   Inserting new entry...";
        query.prepare("INSERT INTO fingerprints (idcardnum, finger_id, template) "
                     "VALUES (?, ?, ?)");
        query.bindValue(0, employeeId);
        query.bindValue(1, fingerId);
        query.bindValue(2, hexTemplate);  // ✅ Store as HEX (BLOB)
    }
    
    if (!query.exec()) {
        qDebug() << "ENROLLMENT: ERROR - Failed to store:" << query.lastError().text();
        return false;
    }
    
    qDebug() << "ENROLLMENT: ✓ Stored HEX in database (BLOB format)";
    
    // ========== STEP 3: Convert to Base64 ONLY for Server Upload ==========
    qDebug() << "ENROLLMENT: [Step 3] Converting HEX → Base64 for server...";
    
    QString base64ForServer = hexToBase64ForNetwork(hexTemplate);
    
    qDebug() << "ENROLLMENT: ✓ Converted to Base64 for transmission";
    qDebug() << "ENROLLMENT:   Base64 length:" << base64ForServer.length() << "chars";
    qDebug() << "ENROLLMENT:   Base64 preview:" << base64ForServer.left(50) << "...";
    
    // ========== STEP 4: Send Base64 to Server ==========
    qDebug() << "ENROLLMENT: [Step 4] Preparing JSON for server upload...";
    
    // Example JSON structure:
    // {
    //   "fingerId": 62,
    //   "empId": "egwft",
    //   "template": "7wEAAAACAJmgL6hBc9iNmR..."  // Base64 for network
    // }
    
    Json::Value fingerprint;
    fingerprint["fingerId"] = fingerId;
    fingerprint["empId"] = employeeId.toStdString();
    fingerprint["template"] = base64ForServer.toStdString();  // ✅ Base64 for JSON
    
    // You would send this via MQTT or HTTP API
    // publishToMqtt("fingerprint/enroll", fingerprint);
    
    qDebug() << "ENROLLMENT: ✓ Ready to send Base64 to server";
    
    qDebug() << "═══════════════════════════════════════════════════════";
    qDebug() << "ENROLLMENT: ✓ SUCCESS";
    qDebug() << "ENROLLMENT:   Employee:" << employeeId;
    qDebug() << "ENROLLMENT:   Finger ID:" << fingerId;
    qDebug() << "ENROLLMENT:   DB Storage: HEX (" << hexTemplate.size() << "bytes)";
    qDebug() << "ENROLLMENT:   Network: Base64 (" << base64ForServer.length() << "chars)";
    qDebug() << "═══════════════════════════════════════════════════════";
    
    return true;
}

bool MqttHeartbeatManager::syncFingerprintJson(const Json::Value& jsonRoot, 
                                               const QString& serverLastModified)
{
    qDebug() << "═══════════════════════════════════════════════════════";
    qDebug() << "FINGERPRINT_SYNC: ========== SYNCING FINGERPRINT.JSON ==========";
    qDebug() << "FINGERPRINT_SYNC: Server Last Modified:" << serverLastModified;
    qDebug() << "═══════════════════════════════════════════════════════";
    
    if (!jsonRoot.isArray()) {
        qDebug() << "FINGERPRINT_SYNC: ERROR - Not an array";
        return false;
    }
    
    RegisteredFacesDB* db = RegisteredFacesDB::GetInstance();
    if (!db) {
        qDebug() << "FINGERPRINT_SYNC: ERROR - Database not available";
        return false;
    }
    
    // Check timestamp
    QString localTimestamp = m_localJsonTimestamps.value("fingerprints.json", "");
    qint64 localTime = parseISO8601ToTimestamp(localTimestamp);
    qint64 serverTime = parseISO8601ToTimestamp(serverLastModified);
    
    if (serverTime <= localTime && !localTimestamp.isEmpty()) {
        qDebug() << "FINGERPRINT_SYNC: Data is up to date - skipping";
        return true;
    }
    
    // Parse JSON and convert Base64 → HEX immediately
    QMap<QString, QList<QPair<uint16_t, QByteArray>>> employeeFingerprintMap;
    int totalFingerprints = jsonRoot.size();
    int validFingerprints = 0;
    int invalidFingerprints = 0;
    
    qDebug() << "FINGERPRINT_SYNC: Processing" << totalFingerprints << "fingerprints";
    
    for (int i = 0; i < jsonRoot.size(); i++) {
        const Json::Value& fingerEntry = jsonRoot[i];
        
        if (!fingerEntry.isObject()) {
            invalidFingerprints++;
            continue;
        }
        
        QString empId = QString::fromStdString(fingerEntry.get("empId", "").asString());
        
        // The server sends fingerId as null. We will default it to 1 since we need a valid ID
        int fingerId = fingerEntry.get("fingerId", 1).asInt();
        if (fingerId <= 0) fingerId = 1;
        
        QString templateBase64 = QString::fromStdString(
            fingerEntry.get("template", fingerEntry.get("base64Data", "")).asString());  // Try both template and base64Data
        
        if (empId.isEmpty() || templateBase64.isEmpty()) {
            printf("FINGERPRINT_SYNC: Invalid entry %d! empId='%s', fingerId=%d, templateEmpty=%d\n", 
                   i, empId.toStdString().c_str(), fingerId, templateBase64.isEmpty());
            
            // Print the raw JSON of the first invalid entry so we can see the correct keys
            if (invalidFingerprints == 0) {
                Json::FastWriter writer;
                printf("FINGERPRINT_SYNC: Raw JSON format: %s\n", writer.write(fingerEntry).c_str());
            }
            
            invalidFingerprints++;
            continue;
        }
        
        // ✅ Convert Base64 → HEX immediately
        QByteArray templateHex = base64FromNetworkToHex(templateBase64);
        
        if (templateHex.isEmpty()) {
            qDebug() << "FINGERPRINT_SYNC: Failed to convert Base64 for employee" << empId;
            printf("FINGERPRINT_SYNC: Base64 to HEX failed for empId='%s', base64 starts with: '%s'\n", 
                   empId.toStdString().c_str(), templateBase64.left(20).toStdString().c_str());
            invalidFingerprints++;
            continue;
        }
        
        if (!employeeFingerprintMap.contains(empId)) {
            employeeFingerprintMap[empId] = QList<QPair<uint16_t, QByteArray>>();
        }
        
        // Store HEX data, not Base64
        employeeFingerprintMap[empId].append(QPair<uint16_t, QByteArray>(fingerId, templateHex));
        validFingerprints++;
        
        qDebug() << "FINGERPRINT_SYNC:   Fingerprint" << fingerId << "→ Employee" << empId
                 << "(HEX:" << templateHex.size() << "bytes)";
    }
    
    qDebug() << "FINGERPRINT_SYNC: ========== SUMMARY ==========";
    qDebug() << "FINGERPRINT_SYNC: Valid fingerprints:" << validFingerprints;
    qDebug() << "FINGERPRINT_SYNC: Invalid entries:" << invalidFingerprints;
    
    // Validate employees
    QList<PERSONS_t> localUsers = db->GetAllPersonFromRAM();
    QMap<QString, PERSONS_t> localUserMap;
    for (const auto& user : localUsers) {
        if (!user.idcard.isEmpty()) {
            localUserMap[user.idcard] = user;
        }
    }
    
    // Update fingerprints
    int updatedCount = 0;
    int notFoundCount = 0;
    int uploadedToSensorCount = 0;
    
    QMapIterator<QString, QList<QPair<uint16_t, QByteArray>>> it(employeeFingerprintMap);
    while (it.hasNext()) {
        it.next();
        QString employeeId = it.key();
        QList<QPair<uint16_t, QByteArray>> fingerprints = it.value();
        
        if (!localUserMap.contains(employeeId)) {
            qDebug() << "FINGERPRINT_SYNC: Employee" << employeeId << "not found";
            notFoundCount++;
            continue;
        }
        
        bool anySuccess = false;
        for (const auto& fingerData : fingerprints) {
            uint16_t fingerId = fingerData.first;
            QByteArray templateHex = fingerData.second;  // Already HEX format
            
            if (updateUserFingerprintHex(employeeId, fingerId, templateHex)) {
                anySuccess = true;
                uploadedToSensorCount++;
            }
        }
        
        if (anySuccess) {
            updatedCount++;
        }
    }
    
    qDebug() << "═══════════════════════════════════════════════════════";
    qDebug() << "FINGERPRINT_SYNC: ========== SYNC COMPLETE ==========";
    qDebug() << "FINGERPRINT_SYNC: Employees updated:" << updatedCount;
    qDebug() << "FINGERPRINT_SYNC: Fingerprints uploaded:" << uploadedToSensorCount;
    qDebug() << "═══════════════════════════════════════════════════════";
    
    // Update timestamp
    m_localJsonTimestamps["fingerprints.json"] = serverLastModified;
    db->UpdateFingerprintJsonTimestamp(serverLastModified);
    
    return true;
}

bool MqttHeartbeatManager::updateUserFingerprintHex(const QString& employeeId, 
                                                    uint16_t fingerId,
                                                    const QByteArray& templateHex)
{
    qDebug() << "──────────────────────────────────────────────────────────";
    qDebug() << "UPDATE: Employee" << employeeId << "fingerId" << fingerId;
    qDebug() << "──────────────────────────────────────────────────────────";
    
    RegisteredFacesDB* db = RegisteredFacesDB::GetInstance();
    if (!db) {
        qDebug() << "UPDATE: ERROR - Database not available";
        return false;
    }
    
    // Verify employee exists
    QList<PERSONS_t> allPersons = db->GetAllPersonFromRAM();
    bool userFound = false;
    QString userUuid = "";
    
    for (const auto& person : allPersons) {
        if (person.idcard == employeeId) {
            userFound = true;
            userUuid = person.uuid;
            break;
        }
    }
    
    if (!userFound) {
        qDebug() << "UPDATE: ERROR - Employee not found";
        return false;
    }
    
    // ========== STEP 1: Store HEX in Database ==========
    qDebug() << "UPDATE: [Step 1] Storing HEX in database...";
    qDebug() << "UPDATE:   HEX size:" << templateHex.size() << "bytes";
    qDebug() << "UPDATE:   HEX preview:" << templateHex.left(16).toHex();
    
    bool dbSuccess = db->UpdatePersonFingerprint(userUuid, templateHex);
    bool idSuccess = db->UpdatePersonFingerId(userUuid, fingerId);
    
    if (!dbSuccess || !idSuccess) {
        qDebug() << "UPDATE: ERROR - Failed to save to database via RegisteredFacesDB";
        return false;
    }
    
    qDebug() << "UPDATE: ✓ Stored HEX in database";
    
    // ========== STEP 2: Upload HEX to Sensor (NO CONVERSION) ==========
    qDebug() << "UPDATE: [Step 2] Uploading HEX to sensor...";
    
    static FingerprintManager* g_fpManager = nullptr;
    if (!g_fpManager) {
        g_fpManager = new FingerprintManager();
    }
    FingerprintManager* fpManager = g_fpManager;
    if (!fpManager) {
        qDebug() << "UPDATE: ERROR - FingerprintManager not available";
        return false;
    }
    
    if (!fpManager->uploadTemplateToSensor(fingerId, templateHex)) {  // ✅ Upload HEX directly
        qDebug() << "UPDATE: ERROR - Upload to sensor failed";
        return false;
    }
    
    qDebug() << "UPDATE: ✓ Uploaded HEX to sensor";
    
    // ========== STEP 3: AUTO-PUBLISH TO MQTT ==========
    qDebug() << "UPDATE: [Step 3] Auto-publishing fingerprint to MQTT...";
    printf("\n");
    printf("========================================================================\n");
    printf("S3_SYNC: Auto-publishing fingerprint data for employee %s\n", 
           employeeId.toStdString().c_str());
    printf("========================================================================\n");
    fflush(stdout);
    
    // Get tenant and assignedTo
    QString tenantId = getTenantIdFromConfig();
    int assignedTo = getAssignedToForUser(employeeId);
    
    printf("S3_SYNC: Tenant: %s, AssignedTo: %d\n", 
           tenantId.toStdString().c_str(), assignedTo);
    fflush(stdout);
    
    // Publish fingerprint data (HEX will be converted to Base64 internally)
    bool mqttSuccess = publishFingerprintDataFromHex(templateHex, tenantId, employeeId, fingerId, assignedTo);
    
    if (mqttSuccess) {
        qDebug() << "UPDATE: ✓ Fingerprint published to MQTT";
        printf("S3_SYNC: ✓ Fingerprint published to MQTT for %s\n", 
               employeeId.toStdString().c_str());
    } else {
        qDebug() << "UPDATE: ✗ Failed to publish fingerprint to MQTT";
        printf("S3_SYNC: ✗ Failed to publish fingerprint to MQTT for %s\n", 
               employeeId.toStdString().c_str());
    }
    
    printf("========================================================================\n\n");
    fflush(stdout);
    
    qDebug() << "──────────────────────────────────────────────────────────";
    qDebug() << "UPDATE: ✓ SUCCESS";
    qDebug() << "UPDATE:   DB Storage: HEX (" << templateHex.size() << "bytes)";
    qDebug() << "UPDATE:   Sensor: HEX (" << templateHex.size() << "bytes)";
    qDebug() << "UPDATE:   MQTT: Published (" << (mqttSuccess ? "SUCCESS" : "FAILED") << ")";
    qDebug() << "──────────────────────────────────────────────────────────";
    
    return true;
}

QByteArray MqttHeartbeatManager::loadFingerprintFromDatabase(const QString& employeeId, 
                                                              uint16_t fingerId)
{
    qDebug() << "LOAD: Loading fingerprint from DB for" << employeeId << "fingerId" << fingerId;
    RegisteredFacesDB* db = RegisteredFacesDB::GetInstance();
    if (!db) return QByteArray();
    
    // Find UUID
    QList<PERSONS_t> allPersons = db->GetAllPersonFromRAM();
    QString userUuid = "";
    
    for (const auto& person : allPersons) {
        if (person.idcard == employeeId) {
            userUuid = person.uuid;
            break;
        }
    }
    
    if (userUuid.isEmpty()) {
        qDebug() << "LOAD: ERROR - Employee not found";
        return QByteArray();
    }
    
    QByteArray templateHex;
    if (!db->GetPersonFingerprint(userUuid, templateHex) || templateHex.isEmpty()) {
        qDebug() << "LOAD: ERROR - Fingerprint not found in DB";
        return QByteArray();
    }
    
    qDebug() << "LOAD: ✓ Loaded HEX:" << templateHex.size() << "bytes";
    qDebug() << "LOAD:   HEX preview:" << templateHex.left(16).toHex();
    
    return templateHex;
}

// ============================================================================
// HELPER FUNCTIONS FOR FACE DATA PUBLISHING
// ============================================================================

QString MqttHeartbeatManager::getTenantIdFromConfig()
{
    // Tenant ID is automatically extracted from heartbeat response
    // See handleHeartbeatResponse() - line ~1256
    if (!m_tenantId.isEmpty()) {
        return m_tenantId;
    }
    
    qDebug() << "FACE_PUBLISH: WARNING - Tenant ID not available yet from heartbeat";
    return "";
}

int MqttHeartbeatManager::getAssignedToForUser(const QString& employeeId)
{
    RegisteredFacesDB* db = RegisteredFacesDB::GetInstance();
    if (!db) {
        qDebug() << "FACE_PUBLISH: Cannot get assignedTo - database not available";
        return 0;
    }
    
    // Get user from database
    QList<PERSONS_t> allPersons = db->GetAllPersonFromRAM();
    for (const auto& person : allPersons) {
        if (person.idcard == employeeId) {
            qDebug() << "FACE_PUBLISH: Found user" << employeeId 
                     << "personalModuleId:" << person.personalModuleId
                     << "personid:" << person.personid;
            
            if (person.personalModuleId != 0) {
                printf("FACE_PUBLISH: assignedTo = personalModuleId = %d\n", person.personalModuleId);
            } else {
                printf("FACE_PUBLISH: personalModuleId is 0 for '%s'. Sending 0 as assignedTo.\n",
                       employeeId.toStdString().c_str());
            }
            return person.personalModuleId;
        }
    }
    
    qDebug() << "FACE_PUBLISH: User not found for assignedTo:" << employeeId;
    printf("FACE_PUBLISH: User '%s' not found in RAM — check idcardnum column matches what was entered in Employee ID field\n",
           employeeId.toStdString().c_str());
    return 0;
}

// ============================================================================
// FACE DATA PUBLISHING
// ============================================================================

bool MqttHeartbeatManager::publishFaceData(const QString& base64FaceData, 
                                          const QString& tenantId, 
                                          const QString& employeeId,
                                          int assignedTo)
{
    printf("\n");
    printf(">>> publishFaceData() FUNCTION CALLED <<<\n");
    printf(">>> Parameters received:\n");
    printf(">>>   - base64FaceData length: %d chars\n", base64FaceData.length());
    printf(">>>   - tenantId: '%s'\n", tenantId.toStdString().c_str());
    printf(">>>   - assignedTo: %d\n", assignedTo);
    fflush(stdout);
    
    qDebug() << "FACE_PUBLISH: Publishing face data to MQTT";
    printf("FACE_PUBLISH: Publishing face data to MQTT\n");
    fflush(stdout);
    
    // Check if main MQTT client is available
    printf("FACE_PUBLISH: Checking m_mosq and connection state...\n");
    if (!m_mosq) {
        qDebug() << "FACE_PUBLISH: ERROR - MQTT client not initialized";
        printf("FACE_PUBLISH: ERROR - m_mosq is NULL\n");
        fflush(stdout);
        return false;
    }
    if (!m_connected) {
        qDebug() << "FACE_PUBLISH: ERROR - MQTT client not connected to broker";
        printf("FACE_PUBLISH: ERROR - MQTT not connected (m_connected=false). Will not attempt publish.\n");
        fflush(stdout);
        return false;
    }
    printf("FACE_PUBLISH: ✓ m_mosq is initialized and connected\n");
    
    printf("FACE_PUBLISH: Checking base64FaceData...\n");
    if (base64FaceData.isEmpty()) {
        qDebug() << "FACE_PUBLISH: ERROR - Face data is empty";
        printf("FACE_PUBLISH: ERROR - base64FaceData is empty\n");
        fflush(stdout);
        return false;
    }
    printf("FACE_PUBLISH: ✓ base64FaceData is not empty (%d chars)\n", base64FaceData.length());
    
    printf("FACE_PUBLISH: Checking tenantId...\n");
    if (tenantId.isEmpty()) {
        qDebug() << "FACE_PUBLISH: WARNING - Tenant ID is empty, publishing anyway";
        printf("FACE_PUBLISH: WARNING - tenantId is empty, will publish with empty tenant field\n");
        fflush(stdout);
        // Do not return false here, let the server handle the empty tenant or rely on device SN
    } else {
        printf("FACE_PUBLISH: ✓ tenantId is not empty: '%s'\n", tenantId.toStdString().c_str());
    }
    
    // Use device serial number (same as used in heartbeat)
    QString deviceId = m_deviceSn;
    printf("FACE_PUBLISH: Checking deviceId...\n");
    if (deviceId.isEmpty()) {
        qDebug() << "FACE_PUBLISH: ERROR - Device SN not set";
        printf("FACE_PUBLISH: ERROR - m_deviceSn is empty\n");
        fflush(stdout);
        return false;
    }
    printf("FACE_PUBLISH: ✓ Device SN: '%s'\n", deviceId.toStdString().c_str());
    
    // Build topic: device/{DEVICE_SN}/face
    QString topic = QString("device/%1/face").arg(deviceId);
    
    // Build JSON payload
    Json::Value root;
    root["base64Data"] = base64FaceData.toStdString();
    root["tenant"] = tenantId.toStdString();
    root["empId"] = employeeId.toStdString();
    root["assignedTo"] = assignedTo;
    
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";  // Compact JSON
    std::string jsonPayload = Json::writeString(builder, root);
    
    qDebug() << "FACE_PUBLISH: ============================================";
    qDebug() << "FACE_PUBLISH: Broker:" << MQTT_BROKER;
    qDebug() << "FACE_PUBLISH: Topic:" << topic;
    qDebug() << "FACE_PUBLISH: Device SN:" << deviceId;
    qDebug() << "FACE_PUBLISH: Tenant:" << tenantId;
    qDebug() << "FACE_PUBLISH: AssignedTo:" << assignedTo;
    qDebug() << "FACE_PUBLISH: Base64 data size:" << base64FaceData.length() << "characters";
    qDebug() << "FACE_PUBLISH: Payload size:" << jsonPayload.length() << "bytes";
    
    printf("FACE_PUBLISH: ============================================\n");
    printf("FACE_PUBLISH: Publishing to broker: %s\n", MQTT_BROKER);
    printf("FACE_PUBLISH: Topic: %s\n", topic.toStdString().c_str());
    printf("FACE_PUBLISH: Device SN: %s\n", deviceId.toStdString().c_str());
    printf("FACE_PUBLISH: Tenant: %s\n", tenantId.toStdString().c_str());
    printf("FACE_PUBLISH: AssignedTo: %d\n", assignedTo);
    printf("FACE_PUBLISH: Base64 data size: %d characters\n", base64FaceData.length());
    printf("FACE_PUBLISH: Payload size: %zu bytes\n", jsonPayload.length());
    printf("FACE_PUBLISH: ============================================\n");
    fflush(stdout);
    
    // Publish using the same MQTT connection as heartbeat
    printf("FACE_PUBLISH: Calling mosquitto_publish()...\n");
    fflush(stdout);
    
    int result = mosquitto_publish(
        m_mosq,      // Use same client as heartbeat
        nullptr,     // mid - message id (can be NULL)
        topic.toStdString().c_str(),
        jsonPayload.length(),
        jsonPayload.c_str(),
        1,          // QoS 1 - at least once delivery
        false       // retain - false
    );
    
    printf("FACE_PUBLISH: mosquitto_publish() returned: %d\n", result);
    fflush(stdout);
    
    if (result == MOSQ_ERR_SUCCESS) {
        qDebug() << "FACE_PUBLISH: ✓ Successfully published face data";
        printf("FACE_PUBLISH: ✓✓✓ Successfully published (MOSQ_ERR_SUCCESS)\n");
    } else {
        qDebug() << "FACE_PUBLISH: ERROR - Failed to publish:" << mosquitto_strerror(result);
        printf("FACE_PUBLISH: ERROR - Publish failed with code %d: %s\n", result, mosquitto_strerror(result));
    }
    qDebug() << "FACE_PUBLISH: ============================================";
    printf("FACE_PUBLISH: ============================================\n");
    fflush(stdout);
    
    return (result == MOSQ_ERR_SUCCESS);
}

bool MqttHeartbeatManager::publishFaceDataFromImage(const QImage& faceImage,
                                                    const QString& tenantId,
                                                    const QString& employeeId,
                                                    int assignedTo)
{
    qDebug() << "FACE_PUBLISH: Converting image to base64...";
    
    if (faceImage.isNull()) {
        qDebug() << "FACE_PUBLISH: ERROR - Face image is null";
        return false;
    }
    
    // Convert QImage to base64
    QByteArray byteArray;
    QBuffer buffer(&byteArray);
    buffer.open(QIODevice::WriteOnly);
    
    // Save as JPEG with good quality
    if (!faceImage.save(&buffer, "JPEG", 85)) {
        qDebug() << "FACE_PUBLISH: ERROR - Failed to convert image to JPEG";
        return false;
    }
    
    QString base64Data = QString::fromLatin1(byteArray.toBase64());
    
    qDebug() << "FACE_PUBLISH: Image converted to base64, size:" << base64Data.length();
    
    // Publish using the base64 data
    return publishFaceData(base64Data, tenantId, employeeId, assignedTo);
}

bool MqttHeartbeatManager::publishFaceDataFromFile(const QString& imagePath,
                                                   const QString& tenantId,
                                                   const QString& employeeId,
                                                   int assignedTo)
{
    qDebug() << "FACE_PUBLISH: Loading image from file:" << imagePath;
    
    QImage faceImage(imagePath);
    if (faceImage.isNull()) {
        qDebug() << "FACE_PUBLISH: ERROR - Failed to load image from:" << imagePath;
        return false;
    }
    
    return publishFaceDataFromImage(faceImage, tenantId, employeeId, assignedTo);
}

bool MqttHeartbeatManager::publishFingerprintData(const QString& base64FingerprintData, 
                                                  const QString& tenantId, 
                                                  const QString& employeeId,
                                                  uint16_t fingerId,
                                                  int assignedTo)
{
    printf("\n");
    printf(">>> publishFingerprintData() FUNCTION CALLED <<<\n");
    printf(">>> Parameters received:\n");
    printf(">>>   - base64FingerprintData length: %d chars\n", base64FingerprintData.length());
    printf(">>>   - tenantId: '%s'\n", tenantId.toStdString().c_str());
    printf(">>>   - assignedTo: %d\n", assignedTo);
    fflush(stdout);
    
    qDebug() << "FINGERPRINT_PUBLISH: Publishing fingerprint data to MQTT";
    printf("FINGERPRINT_PUBLISH: Publishing fingerprint data to MQTT\n");
    fflush(stdout);
    
    // Check if main MQTT client is available
    printf("FINGERPRINT_PUBLISH: Checking m_mosq and connection state...\n");
    if (!m_mosq) {
        qDebug() << "FINGERPRINT_PUBLISH: ERROR - MQTT client not initialized";
        printf("FINGERPRINT_PUBLISH: ERROR - m_mosq is NULL\n");
        fflush(stdout);
        return false;
    }
    if (!m_connected) {
        qDebug() << "FINGERPRINT_PUBLISH: ERROR - MQTT client not connected to broker";
        printf("FINGERPRINT_PUBLISH: ERROR - MQTT not connected (m_connected=false). Will not attempt publish.\n");
        fflush(stdout);
        return false;
    }
    printf("FINGERPRINT_PUBLISH: ✓ m_mosq is initialized and connected\n");
    
    printf("FINGERPRINT_PUBLISH: Checking base64FingerprintData...\n");
    if (base64FingerprintData.isEmpty()) {
        qDebug() << "FINGERPRINT_PUBLISH: ERROR - Fingerprint data is empty";
        printf("FINGERPRINT_PUBLISH: ERROR - base64FingerprintData is empty\n");
        fflush(stdout);
        return false;
    }
    printf("FINGERPRINT_PUBLISH: ✓ base64FingerprintData is not empty (%d chars)\n", base64FingerprintData.length());
    
    printf("FINGERPRINT_PUBLISH: Checking tenantId...\n");
    if (tenantId.isEmpty()) {
        qDebug() << "FINGERPRINT_PUBLISH: WARNING - Tenant ID is empty, publishing anyway";
        printf("FINGERPRINT_PUBLISH: WARNING - tenantId is empty, will publish with empty tenant field\n");
        fflush(stdout);
        // Do not return false here, let the server handle the empty tenant or rely on device SN
    } else {
        printf("FINGERPRINT_PUBLISH: ✓ tenantId is not empty: '%s'\n", tenantId.toStdString().c_str());
    }
    
    // Use device serial number (same as used in heartbeat)
    QString deviceId = m_deviceSn;
    printf("FINGERPRINT_PUBLISH: Checking deviceId...\n");
    if (deviceId.isEmpty()) {
        qDebug() << "FINGERPRINT_PUBLISH: ERROR - Device SN not set";
        printf("FINGERPRINT_PUBLISH: ERROR - m_deviceSn is empty\n");
        fflush(stdout);
        return false;
    }
    printf("FINGERPRINT_PUBLISH: ✓ Device SN: '%s'\n", deviceId.toStdString().c_str());
    
    // Build topic: device/{DEVICE_SN}/finger
    QString topic = QString("device/%1/finger").arg(deviceId);
    
    // Build JSON payload
    Json::Value root;
    root["base64Data"] = base64FingerprintData.toStdString();
    root["template"] = base64FingerprintData.toStdString(); // Send both keys just in case
    root["tenant"] = tenantId.toStdString();
    root["empId"] = employeeId.toStdString();
    root["fingerId"] = static_cast<int>(fingerId);
    root["assignedTo"] = assignedTo;
    root["deviceId"] = deviceId.toStdString(); // Sometimes servers need device ID in payload
    
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";  // Compact JSON
    std::string jsonPayload = Json::writeString(builder, root);
    
    qDebug() << "FINGERPRINT_PUBLISH: ============================================";
    qDebug() << "FINGERPRINT_PUBLISH: Broker:" << MQTT_BROKER;
    qDebug() << "FINGERPRINT_PUBLISH: Topic:" << topic;
    qDebug() << "FINGERPRINT_PUBLISH: Device SN:" << deviceId;
    qDebug() << "FINGERPRINT_PUBLISH: Tenant:" << tenantId;
    qDebug() << "FINGERPRINT_PUBLISH: AssignedTo:" << assignedTo;
    qDebug() << "FINGERPRINT_PUBLISH: Base64 data size:" << base64FingerprintData.length() << "characters";
    qDebug() << "FINGERPRINT_PUBLISH: Payload size:" << jsonPayload.length() << "bytes";
    
    printf("FINGERPRINT_PUBLISH: ============================================\n");
    printf("FINGERPRINT_PUBLISH: Publishing to broker: %s\n", MQTT_BROKER);
    printf("FINGERPRINT_PUBLISH: Topic: %s\n", topic.toStdString().c_str());
    printf("FINGERPRINT_PUBLISH: Device SN: %s\n", deviceId.toStdString().c_str());
    printf("FINGERPRINT_PUBLISH: Tenant: %s\n", tenantId.toStdString().c_str());
    printf("FINGERPRINT_PUBLISH: AssignedTo: %d\n", assignedTo);
    printf("FINGERPRINT_PUBLISH: Base64 data size: %d characters\n", base64FingerprintData.length());
    printf("FINGERPRINT_PUBLISH: Payload size: %zu bytes\n", jsonPayload.length());
    printf("FINGERPRINT_PUBLISH: EXACT JSON PAYLOAD BEING SENT:\n");
    printf("%s\n", jsonPayload.c_str());
    printf("FINGERPRINT_PUBLISH: ============================================\n");
    fflush(stdout);
    
    // Publish using the same MQTT connection as heartbeat
    printf("FINGERPRINT_PUBLISH: Calling mosquitto_publish()...\n");
    fflush(stdout);
    
    int result = mosquitto_publish(
        m_mosq,      // Use same client as heartbeat
        nullptr,     // mid - message id (can be NULL)
        topic.toStdString().c_str(),
        jsonPayload.length(),
        jsonPayload.c_str(),
        1,          // QoS 1 - at least once delivery
        false       // retain - false
    );
    
    printf("FINGERPRINT_PUBLISH: mosquitto_publish() returned: %d\n", result);
    fflush(stdout);
    
    if (result == MOSQ_ERR_SUCCESS) {
        qDebug() << "FINGERPRINT_PUBLISH: ✓ Successfully published fingerprint data";
        printf("FINGERPRINT_PUBLISH: ✓✓✓ Successfully published (MOSQ_ERR_SUCCESS)\n");
    } else {
        qDebug() << "FINGERPRINT_PUBLISH: ERROR - Failed to publish:" << mosquitto_strerror(result);
        printf("FINGERPRINT_PUBLISH: ERROR - Publish failed with code %d: %s\n", result, mosquitto_strerror(result));
    }
    qDebug() << "FINGERPRINT_PUBLISH: ============================================";
    printf("FINGERPRINT_PUBLISH: ============================================\n");
    fflush(stdout);
    
    return (result == MOSQ_ERR_SUCCESS);
}

bool MqttHeartbeatManager::publishFingerprintDataFromHex(const QByteArray& hexTemplate,
                                                         const QString& tenantId,
                                                         const QString& employeeId,
                                                         uint16_t fingerId,
                                                         int assignedTo)
{
    qDebug() << "FINGERPRINT_PUBLISH: Converting HEX template to base64...";
    printf("FINGERPRINT_PUBLISH: Converting HEX template to base64...\n");
    fflush(stdout);
    
    if (hexTemplate.isEmpty()) {
        qDebug() << "FINGERPRINT_PUBLISH: ERROR - HEX template is empty";
        printf("FINGERPRINT_PUBLISH: ERROR - HEX template is empty\n");
        fflush(stdout);
        return false;
    }
    
    // Convert HEX to Base64 using existing helper function
    QString base64Data = hexToBase64ForNetwork(hexTemplate);
    
    qDebug() << "FINGERPRINT_PUBLISH: HEX template converted to base64, size:" << base64Data.length();
    printf("FINGERPRINT_PUBLISH: HEX size: %d bytes → Base64 size: %d chars\n", 
           hexTemplate.size(), base64Data.length());
    fflush(stdout);
    
    // Publish using the base64 data
    return publishFingerprintData(base64Data, tenantId, employeeId, fingerId, assignedTo);
}

bool MqttHeartbeatManager::syncFourDoorControllerJson(const Json::Value& jsonRoot, const QString& serverLastModified)
{
    qDebug() << "═══════════════════════════════════════════════════════════";
    qDebug() << "FOUR_DOOR_SYNC: ========== SYNCING FOUR_DOOR_CONTROLLER.JSON ==========";
    qDebug() << "FOUR_DOOR_SYNC: Server Last Modified:" << serverLastModified;
    qDebug() << "═══════════════════════════════════════════════════════════";
    
    if (!jsonRoot.isArray()) {
        qDebug() << "FOUR_DOOR_SYNC: ERROR - Not an array";
        return false;
    }
    
    RegisteredFacesDB* db = RegisteredFacesDB::GetInstance();
    if (!db) {
        qDebug() << "FOUR_DOOR_SYNC: ERROR - Database not available";
        return false;
    }
    
    // Check timestamp
    QString localTimestamp = m_localJsonTimestamps.value("four_door_controller.json", "");
    qint64 localTime = parseISO8601ToTimestamp(localTimestamp);
    qint64 serverTime = parseISO8601ToTimestamp(serverLastModified);
    
    if (serverTime <= localTime && !localTimestamp.isEmpty()) {
        qDebug() << "FOUR_DOOR_SYNC: Data is up to date - skipping";
        return true;
    }
    
    // Build a map of Employee ID -> QR Code
    QMap<QString, QString> employeeQrMap;
    int totalEntries = jsonRoot.size();
    int validEntries = 0;
    int unassignedQr = 0;
    int invalidEntries = 0;
    
    qDebug() << "FOUR_DOOR_SYNC: Processing" << totalEntries << "QR code entries from JSON";
    
    for (int i = 0; i < jsonRoot.size(); i++) {
        QString qrEntry = QString::fromStdString(jsonRoot[i].asString());
        
        if (qrEntry.isEmpty()) {
            invalidEntries++;
            continue;
        }
        
        // Parse format: "QR_CODE:EMPLOYEE_ID" or "QR_CODE:-"
        QStringList parts = qrEntry.split(":");
        if (parts.size() != 2) {
            qDebug() << "FOUR_DOOR_SYNC: Invalid QR format:" << qrEntry;
            invalidEntries++;
            continue;
        }
        
        QString qrCode = parts[0].trimmed();
        QString employeeId = parts[1].trimmed();
        
        // Skip invalid/empty QR codes
        if (qrCode.isEmpty()) {
            invalidEntries++;
            continue;
        }
        
        // Check if QR is assigned to an employee
        if (employeeId == "-" || employeeId.isEmpty()) {
            unassignedQr++;
            qDebug() << "FOUR_DOOR_SYNC:   QR" << qrCode << "is unassigned";
            continue;
        }
        
        // Add QR to employee's data
        employeeQrMap[employeeId] = qrCode;
        validEntries++;
        
        qDebug() << "FOUR_DOOR_SYNC:   QR" << qrCode << "→ Employee" << employeeId;
    }
    
    qDebug() << "FOUR_DOOR_SYNC: ========== QR PROCESSING SUMMARY ==========";
    qDebug() << "FOUR_DOOR_SYNC: Total entries processed:" << totalEntries;
    qDebug() << "FOUR_DOOR_SYNC: Valid QR assignments:" << validEntries;
    qDebug() << "FOUR_DOOR_SYNC: Unassigned QR codes:" << unassignedQr;
    qDebug() << "FOUR_DOOR_SYNC: Invalid entries:" << invalidEntries;
    qDebug() << "FOUR_DOOR_SYNC: Unique employees with QR:" << employeeQrMap.size();
    
    printf("MQTT_PRINTF: QR Summary - Total: %d, Valid: %d, Unassigned: %d, Employees: %d\n",
           totalEntries, validEntries, unassignedQr, employeeQrMap.size());
    fflush(stdout);
    
    // Load all users from database for validation
    QList<PERSONS_t> localUsers = db->GetAllPersonFromRAM();
    QMap<QString, PERSONS_t> localUserMap;
    for (const auto& user : localUsers) {
        if (!user.idcard.isEmpty()) {
            localUserMap[user.idcard] = user;
        }
    }
    
    // First pass: Clear all existing QR codes (fresh sync)
    qDebug() << "FOUR_DOOR_SYNC: Clearing existing QR data for fresh sync...";
    
    QSqlQuery clearQuery(QSqlDatabase::database("isc_arcsoft_face"));
    clearQuery.prepare("UPDATE person SET qr_code = NULL");
    if (!clearQuery.exec()) {
        qDebug() << "FOUR_DOOR_SYNC: WARNING - Failed to clear QR codes:" << clearQuery.lastError().text();
    }
    
    // Update QR codes for each employee
    int updatedCount = 0;
    int notFoundCount = 0;
    
    QMapIterator<QString, QString> it(employeeQrMap);
    while (it.hasNext()) {
        it.next();
        QString employeeId = it.key();
        QString qrCode = it.value();
        
        qDebug() << "FOUR_DOOR_SYNC: Processing employee" << employeeId << "with QR:" << qrCode;
        
        // Check if employee exists
        if (!localUserMap.contains(employeeId)) {
            qDebug() << "FOUR_DOOR_SYNC: WARNING - Employee" << employeeId << "not found in database";
            notFoundCount++;
            continue;
        }
        
        // Update QR code for this employee
        if (updateUserQrCode(employeeId, qrCode)) {
            updatedCount++;
            printf("MQTT_PRINTF: Updated QR for %s: %s\n", 
                   employeeId.toStdString().c_str(), 
                   qrCode.toStdString().c_str());
            fflush(stdout);
        }
    }
    
    // Handle employees NOT in the QR list (set their QR to NULL)
    int clearedCount = 0;
    for (const auto& user : localUsers) {
        if (!user.idcard.isEmpty() && !employeeQrMap.contains(user.idcard)) {
            if (updateUserQrCode(user.idcard, QString())) {  // Empty string = NULL
                clearedCount++;
                qDebug() << "FOUR_DOOR_SYNC: Cleared QR for employee" << user.idcard;
            }
        }
    }
    
    qDebug() << "FOUR_DOOR_SYNC: ========== FOUR DOOR SYNC COMPLETE ==========";
    qDebug() << "FOUR_DOOR_SYNC: Employees updated:" << updatedCount;
    qDebug() << "FOUR_DOOR_SYNC: Employees not found:" << notFoundCount;
    qDebug() << "FOUR_DOOR_SYNC: QR codes cleared:" << clearedCount;
    
    printf("MQTT_PRINTF: Four Door sync complete - Updated: %d, Not Found: %d, Cleared: %d\n", 
           updatedCount, notFoundCount, clearedCount);
    fflush(stdout);
    
    // Update local timestamp
    m_localJsonTimestamps["four_door_controller.json"] = serverLastModified;
    
    // Store the updated timestamp in database
    db->UpdateFourDoorJsonTimestamp(serverLastModified);
    
    return true;
}

bool MqttHeartbeatManager::updateUserQrCode(const QString& employeeId, const QString& qrCode)
{
    qDebug() << "FOUR_DOOR_SYNC: Updating QR code for employee" << employeeId;
    
    RegisteredFacesDB* db = RegisteredFacesDB::GetInstance();
    if (!db) {
        qDebug() << "FOUR_DOOR_SYNC: Database not available";
        return false;
    }
    
    // Get current user data to verify existence
    QList<PERSONS_t> allPersons = db->GetAllPersonFromRAM();
    bool userFound = false;
    
    for (const auto& person : allPersons) {
        if (person.idcard == employeeId) {
            userFound = true;
            break;
        }
    }
    
    if (!userFound) {
        qDebug() << "FOUR_DOOR_SYNC: Employee" << employeeId << "not found in database";
        return false;
    }
    
    // Format QR code: store as-is or NULL
    QString qrValue = qrCode.isEmpty() ? QString() : qrCode;
    
    if (!qrCode.isEmpty()) {
        qDebug() << "FOUR_DOOR_SYNC: Storing QR code:" << qrCode;
    } else {
        qDebug() << "FOUR_DOOR_SYNC: Clearing QR code (NULL)";
    }
    
    // Update database
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    query.prepare("UPDATE person SET qr_code = ? WHERE idcardnum = ?");
    query.bindValue(0, qrValue.isEmpty() ? QVariant(QVariant::String) : qrValue);
    query.bindValue(1, employeeId);
    
    if (!query.exec()) {
        QSqlError error = query.lastError();
        qDebug() << "FOUR_DOOR_SYNC: Failed to update QR in database:" << error.text();
        return false;
    }
    
    qDebug() << "FOUR_DOOR_SYNC: Successfully updated QR for" << employeeId;
    return true;
}
