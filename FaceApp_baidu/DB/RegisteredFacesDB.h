#ifndef REGISTEREDFACESDB_H
#define REGISTEREDFACESDB_H

#include <QtCore/QThread>
#include "SharedInclude/GlobalDef.h"
#include "BaiduFace/BaiduFaceManager.h"
#include <QRect>

class PERSONS_s;
class RegisteredFacesDBPrivate;
class RegisteredFacesDB : public QThread
{
    Q_OBJECT
public:
    RegisteredFacesDB(QObject *parent = Q_NULLPTR);
    ~RegisteredFacesDB();
public:
    static inline RegisteredFacesDB *GetInstance(){static RegisteredFacesDB g;return &g;}
public:
    void setThanthreshold(const float &);

public:
      bool UpdatePersonFaceFeature(const QString &uuid, const QByteArray &faceFeature, 
                               const QByteArray &faceImageData = QByteArray(), 
                               const QString &imageFormat = "jpg");
public:
    bool selectICcardPerson(const QString &, PERSONS_s &s);
    bool selectIDcardPerson(const QString &, PERSONS_s &s);
    bool CheckPassageOfTime(QString person_uuid);
    bool UpdatePersonDirectly(const QString &uuid, const QString &name, const QString &idCard, const QString &sex, const QByteArray &faceFeature);
    bool UpdatePersonFingerprint(const QString &uuid, const QByteArray &fingerprintData);
    bool GetPersonFingerprint(const QString &uuid, QByteArray &fingerprintData);
    bool DeletePersonFingerprint(const QString &uuid);
    
    // Helper function to check if fingerprint column exists
    bool ensureFingerprintColumn();
    bool clearFingerprint(const QString& uuid);
    uint16_t getFingerId(const QString& uuid);
    static int getNextAvailableFingerId();  // Make it public and static

public:

    bool saveFaceImage(const QString &employeeId, const QByteArray &imageData, const QString &format = "jpg");
    bool saveCroppedFaceImage(const QString &employeeId, const QString &originalImagePath, 
                            const QRect &faceRect, const QString &format = "jpg");
    QString getFaceImagePath(const QString &employeeId, const QString &format = "jpg");
    bool deleteFaceImage(const QString &employeeId, const QString &format = "jpg");

    bool extractAndSaveFaceImage(const QString &employeeId, const QString &sourceImagePath);
    QString findUuidByEmployeeId(const QString &employeeId);

     QByteArray convertCroppedImageToBase64(const QString &employeeId);
    bool sendCroppedImageToServer(const QString &employeeId, const QString &userName, 
                                               const QString &icCard, const QString &userSex, 
                                               const QString &department, const QString &timeOfAccess, const QByteArray &faceFeature);
    
    // Modified existing functions to support face image storage AND NEW COLUMNS
      bool RegPersonToDBAndRAM(const QString &uuid, const QString &Name, const QString &idCard, 
                            const QString &icCard, const QString &Sex, const QString &department, 
                            const QString &timeOfAccess, const QByteArray &FaceFeature, 
                            const QByteArray &faceImageData = QByteArray(), 
                            const QString &imageFormat = "jpg",
                            const QString &attendanceMode = "", 
                            const QString &tenantId = "", 
                            const QString &id = "", 
                            const QString &status = "",
                            int personalModuleId = 0);  // ✅ ADD THIS PARAMETER
                           
    bool ProcessFaceRegistrationFromCroppedImage(const QString &employeeId);
    bool ExtractAndStoreFaceFeatures(const QString &employeeId, const QString &croppedImagePath);
    bool UpdateUserWithCroppedImageFeatures(const QString &employeeId);
    bool ProcessFaceRegistrationComplete(const QString &employeeId, const QByteArray &liveFeature = QByteArray());

    
     int UpdatePersonToDBAndRAM(const QString &uuid, const QString &name, const QString &idCard, 
                               const QString &icCard, const QString &sex, const QString &department, 
                               const QString &timeOfAccess, const QString &jpeg = "", 
                               const QByteArray &faceEmbedding = QByteArray(), 
                               const QString &attendanceMode = "", 
                               const QString &tenantId = "", 
                               const QString &id = "", 
                               const QString &status = "",
                               int personalModuleId = 0);  // ✅ ADD THIS PARAMETER

   
    bool RegPersonToDBAndRAM(const QString &name, const QString &sex, const QString &nation, const QString &idcardnum,
                       const QString &Marital, const QString &education, const QString &location, const QString &phone,
                       const QString &politics_status, const QString &date_birth, const QString &number,const QString &branch,
                       const QString &hiredate, const QString &extension_phone, const QString &mailbox, const QString &status,
                       const QString &persontype, const QString &iccardnum, const QByteArray &);

