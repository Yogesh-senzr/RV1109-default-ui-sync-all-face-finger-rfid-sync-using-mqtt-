#ifndef MQTTHEARTBEATMANAGER_H
#define MQTTHEARTBEATMANAGER_H

#include <QObject>
#include <QTimer>
#include <QMutex>
#include <QString>
#include <QMap>        // Add this line
#include <QStringList> // Add this too if not present
#include <mosquitto.h>
#include <QThread>

namespace Json {
    class Value;
}

// ============================================================================
// FORWARD DECLARATION - This solves the circular dependency
// ============================================================================
class MqttHeartbeatManager;

// ============================================================================
// S3SyncWorker - Now it knows MqttHeartbeatManager exists
// ============================================================================
class S3SyncWorker : public QObject
{
    Q_OBJECT

public:
    S3SyncWorker(MqttHeartbeatManager* manager);
    ~S3SyncWorker();

public slots:
    void doSync();

signals:
    void syncFinished(bool success);

private:
    MqttHeartbeatManager* m_manager;
};

// ============================================================================
// MqttHeartbeatManager - Full declaration
// ============================================================================
class MqttHeartbeatManager : public QObject
{
    Q_OBJECT

public:
    static MqttHeartbeatManager* GetInstance();
    bool initialize();
    void setHeartbeatInterval(int seconds);
    
    bool publishHeartbeat();
    bool publishConfigRequest(const QStringList& files);
    void handleConfigResponse(const QString& payload);
    bool downloadJsonFromUrl(const QString& url, std::string& jsonContent);
    bool syncSpecificJson(const QString& filename, const QString& jsonUrl, const QString& lastModified);
    void loadLocalJsonTimestamps();
    bool publishFaceDataFromImage(const QImage& faceImage,
                                  const QString& tenantId,
                                  const QString& employeeId,
                                  int assignedTo);
    bool publishFaceDataFromFile(const QString& imagePath,
                                 const QString& tenantId,
                                 const QString& employeeId,
                                 int assignedTo);
      QString getTenantIdFromConfig();
    int getAssignedToForUser(const QString& employeeId);
    bool publishFaceData(const QString& base64FaceData, 
                        const QString& tenantId, 
                        const QString& employeeId,
                        int assignedTo);
    bool publishFingerprintData(const QString& base64FingerprintData, const QString& tenantId, const QString& employeeId, uint16_t fingerId, int assignedTo);
    bool publishFingerprintDataFromHex(const QByteArray& hexTemplate, const QString& tenantId, const QString& employeeId, uint16_t fingerId, int assignedTo);
    bool syncFourDoorControllerJson(const Json::Value& jsonRoot, const QString& serverLastModified);
    bool updateUserQrCode(const QString& employeeId, const QString& qrCode);
    
    ~MqttHeartbeatManager();

private:
    explicit MqttHeartbeatManager(QObject *parent = nullptr);
    
    // ========================================================================
    // MQTT METHODS
    // ========================================================================
    bool parseUrl(const QString& url, QString& host, int& port);
    void handleCommand(const QString& payload);
    void handleHeartbeatResponse(const QString& payload);
    void checkIfSyncNeeded(const QString& serverEmployeeTimestamp, 
                          const QString& serverFaceTimestamp);
    
    bool downloadS3JsonFile(const QString& filename, std::string& jsonContent);
    qint64 parseISO8601ToTimestamp(const QString& dateTimeStr);
    bool updateUserFromS3Data(const QString& employeeId,
                                                const QString& name,
                                                const QString& faceEmbeddingUrl,
                                                qint64 lastModified,
                                                int personalModuleId);
    bool addNewUserFromS3Data(const QString& empId,
                                                const QString& name,
                                                const QString& faceEmbedding,
                                                qint64 lastModified,
                                                int personalModuleId);
    bool downloadFaceImage(const QString& imageUrl, std::string& imageData);
    QStringList getChangedJsonFiles(const Json::Value& heartbeatFiles);
    bool syncEmployeesJson(const Json::Value& jsonRoot, const QString& serverLastModified);
    bool syncFacesJson(const Json::Value& jsonRoot, const QString& serverLastModified);
    bool syncRfidJson(const Json::Value& jsonRoot, const QString& serverLastModified);
    bool updateUserRfidCards(const QString& employeeId, const QStringList& rfidCards);
     QByteArray base64FromNetworkToHex(const QString& base64String);
     QString hexToBase64ForNetwork(const QByteArray& hexData);
      bool enrollFingerprintFromSensor(const QString& employeeId, uint16_t fingerId);
     bool syncFingerprintJson(const Json::Value& jsonRoot, const QString& serverLastModified);
    bool updateUserFingerprintHex(const QString& employeeId, 
                                   uint16_t fingerId,
                                   const QByteArray& templateHex);
    QByteArray loadFingerprintFromDatabase(const QString& employeeId, uint16_t fingerId);
    bool initializeFaceMqtt();
    
    // ========================================================================
    // MQTT CALLBACKS
    // ========================================================================
    static void onConnect(struct mosquitto* mosq, void* obj, int reason_code);
    static void onDisconnect(struct mosquitto* mosq, void* obj, int reason_code);
    static void onPublish(struct mosquitto* mosq, void* obj, int mid);
    static void onMessage(struct mosquitto* mosq, void* obj, const struct mosquitto_message* message);
    static void onLog(struct mosquitto* mosq, void* obj, int level, const char* str);
     static void onFaceMqttConnect(struct mosquitto* mosq, void* obj, int rc);
    static void onFaceMqttLog(struct mosquitto* mosq, void* obj, int level, const char* str);

private slots:
    void onHeartbeatTimer();
    void onSyncFinished(bool success);
    void onS3SyncTimer();

private:
    // Singleton instance
    static MqttHeartbeatManager* m_instance;
    static QMutex m_mutex;
    struct mosquitto* m_faceMosq; // Face data MQTT client (AWS broker)
    bool m_faceMqttConnected;     // Face MQTT connection status
    
    // MQTT connection
    struct mosquitto* m_mosq;
    QString m_broker;
    int m_port;
    QString m_clientId;
    QString m_deviceSn;
    bool m_connected;
     QString m_tenantId; 
    
    // Timers
    QTimer* m_heartbeatTimer;
    QTimer* m_s3SyncTimer;
    int m_heartbeatInterval;
    
    // S3 Sync Thread
    QThread* m_syncThread;
    S3SyncWorker* m_syncWorker;
    QMutex m_syncMutex;
    bool m_syncInProgress;
    QTimer* m_configCheckTimer;
    QMap<QString, QString> m_localJsonTimestamps;  // Store local JSON file timestamps
    bool m_configRequestInProgress;
    QMutex m_configMutex;
    QStringList m_pendingConfigFiles; 
    QMap<QString, qint64> m_pendingConfigExpiry;  // FIX: tracks when each pending request was sent (for timeout)
};

#endif // MQTTHEARTBEATMANAGER_H
