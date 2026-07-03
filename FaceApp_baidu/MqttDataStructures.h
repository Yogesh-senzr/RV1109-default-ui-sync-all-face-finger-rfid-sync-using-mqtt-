#ifndef MQTT_DATA_STRUCTURES_H
#define MQTT_DATA_STRUCTURES_H

#include <QString>
#include <QDateTime>
#include <QList>

// =============================================================================
// Employee Data Structure
// =============================================================================
struct EmployeeJsonData
{
    QString userId;
    QString name;
    QString cardNo;
    QString icCard;
    QString faceId;
    QString faceDataUrl;
    QString status;
    QDateTime lastModified;
    
    EmployeeJsonData() {}
};

// =============================================================================
// Sync Error Structure
// =============================================================================
struct SyncError
{
    QString errorCode;
    QString userId;
    QString message;
    QString details;
    
    SyncError() {}
    
    SyncError(const QString& code, const QString& id, const QString& msg, const QString& det = "")
        : errorCode(code), userId(id), message(msg), details(det) {}
};

// =============================================================================
// Sync Results Structure
// =============================================================================
struct SyncResults
{
    int employeesAdded;
    int employeesModified;
    int employeesDeleted;
    int employeesUnchanged;
    int totalEmployees;
    qint64 syncDurationSeconds;
    int faceImagesDownloaded;
    int faceProcessingSuccess;
    int faceProcessingFailed;
    QList<SyncError> errors;
    
    SyncResults()
        : employeesAdded(0)
        , employeesModified(0)
        , employeesDeleted(0)
        , employeesUnchanged(0)
        , totalEmployees(0)
        , syncDurationSeconds(0)
        , faceImagesDownloaded(0)
        , faceProcessingSuccess(0)
        , faceProcessingFailed(0)
    {}
};

#endif // MQTT_DATA_STRUCTURES_H