    bool ComparisonPersonFaceFeature(QString &name, QString &sex, QString &idcard, QString &iccard, QString &uuid, int &persontype, int &personid, QString &gids, QString &pids, const QByteArray &FaceFeature);
    bool ComparisonPersonFaceFeature_baidu(QString &name, QString &sex, QString &idcard, QString &iccard, QString &uuid, int &persontype, 
   int &personid, QString &gids, QString &pids, QString &id, unsigned char *FaceFeature, int FaceFeatureSize);

    bool DelPersonDBInfo(const QString &name, const QString &createtime);
    
    bool QueryNameRepetition(const QString &name)const;

    
    QList<PERSONS_t> GetAllPersonFromRAM();


    QList<PERSONS_t> GetPersonDataByNameFromRAM(int nCurrPage,int nPerPage,QString name);
    int GetPersonTotalNumByNameFromRAM(QString name);

    QList<PERSONS_t> GetPersonDataByPersonUUIDFromRAM(QString uuid);
    int GetPersonTotalNumByPersonUUIDFromRAM(QString uuid);

    QList<PERSONS_t> GetPersonDataByTimeFromRAM(int nCurrPage,int nPerPage,QTime startTime,QTime endTime);
    int GetPersonTotalNumByTimeFromRAM(QTime startTime,QTime endTime);

     bool DelPersonByPersionUUIDFromDBAndRAM(QString uuid);

     // Updated StoreUserWithoutFaceData with new columns
     
    bool StoreUserWithoutFaceData(const QString &uuid, const QString &Name, const QString &idCard, 
                                  const QString &icCard, const QString &Sex, const QString &department, 
                                  const QString &timeOfAccess, 
                                  const QString &attendanceMode = "", 
                                  const QString &tenantId = "", 
                                  const QString &id = "", 
                                  const QString &status = "",
                                  int personalModuleId = 0);  // ✅ ADD THIS PARAMETER

        
    void setCurrentUserServerTime(const QString& serverTime);
    QString getCurrentUserServerTime();
    bool validateBase64PersistenceAfterCreation(const QString &employeeId);

    // NEW FUNCTIONS for managing new columns
    bool createOrUpdateDatabaseSchema();
    PERSONS_t getPersonByUuid(const QString &uuid);
    bool updatePersonNewColumns(const QString &uuid, const QString &attendanceMode, 
                              const QString &tenantId, const QString &id, const QString &status);

    bool UpdatePersonFingerId(const QString &uuid, int fingerId);
    bool GetPersonByFingerId(int fingerId, PERSONS_t &person);
    bool clearAllFingerIds();
    bool ClearAllRfidCards();
    bool ClearAllDataForTenantChange(); // ✅ NEW: Clear everything on tenant change
    bool UpdateRfidJsonTimestamp(const QString& rfidJsonLastModified);
     bool UpdateJsonLastModifiedTimestamps(
        const QString& employeeJsonLastModified,
        const QString& faceJsonLastModified
    );
    bool GetJsonLastModifiedTimestamps(
        QString& employeeJsonLastModified,
        QString& faceJsonLastModified
    );
    // NEW - Get JSON data for a specific type
    QString GetJsonData(const QString& dataType);
    
    // NEW - Upsert methods
    bool UpsertEmployee(const QString& empId, const QString& name, 
                       const QString& gender, const QString& lastModified);
    bool UpsertFaceData(const QString& empId, const QString& faceData, 
                       const QString& lastModified);
    bool GetFingerprintJsonTimestamp(QString& fingerprintJsonLastModified);
    bool UpdateFingerprintJsonTimestamp(const QString& fingerprintJsonLastModified);
    bool GetAllJsonTimestamps(
    QString& employeeJsonLastModified,
    QString& faceJsonLastModified,
    QString& rfidJsonLastModified,
    QString& fingerprintJsonLastModified,
    QString& fourDoorJsonLastModified);
    bool GetFourDoorJsonTimestamp(QString& fourDoorJsonLastModified);
    bool UpdateFourDoorJsonTimestamp(const QString& fourDoorJsonLastModified);
    bool GetRfidJsonTimestamp(QString& rfidJsonLastModified);
private:
    void run();
    bool ensureFaceImageDirectory();
    QString getServerDateUpdatedTime();
    QString getGlobalServerDateUpdatedTime();
    QString getMostRecentUserServerTime();
    bool validateCroppedImageExists(const QString &employeeId);
    QString m_currentUserServerTime;
private:
    QScopedPointer<RegisteredFacesDBPrivate>d_ptr;
private:
    Q_DECLARE_PRIVATE(RegisteredFacesDB)
    Q_DISABLE_COPY(RegisteredFacesDB)
};

#endif // REGISTEREDFACESDB_H
