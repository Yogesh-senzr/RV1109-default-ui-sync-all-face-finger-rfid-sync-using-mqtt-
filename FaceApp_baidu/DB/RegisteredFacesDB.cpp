#include "RegisteredFacesDB.h"

#ifdef Q_OS_LINUX
//#include "ArcsoftFaceManager.h"
#include <unistd.h>
#endif

#include "BaiduFaceManager.h"
#include "../BaiduFace/FingerprintManager.h"
#include "SharedInclude/GlobalDef.h"
#include "Application/FaceApp.h"
#include "MessageHandler/Log.h"
#include "HttpServer/ConnHttpServerThread.h"
#include "FaceMainFrm.h"
#include <fcntl.h>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlDatabase>
#include <QtCore/QWaitCondition>
#include <QtCore/QMutex>
#include <QtCore/QList>
#include <QtCore/QUuid>
#include <QtCore/QDateTime>
#include <QtSql/QSqlError>
#include <QtCore/QDebug>
#include <QtCore/QTime>
#include <QDir>
#include <QPixmap>
#include <QBuffer>
#include <QImageWriter>
#include <QImageReader>


 double gAlogStateFaceSimilar = 0; //全局人脸对比阈值

static QMutex g_personIdMutex;

class RegisteredFacesDBPrivate
{
    Q_DECLARE_PUBLIC(RegisteredFacesDB)
public:
    RegisteredFacesDBPrivate(RegisteredFacesDB *dd);
private:
    mutable QMutex sync;
    QWaitCondition pauseCond;
    volatile bool is_pause;
private:
    float mThanthreshold;
    QList<PERSONS_t>mPersons;
private:
    RegisteredFacesDB *const q_ptr;
};

RegisteredFacesDBPrivate::RegisteredFacesDBPrivate(RegisteredFacesDB *dd)
    : q_ptr(dd)
    , is_pause(false)
    , mThanthreshold(0.8)
{
}


RegisteredFacesDB::RegisteredFacesDB(QObject *parent)
    : QThread(parent)
    , d_ptr(new RegisteredFacesDBPrivate(this))
{
    this->start();
}

RegisteredFacesDB::~RegisteredFacesDB()
{
    Q_D(RegisteredFacesDB);
    this->requestInterruption();
    d->is_pause = false;
    d->pauseCond.wakeOne();
    this->quit();
    this->wait();
}

bool RegisteredFacesDB::ensureFaceImageDirectory()
{
    QString dirPath = "/mnt/user/reg_face_image";
    QDir dir;
    
    // Check if directory exists
    if (!dir.exists(dirPath)) {
        LogD("%s %s[%d] Directory does not exist, creating: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, dirPath.toStdString().c_str());
             
        // Create the directory with all parent directories
        if (!dir.mkpath(dirPath)) {
            LogE("%s %s[%d] Failed to create directory: %s\n", 
                 __FILE__, __FUNCTION__, __LINE__, dirPath.toStdString().c_str());
            return false;
        }
        
        // Set proper permissions (read/write for owner, group, others)
        QFile::setPermissions(dirPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                     QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup |
                                     QFile::ReadOther | QFile::WriteOther | QFile::ExeOther);
        
        LogD("%s %s[%d] Successfully created directory: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, dirPath.toStdString().c_str());
    } else {
        LogD("%s %s[%d] Directory already exists: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, dirPath.toStdString().c_str());
    }
    
    // Double check that directory is writable
    QFileInfo dirInfo(dirPath);
    if (!dirInfo.isWritable()) {
        LogE("%s %s[%d] Directory is not writable: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, dirPath.toStdString().c_str());
        return false;
    }
    
    return true;
}

bool RegisteredFacesDB::saveFaceImage(const QString &employeeId, const QByteArray &imageData, const QString &format)
{
    LogD("%s %s[%d] Starting to save face image for employee: %s, data size: %d bytes\n", 
         __FILE__, __FUNCTION__, __LINE__, employeeId.toStdString().c_str(), imageData.size());
    
    if (imageData.isEmpty()) {
        LogE("%s %s[%d] Image data is empty for employee: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, employeeId.toStdString().c_str());
        return false;
    }
    
    if (employeeId.isEmpty()) {
        LogE("%s %s[%d] Employee ID is empty\n", __FILE__, __FUNCTION__, __LINE__);
        return false;
    }
    
    if (!ensureFaceImageDirectory()) {
        LogE("%s %s[%d] Failed to ensure directory exists\n", __FILE__, __FUNCTION__, __LINE__);
        return false;
    }
    
    QString fileName = QString("/mnt/user/reg_face_image/%1.%2").arg(employeeId).arg(format.toLower());
    LogD("%s %s[%d] Attempting to save image to: %s\n", 
         __FILE__, __FUNCTION__, __LINE__, fileName.toStdString().c_str());
    
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        LogE("%s %s[%d] Failed to open file for writing: %s, Error: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, fileName.toStdString().c_str(), 
             file.errorString().toStdString().c_str());
        return false;
    }
    
    qint64 bytesWritten = file.write(imageData);
    file.close();
    
    if (bytesWritten == imageData.size()) {
        LogD("%s %s[%d] Successfully saved face image: %s (size: %lld bytes)\n", 
             __FILE__, __FUNCTION__, __LINE__, fileName.toStdString().c_str(), bytesWritten);
        
        // Sync to ensure data is written to disk
#ifdef Q_OS_LINUX
        system("sync");
#endif
        
        // Verify file was actually created and has correct size
        QFileInfo fileInfo(fileName);
        if (fileInfo.exists() && fileInfo.size() == imageData.size()) {
            LogD("%s %s[%d] File verification successful: %s, size: %lld\n", 
                 __FILE__, __FUNCTION__, __LINE__, fileName.toStdString().c_str(), fileInfo.size());
            return true;
        } else {
            LogE("%s %s[%d] File verification failed: %s, exists: %s, expected size: %d, actual size: %lld\n", 
                 __FILE__, __FUNCTION__, __LINE__, fileName.toStdString().c_str(),
                 fileInfo.exists() ? "true" : "false", imageData.size(), fileInfo.size());
            return false;
        }
    } else {
        LogE("%s %s[%d] Failed to write complete image data to: %s, wrote %lld of %d bytes\n", 
             __FILE__, __FUNCTION__, __LINE__, fileName.toStdString().c_str(), 
             bytesWritten, imageData.size());
        return false;
    }
}

bool RegisteredFacesDB::saveCroppedFaceImage(const QString &employeeId, const QString &originalImagePath, 
                                           const QRect &faceRect, const QString &format)
{
    if (!ensureFaceImageDirectory()) {
        return false;
    }
    
    // Load original image
    QImageReader reader(originalImagePath);
    if (!reader.canRead()) {
        LogE("%s %s[%d] Cannot read image file: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, originalImagePath.toStdString().c_str());
        return false;
    }
    
    QImage originalImage = reader.read();
    if (originalImage.isNull()) {
        LogE("%s %s[%d] Failed to load image: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, originalImagePath.toStdString().c_str());
        return false;
    }
    
    // Crop the face region
    QImage croppedImage = originalImage.copy(faceRect);
    if (croppedImage.isNull()) {
        LogE("%s %s[%d] Failed to crop image with rect (%d,%d,%d,%d)\n", 
             __FILE__, __FUNCTION__, __LINE__, faceRect.x(), faceRect.y(), 
             faceRect.width(), faceRect.height());
        return false;
    }
    
    // Save cropped image
    QString fileName = QString("/mnt/user/reg_face_image/%1.%2").arg(employeeId).arg(format.toLower());
    
    QImageWriter writer(fileName, format.toUpper().toUtf8());
    writer.setQuality(95); // High quality
    
    if (writer.write(croppedImage)) {
        LogD("%s %s[%d] Successfully saved cropped face image: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, fileName.toStdString().c_str());
        return true;
    } else {
        LogE("%s %s[%d] Failed to save cropped image: %s, Error: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, fileName.toStdString().c_str(), 
             writer.errorString().toStdString().c_str());
        return false;
    }
}


QString RegisteredFacesDB::getFaceImagePath(const QString &employeeId, const QString &format)
{
    QString fileName = QString("/mnt/user/reg_face_image/%1.%2").arg(employeeId).arg(format.toLower());
    QFile file(fileName);
    if (file.exists()) {
        return fileName;
    }
    return QString(); // Return empty string if file doesn't exist
}

bool RegisteredFacesDB::deleteFaceImage(const QString &employeeId, const QString &format)
{
    QString fileName = QString("/mnt/user/reg_face_image/%1.%2").arg(employeeId).arg(format.toLower());
    QFile file(fileName);
    if (file.exists()) {
        bool result = file.remove();
        if (result) {
            LogD("%s %s[%d] Successfully deleted face image: %s\n", 
                 __FILE__, __FUNCTION__, __LINE__, fileName.toStdString().c_str());
        } else {
            LogE("%s %s[%d] Failed to delete face image: %s\n", 
                 __FILE__, __FUNCTION__, __LINE__, fileName.toStdString().c_str());
        }
        return result;
    }
    return true; // Consider it successful if file doesn't exist
}

bool RegisteredFacesDB::extractAndSaveFaceImage(const QString &employeeId, const QString &sourceImagePath)
{
    LogD("%s %s[%d] === FACE CROPPING EXTRACTION (like AddUserFrm) ===\n", __FILE__, __FUNCTION__, __LINE__);
    LogD("%s %s[%d] Employee ID: %s, Source: %s\n", __FILE__, __FUNCTION__, __LINE__, 
         employeeId.toStdString().c_str(), sourceImagePath.toStdString().c_str());
    
    if (employeeId.isEmpty() || sourceImagePath.isEmpty()) {
        LogE("%s %s[%d] Invalid parameters\n", __FILE__, __FUNCTION__, __LINE__);
        return false;
    }
    
    // Check if source file exists
    QFile sourceFile(sourceImagePath);
    if (!sourceFile.exists()) {
        LogE("%s %s[%d] Source image does not exist: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, sourceImagePath.toStdString().c_str());
        return false;
    }
    
    // ✅ USE SAME LOGIC AS AddUserFrm - Call BaiduFaceManager for cropping
    BaiduFaceManager* faceManager = (BaiduFaceManager*)qXLApp->GetAlgoFaceManager();
    if (!faceManager) {
        LogE("%s %s[%d] BaiduFaceManager not available\n", __FILE__, __FUNCTION__, __LINE__);
        return false;
    }
    
    bool imageSaved = false;
    
    // Method 1: Try cropping from the captured image file (SAME AS AddUserFrm)
    LogD("%s %s[%d] Attempting to crop from file...\n", __FILE__, __FUNCTION__, __LINE__);
    imageSaved = faceManager->cropAndSaveFaceImage(employeeId, sourceImagePath);
    
    if (imageSaved) {
        LogD("%s %s[%d] SUCCESS: Cropped face image saved from file for employee: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, employeeId.toStdString().c_str());
    } else {
        LogD("%s %s[%d] File-based cropping failed, trying current face data...\n", __FILE__, __FUNCTION__, __LINE__);
        
        // Method 2: Try cropping from current face data in memory (SAME AS AddUserFrm)
        imageSaved = faceManager->cropCurrentFaceAndSave(employeeId);
        
        if (imageSaved) {
            LogD("%s %s[%d] SUCCESS: Cropped face image saved from current data for employee: %s\n", 
                 __FILE__, __FUNCTION__, __LINE__, employeeId.toStdString().c_str());
        }
    }
    
    if (imageSaved) {
        QString savedPath = QString("/mnt/user/reg_face_image/%1.jpg").arg(employeeId);
        LogD("%s %s[%d] Cropped face image saved at: %s\n", __FILE__, __FUNCTION__, __LINE__, savedPath.toStdString().c_str());
        
        // Verify the file exists and has reasonable size (SAME AS AddUserFrm)
        QFileInfo fileInfo(savedPath);
        if (fileInfo.exists() && fileInfo.size() > 1000) { // At least 1KB
            LogD("%s %s[%d] File verification passed: size = %lld bytes\n", __FILE__, __FUNCTION__, __LINE__, fileInfo.size());
        } else {
            LogE("%s %s[%d] File verification failed or file too small\n", __FILE__, __FUNCTION__, __LINE__);
            return false;
        }
    } else {
        LogE("%s %s[%d] Failed to save cropped face image for employee: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, employeeId.toStdString().c_str());
        return false;
    }
    
    return imageSaved;
}

// Add this function to RegisteredFacesDB.cpp
// (Helper function to find UUID by employee ID)
QString RegisteredFacesDB::findUuidByEmployeeId(const QString &employeeId)
{
    Q_D(RegisteredFacesDB);
    QMutexLocker lock(&d->sync);
    
    LogD("%s %s[%d] Looking for UUID for employee ID: %s\n", 
         __FILE__, __FUNCTION__, __LINE__, employeeId.toStdString().c_str());
    
    // First check in-memory data
    for (int i = 0; i < d->mPersons.size(); i++) {
        if (d->mPersons[i].idcard == employeeId) {
            LogD("%s %s[%d] Found UUID in memory: %s for employee ID: %s\n", 
                 __FILE__, __FUNCTION__, __LINE__, d->mPersons[i].uuid.toStdString().c_str(), employeeId.toStdString().c_str());
            return d->mPersons[i].uuid;
        }
    }
    
    // If not found in memory, check database
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    query.prepare("SELECT uuid FROM person WHERE idcardnum = ?");
    query.bindValue(0, employeeId);
    
    if (query.exec() && query.next()) {
        QString uuid = query.value("uuid").toString();
        LogD("%s %s[%d] Found UUID in database: %s for employee ID: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, uuid.toStdString().c_str(), employeeId.toStdString().c_str());
        return uuid;
    }
    
    LogE("%s %s[%d] UUID not found for employee ID: %s\n", 
         __FILE__, __FUNCTION__, __LINE__, employeeId.toStdString().c_str());
    return QString();
}

bool RegisteredFacesDB::sendCroppedImageToServer(const QString &employeeId, const QString &userName, 
                                               const QString &icCard, const QString &userSex, 
                                               const QString &department, const QString &timeOfAccess, const QByteArray &faceFeature)
{
    LogD("%s %s[%d] ============= DETAILED BASE64 DEBUG START =============\n", __FILE__, __FUNCTION__, __LINE__);
    LogD("%s %s[%d] PARAMETERS RECEIVED:\n", __FILE__, __FUNCTION__, __LINE__);
    LogD("%s %s[%d]   employeeId: '%s'\n", __FILE__, __FUNCTION__, __LINE__, employeeId.toStdString().c_str());
    LogD("%s %s[%d]   userName: '%s'\n", __FILE__, __FUNCTION__, __LINE__, userName.toStdString().c_str());
    LogD("%s %s[%d]   icCard: '%s'\n", __FILE__, __FUNCTION__, __LINE__, icCard.toStdString().c_str());
    LogD("%s %s[%d]   userSex: '%s'\n", __FILE__, __FUNCTION__, __LINE__, userSex.toStdString().c_str());
    LogD("%s %s[%d]   department: '%s'\n", __FILE__, __FUNCTION__, __LINE__, department.toStdString().c_str());
    LogD("%s %s[%d]   timeOfAccess: '%s'\n", __FILE__, __FUNCTION__, __LINE__, timeOfAccess.toStdString().c_str());
    
    // Step 1: Check if cropped image exists
    QString croppedImagePath = QString("/mnt/user/reg_face_image/%1.jpg").arg(employeeId);
    LogD("%s %s[%d] STEP 1: Checking cropped image path: %s\n", __FILE__, __FUNCTION__, __LINE__, 
         croppedImagePath.toStdString().c_str());
    
    QFileInfo imageInfo(croppedImagePath);
    
    if (!imageInfo.exists()) {
        LogE("%s %s[%d] ERROR: Cropped face image does not exist: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, croppedImagePath.toStdString().c_str());
        LogE("%s %s[%d] DEBUG: Current working directory: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, QDir::currentPath().toStdString().c_str());
        LogE("%s %s[%d] DEBUG: Image directory exists: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, QDir("/mnt/user/reg_face_image/").exists() ? "YES" : "NO");
        return false;
    }
    
    LogD("%s %s[%d] SUCCESS: Image file exists\n", __FILE__, __FUNCTION__, __LINE__);
    LogD("%s %s[%d] Image file size: %lld bytes\n", __FILE__, __FUNCTION__, __LINE__, imageInfo.size());
    LogD("%s %s[%d] Image file permissions: readable=%s, writable=%s\n", __FILE__, __FUNCTION__, __LINE__,
         imageInfo.isReadable() ? "YES" : "NO", imageInfo.isWritable() ? "YES" : "NO");
    
    if (imageInfo.size() < 1000) {
        LogE("%s %s[%d] ERROR: Cropped face image too small: %lld bytes (minimum 1000 bytes expected)\n", 
             __FILE__, __FUNCTION__, __LINE__, imageInfo.size());
        return false;
    }
    
    // Step 2: Read the cropped image file data
    LogD("%s %s[%d] STEP 2: Reading image file data\n", __FILE__, __FUNCTION__, __LINE__);
    QFile croppedImageFile(croppedImagePath);
    
    if (!croppedImageFile.open(QIODevice::ReadOnly)) {
        LogE("%s %s[%d] ERROR: Cannot open cropped image file for reading\n", __FILE__, __FUNCTION__, __LINE__);
        LogE("%s %s[%d] File error string: %s\n", __FILE__, __FUNCTION__, __LINE__, 
             croppedImageFile.errorString().toStdString().c_str());
        LogE("%s %s[%d] File error code: %d\n", __FILE__, __FUNCTION__, __LINE__, 
             (int)croppedImageFile.error());
        return false;
    }
    
    LogD("%s %s[%d] SUCCESS: Image file opened for reading\n", __FILE__, __FUNCTION__, __LINE__);
    
    QByteArray croppedImageData = croppedImageFile.readAll();
    croppedImageFile.close();
    
    LogD("%s %s[%d] Image data read successfully: %d bytes\n", __FILE__, __FUNCTION__, __LINE__, 
         croppedImageData.size());
    LogD("%s %s[%d] Image data first 10 bytes (hex): ", __FILE__, __FUNCTION__, __LINE__);
    for (int i = 0; i < qMin(10, croppedImageData.size()); i++) {
        printf("%02X ", (unsigned char)croppedImageData[i]);
    }
    printf("\n");
    
    // Step 3: Validate image data
    LogD("%s %s[%d] STEP 3: Validating image data\n", __FILE__, __FUNCTION__, __LINE__);
    
    if (croppedImageData.size() < 1000) {
        LogE("%s %s[%d] ERROR: Read image data too small: %d bytes\n", 
             __FILE__, __FUNCTION__, __LINE__, croppedImageData.size());
        return false;
    }
    
    LogD("%s %s[%d] Image data size validation passed: %d bytes\n", 
         __FILE__, __FUNCTION__, __LINE__, croppedImageData.size());
    
    // Enhanced image format validation
    bool isValidJPEG = false;
    bool isValidPNG = false;
    
    if (croppedImageData.size() >= 2) {
        // Check JPEG header (FF D8)
        if ((unsigned char)croppedImageData[0] == 0xFF && (unsigned char)croppedImageData[1] == 0xD8) {
            isValidJPEG = true;
            LogD("%s %s[%d] Valid JPEG header detected (FF D8)\n", __FILE__, __FUNCTION__, __LINE__);
        }
        // Check PNG header (89 50 4E 47)
        else if (croppedImageData.size() >= 4 && 
                 (unsigned char)croppedImageData[0] == 0x89 && 
                 (unsigned char)croppedImageData[1] == 0x50 &&
                 (unsigned char)croppedImageData[2] == 0x4E && 
                 (unsigned char)croppedImageData[3] == 0x47) {
            isValidPNG = true;
            LogD("%s %s[%d] Valid PNG header detected (89 50 4E 47)\n", __FILE__, __FUNCTION__, __LINE__);
        }
        else {
            LogD("%s %s[%d] WARNING: Unknown image format - first 4 bytes: %02X %02X %02X %02X\n", 
                 __FILE__, __FUNCTION__, __LINE__,
                 (unsigned char)croppedImageData[0], (unsigned char)croppedImageData[1],
                 (unsigned char)croppedImageData[2], (unsigned char)croppedImageData[3]);
        }
    }
    
    if (!isValidJPEG && !isValidPNG) {
        LogD("%s %s[%d] WARNING: Image format not recognized, but proceeding with base64 conversion\n", 
             __FILE__, __FUNCTION__, __LINE__);
    }
    
    // Step 4: Convert image data to base64
    LogD("%s %s[%d] STEP 4: Converting image to base64\n", __FILE__, __FUNCTION__, __LINE__);
    LogD("%s %s[%d] Original image size before base64: %d bytes\n", __FILE__, __FUNCTION__, __LINE__, 
         croppedImageData.size());
    
    QByteArray base64ImageData = croppedImageData.toBase64();
    
    LogD("%s %s[%d] Base64 conversion completed successfully\n", __FILE__, __FUNCTION__, __LINE__);
    LogD("%s %s[%d] Original image size: %d bytes\n", __FILE__, __FUNCTION__, __LINE__, 
         croppedImageData.size());
    LogD("%s %s[%d] Base64 encoded size: %d bytes\n", __FILE__, __FUNCTION__, __LINE__, 
         base64ImageData.size());
    LogD("%s %s[%d] Base64 size ratio: %.2f%% increase\n", __FILE__, __FUNCTION__, __LINE__, 
         ((float)base64ImageData.size() / (float)croppedImageData.size() - 1.0f) * 100.0f);
    
    // Debug base64 content
    if (base64ImageData.size() > 0) {
        LogD("%s %s[%d] Base64 first 50 characters: %.50s\n", __FILE__, __FUNCTION__, __LINE__, 
             base64ImageData.constData());
        LogD("%s %s[%d] Base64 last 20 characters: %s\n", __FILE__, __FUNCTION__, __LINE__, 
             base64ImageData.right(20).constData());
        
        // Check if it starts with typical image base64 patterns
        QString base64String = QString::fromUtf8(base64ImageData.left(10));
        LogD("%s %s[%d] Base64 prefix analysis:\n", __FILE__, __FUNCTION__, __LINE__);
        if (base64String.startsWith("/9j/")) {
            LogD("%s %s[%d]   - Starts with '/9j/' - JPEG format confirmed\n", __FILE__, __FUNCTION__, __LINE__);
        } else if (base64String.startsWith("iVBORw")) {
            LogD("%s %s[%d]   - Starts with 'iVBORw' - PNG format confirmed\n", __FILE__, __FUNCTION__, __LINE__);
        } else {
            LogD("%s %s[%d]   - Starts with '%s' - format unknown\n", __FILE__, __FUNCTION__, __LINE__, 
                 base64String.toStdString().c_str());
        }
    } else {
        LogE("%s %s[%d] ERROR: Base64 conversion resulted in empty data\n", __FILE__, __FUNCTION__, __LINE__);
        return false;
    }
    
    // Step 5: Prepare server communication
    LogD("%s %s[%d] STEP 5: Preparing server communication\n", __FILE__, __FUNCTION__, __LINE__);
    ConnHttpServerThread* serverThread = ConnHttpServerThread::GetInstance();
    
    if (!serverThread) {
        LogE("%s %s[%d] ERROR: ConnHttpServerThread instance not available\n", __FILE__, __FUNCTION__, __LINE__);
        LogE("%s %s[%d] DEBUG: GetInstance() returned NULL pointer\n", __FILE__, __FUNCTION__, __LINE__);
        return false;
    }
    
    LogD("%s %s[%d] SUCCESS: ConnHttpServerThread instance obtained\n", __FILE__, __FUNCTION__, __LINE__);
    LogD("%s %s[%d] Server thread pointer: %p\n", __FILE__, __FUNCTION__, __LINE__, (void*)serverThread);
    
    // Step 6: Send base64 image data to server
    LogD("%s %s[%d] STEP 6: Sending data to server\n", __FILE__, __FUNCTION__, __LINE__);
    LogD("%s %s[%d] Server parameters being sent:\n", __FILE__, __FUNCTION__, __LINE__);
    LogD("%s %s[%d]   employeeId: '%s'\n", __FILE__, __FUNCTION__, __LINE__, employeeId.toStdString().c_str());
    LogD("%s %s[%d]   userName: '%s'\n", __FILE__, __FUNCTION__, __LINE__, userName.toStdString().c_str());
    LogD("%s %s[%d]   idCard: '%s' (using employeeId)\n", __FILE__, __FUNCTION__, __LINE__, employeeId.toStdString().c_str());
    LogD("%s %s[%d]   icCard: '%s'\n", __FILE__, __FUNCTION__, __LINE__, icCard.toStdString().c_str());
    LogD("%s %s[%d]   userSex: '%s'\n", __FILE__, __FUNCTION__, __LINE__, userSex.toStdString().c_str());
    LogD("%s %s[%d]   department: '%s'\n", __FILE__, __FUNCTION__, __LINE__, department.toStdString().c_str());
    LogD("%s %s[%d]   timeOfAccess: '%s'\n", __FILE__, __FUNCTION__, __LINE__, timeOfAccess.toStdString().c_str());
    LogD("%s %s[%d]   base64ImageData size: %d bytes\n", __FILE__, __FUNCTION__, __LINE__, base64ImageData.size());
    
    try {
        LogD("%s %s[%d] Calling serverThread->sendUserToServer()...\n", __FILE__, __FUNCTION__, __LINE__);
        
        serverThread->sendUserToServer(
            employeeId,                    // employeeId
            userName,                      // name
            employeeId,                    // idCard (using employeeId as idCard)
            icCard,                        // icCard
            userSex,                       // sex
            department,                    // department
            timeOfAccess,                  // timeOfAccess
            base64ImageData                // faceFeature (BASE64 IMAGE DATA, NOT embedding)
        );
        
        LogD("%s %s[%d] SUCCESS: serverThread->sendUserToServer() called successfully\n", __FILE__, __FUNCTION__, __LINE__);
        LogD("%s %s[%d] Base64 image data sent to server (size: %d bytes)\n", __FILE__, __FUNCTION__, __LINE__, base64ImageData.size());
        LogD("%s %s[%d] ============= DETAILED BASE64 DEBUG END - SUCCESS =============\n", __FILE__, __FUNCTION__, __LINE__);
        return true;
        
    } catch (const std::exception& e) {
        LogE("%s %s[%d] EXCEPTION: Error calling sendUserToServer: %s\n", __FILE__, __FUNCTION__, __LINE__, e.what());
        LogD("%s %s[%d] ============= DETAILED BASE64 DEBUG END - EXCEPTION =============\n", __FILE__, __FUNCTION__, __LINE__);
        return false;
    } catch (...) {
        LogE("%s %s[%d] UNKNOWN EXCEPTION: Error calling sendUserToServer\n", __FILE__, __FUNCTION__, __LINE__);
        LogD("%s %s[%d] ============= DETAILED BASE64 DEBUG END - UNKNOWN EXCEPTION =============\n", __FILE__, __FUNCTION__, __LINE__);
        return false;
    }
}

QByteArray RegisteredFacesDB::convertCroppedImageToBase64(const QString &employeeId)
{
    LogD("%s %s[%d] ========== convertCroppedImageToBase64 DEBUG START ==========\n", __FILE__, __FUNCTION__, __LINE__);
    LogD("%s %s[%d] Converting image to base64 for employeeId: '%s'\n", __FILE__, __FUNCTION__, __LINE__, 
         employeeId.toStdString().c_str());
    
    QString imagePath = QString("/mnt/user/reg_face_image/%1.jpg").arg(employeeId);
    LogD("%s %s[%d] Image path: %s\n", __FILE__, __FUNCTION__, __LINE__, imagePath.toStdString().c_str());
    
    // Check file exists
    QFileInfo fileInfo(imagePath);
    if (!fileInfo.exists()) {
        LogE("%s %s[%d] ERROR: Image file does not exist: %s\n", __FILE__, __FUNCTION__, __LINE__, 
             imagePath.toStdString().c_str());
        return QByteArray();
    }
    
    LogD("%s %s[%d] Image file exists, size: %lld bytes\n", __FILE__, __FUNCTION__, __LINE__, fileInfo.size());
    
    // Single file operation - no repeated opens
    QFile imageFile(imagePath);
    if (!imageFile.open(QIODevice::ReadOnly)) {
        LogE("%s %s[%d] ERROR: Cannot open image file for reading\n", __FILE__, __FUNCTION__, __LINE__);
        LogE("%s %s[%d] Error details: %s\n", __FILE__, __FUNCTION__, __LINE__, 
             imageFile.errorString().toStdString().c_str());
        return QByteArray();
    }
    
    LogD("%s %s[%d] Image file opened successfully\n", __FILE__, __FUNCTION__, __LINE__);
    
    QByteArray imageData = imageFile.readAll();
    imageFile.close();
    
    if (imageData.isEmpty()) {
        LogE("%s %s[%d] ERROR: Read image data is empty\n", __FILE__, __FUNCTION__, __LINE__);
        return QByteArray();
    }
    
    LogD("%s %s[%d] Image data read: %d bytes\n", __FILE__, __FUNCTION__, __LINE__, imageData.size());
    
    // Convert to base64 in single operation
    QByteArray base64Data = imageData.toBase64();
    
    LogD("%s %s[%d] Base64 conversion completed\n", __FILE__, __FUNCTION__, __LINE__);
    LogD("%s %s[%d] Original size: %d bytes -> Base64 size: %d bytes\n", __FILE__, __FUNCTION__, __LINE__, 
         imageData.size(), base64Data.size());
    
    if (!base64Data.isEmpty()) {
        LogD("%s %s[%d] Base64 preview (first 30 chars): %.30s\n", __FILE__, __FUNCTION__, __LINE__, 
             base64Data.constData());
    }
    
    LogD("%s %s[%d] ========== convertCroppedImageToBase64 DEBUG END ==========\n", __FILE__, __FUNCTION__, __LINE__);
    
    return base64Data;
}



bool RegisteredFacesDB::validateBase64PersistenceAfterCreation(const QString &employeeId)
{
    LogD("%s %s[%d] === CHECKING BASE64 PERSISTENCE ===\n", __FILE__, __FUNCTION__, __LINE__);
    
    // Wait a moment to ensure file system operations complete
    QThread::msleep(100);
    
    QString croppedImagePath = QString("/mnt/user/reg_face_image/%1.jpg").arg(employeeId);
    QFileInfo fileInfo(croppedImagePath);
    
    if (!fileInfo.exists()) {
        LogE("%s %s[%d] ERROR: Cropped image disappeared after creation!\n", __FILE__, __FUNCTION__, __LINE__);
        return false;
    }
    
    if (fileInfo.size() < 1000) {
        LogE("%s %s[%d] ERROR: Cropped image size too small: %lld bytes\n", __FILE__, __FUNCTION__, __LINE__, 
             fileInfo.size());
        return false;
    }
    
    // Test base64 conversion
    QByteArray testBase64 = convertCroppedImageToBase64(employeeId);
    if (testBase64.isEmpty()) {
        LogE("%s %s[%d] ERROR: Base64 conversion fails after file creation\n", __FILE__, __FUNCTION__, __LINE__);
        return false;
    }
    
    LogD("%s %s[%d] SUCCESS: Base64 persistence validated\n", __FILE__, __FUNCTION__, __LINE__);
    return true;
}

// Helper function to validate cropped image exists
bool RegisteredFacesDB::validateCroppedImageExists(const QString &employeeId)
{
    LogD("%s %s[%d] Validating cropped image exists for: %s\n", __FILE__, __FUNCTION__, __LINE__, 
         employeeId.toStdString().c_str());
    
    QString croppedImagePath = QString("/mnt/user/reg_face_image/%1.jpg").arg(employeeId);
    QFileInfo imageInfo(croppedImagePath);
    
    if (!imageInfo.exists()) {
        LogE("%s %s[%d] Cropped image does not exist: %s\n", __FILE__, __FUNCTION__, __LINE__, 
             croppedImagePath.toStdString().c_str());
        return false;
    }
    
    if (imageInfo.size() < 1000) {
        LogE("%s %s[%d] Cropped image too small: %lld bytes\n", __FILE__, __FUNCTION__, __LINE__, 
             imageInfo.size());
        return false;
    }
    
    LogD("%s %s[%d] Cropped image validation passed: %lld bytes\n", __FILE__, __FUNCTION__, __LINE__, 
         imageInfo.size());
    return true;
}

void RegisteredFacesDB::setThanthreshold(const float &value)
{
    Q_D(RegisteredFacesDB);
    d->mThanthreshold = value;
}

bool RegisteredFacesDB::selectICcardPerson(const QString &iccard, PERSONS_s &s)
{
    s.reader = true;
    s.iccard = QString();
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    // Match if the passed iccard is a substring of the DB iccardnum, OR if the DB iccardnum is a substring of the passed iccard
    query.exec(QString("SELECT * FROM person WHERE iccardnum LIKE '%%1%' OR '%1' LIKE '%' || iccardnum || '%' LIMIT 1").arg(iccard));
    while(query.next())
    {
        s.feature = query.value("feature").toByteArray();
        s.name = query.value("name").toString();
        s.sex = query.value("sex").toString();
        s.idcard = query.value("idcardnum").toString();
        s.iccard = query.value("iccardnum").toString();
        s.uuid= query.value("uuid").toString();
        s.persontype = query.value("persontype").toInt();
        s.personid = query.value("personid").toInt();
        s.gids = query.value("gids").toString();
        s.pids = query.value("aids").toString();
        s.createtime = query.value("createtime").toString();
        return true;
    }
    return false;
}

bool RegisteredFacesDB::selectIDcardPerson(const QString &idCard, PERSONS_s &s)
{
    s.reader = true;
    s.idcard = QString();
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    query.exec(QString("select *from person ORDER BY idcardnum='%1' DESC LIMIT 1").arg(idCard));
    while(query.next())
    {
        s.feature = query.value("feature").toByteArray();
        s.name = query.value("name").toString();
        s.sex = query.value("sex").toString();
        s.idcard = query.value("idcardnum").toString();
        s.iccard = query.value("iccardnum").toString();
        s.uuid= query.value("uuid").toString();
        s.persontype = query.value("persontype").toInt();
        s.personid = query.value("personid").toInt();
        s.gids = query.value("gids").toString();
        s.pids = query.value("aids").toString();
        s.createtime = query.value("createtime").toString();
        return true;
    }
    return false;
}

bool RegisteredFacesDB::CheckPassageOfTime(QString person_uuid)
{
	/**先查询用户表里面，用户自己的通行权限*/
    QString strStartTime;
    QString strCurTime;
    QString strEndTime;

#if 0
case 1: person 通行  PassageTime 通行  预期:通行 
       1.1 person 为空
	   1.2 person 通行      00:10,10:30,1,1,1,1,1,1,1
	   1.3 PassageTime  为空
	   1.4 person 通行 	   
case 2  person 禁行  PassageTime 通行  预期:禁行
       2.1 person 禁行
	   2.2  PassageTime  为空
	   2.3  PassageTime 通行  00:00|23:59,1,1,1,1,1,1,1
case 3  person 通行  PassageTime 禁行  预期:禁行
       3.1 person 为空
	   3.2 person 通行 
	   3.3  PassageTime 禁行  00:00|23:59,0,0,0,0,0,0,0
case 4  person 禁行  PassageTime 禁行  预期:禁行
       4.1 person 禁行
	   4.2  PassageTime 禁行 

#endif 

   //  bool bAccessPerson = false;
	if(person_uuid.size() > 3)
	{
		Q_D(RegisteredFacesDB);

        //step 1:
		for(int i = 0; i < d->mPersons.size(); i++)
		{
			auto &t = d->mPersons.at(i);
			if(t.uuid == person_uuid)
			{
				QStringList sections = t.timeOfAccess.split(",");
				LogD("%s %s[%d] uuid %s timeOfAccess %s  sections=%d\n",__FILE__,__FUNCTION__,__LINE__,t.uuid.toStdString().c_str(),t.timeOfAccess.toStdString().c_str(),sections.size());
                if (sections.size() <= 1) 
                {
                   //bAccessPerson =true;
                }
				else if (sections.size() == 9)  //周循环
				{
					strStartTime = sections.at(0);
					strEndTime = sections.at(1);

					QTime curTime = QTime::currentTime();
					QTime stateTime = QTime::fromString(strStartTime, "hh:mm");
					QTime endTime = QTime::fromString(strEndTime, "hh:mm");

					if (curTime <= endTime && curTime >= stateTime)
					{
						switch (QDate::currentDate().dayOfWeek())
						{
						case 1:
							return (sections[2] == "1");
						case 2:
							return (sections[3] == "1");
						case 3:
							return (sections[4] == "1");
						case 4:
							return (sections[5] == "1");
						case 5:
							return (sections[6] == "1");
						case 6:
							return (sections[7] == "1");
						case 7:
							return (sections[8] == "1");
						}
					} 
                    
					//LogD("%s %s[%d] uuid %s time_of_access %s \n", __FILE__, __FUNCTION__, __LINE__, t.uuid.toStdString().c_str(),t.timeOfAccess.toStdString().c_str());
                    return false;
				} else  //时间范围
				{
					//t.timeOfAccess 2000/01/01 00:00;2000/01/01 00:00
					QStringList sections = t.timeOfAccess.split(";");
					if (sections.size() == 2)
					{
						QDateTime curTimer = QDateTime::currentDateTime();
						QDateTime startTimer = QDateTime::fromString(sections[0], "yyyy/MM/dd hh:mm");
						QDateTime endTimer = QDateTime::fromString(sections[1], "yyyy/MM/dd hh:mm");
						if (curTimer <= endTimer && curTimer >= startTimer)
						{
							return true;
						} else
						{
					        strCurTime = curTimer.toString(QString::fromLatin1("yyyy/MM/dd hh:mm"));
					        strStartTime = startTimer.toString(QString::fromLatin1("yyyy/MM/dd hh:mm"));
					        strEndTime = endTimer.toString(QString::fromLatin1("yyyy/MM/dd hh:mm"));

					        LogD("%s %s[%d] start %s cur %s end %s \n",__FILE__,__FUNCTION__,__LINE__,strStartTime.toStdString().c_str(),
					        		strCurTime.toStdString().c_str(),strEndTime.toStdString().c_str());
							return false;
						}
					}
				}
			}
		}
	}
  
    //step 2:
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    query.exec("select *from PassageTime");

    strCurTime = "";
    strStartTime = "";
    strEndTime = "";

    while(query.next())
    {
    	QTime curTime = QTime::currentTime();
    	QTime stateTime = QTime::fromString(query.value("stateTimer").toString(), "hh:mm");
    	QTime endTime =  QTime::fromString(query.value("endTimer").toString(), "hh:mm");
        strCurTime = curTime.toString(QString::fromLatin1("hh:mm"));
        strStartTime = stateTime.toString(QString::fromLatin1("hh:mm"));
        strEndTime = endTime.toString(QString::fromLatin1("hh:mm"));

        if(curTime<=endTime && curTime>=stateTime)
        {
            switch(QDate::currentDate().dayOfWeek())
            {
            case 1: return query.value("Monday").toBool();
            case 2: return query.value("Tuesday").toBool();
            case 3: return query.value("Wednesday").toBool();
            case 4: return query.value("Thursday").toBool();
            case 5: return query.value("Friday").toBool();
            case 6: return query.value("Saturday").toBool();
            case 7: return query.value("Sunday").toBool();
            }
        }
    }

    //两步校验 结束后
    //if (!bAccessPerson) 
    //  return false; //有一项禁止
        

    //默认没有通行权限，则任意时间段均可通行
    if(strStartTime.size() <= 0 || strEndTime.size() <= 0)
    {
    	return true;
    }


    LogD("%s %s[%d] start %s cur %s end %s ,day %d \n",__FILE__,__FUNCTION__,__LINE__,strStartTime.toStdString().c_str(),
    		strCurTime.toStdString().c_str(),strEndTime.toStdString().c_str(),QDate::currentDate().dayOfWeek());
    return false;
}


static inline int getPersonMaxid()
{
    QMutexLocker lock(&g_personIdMutex);
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    query.exec("select personid from person ORDER BY personid DESC LIMIT 1");
    while(query.next())
    {
        int maxId = query.value("personid").toInt();
        return maxId >= 1 ? maxId + 1 : 1; // Return 1 if maxId is 0 or negative
    }
    return 1; // Default to 1 if no records exist
}

bool RegisteredFacesDB::ProcessFaceRegistrationFromCroppedImage(const QString &employeeId)
{
    LogD("%s %s[%d] === Starting ProcessFaceRegistrationFromCroppedImage ===\n", 
         __FILE__, __FUNCTION__, __LINE__);
    LogD("%s %s[%d] Employee ID: %s\n", __FILE__, __FUNCTION__, __LINE__, employeeId.toStdString().c_str());
    
    if (employeeId.isEmpty()) {
        LogE("%s %s[%d] Employee ID is empty\n", __FILE__, __FUNCTION__, __LINE__);
        return false;
    }
    
    // Check if cropped image exists
    QString croppedImagePath = QString("/mnt/user/reg_face_image/%1.jpg").arg(employeeId);
    QFile imageFile(croppedImagePath);
    if (!imageFile.exists()) {
        LogE("%s %s[%d] Cropped image not found: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, croppedImagePath.toStdString().c_str());
        return false;
    }
    
    LogD("%s %s[%d] Cropped image found: %s (size: %lld bytes)\n", 
         __FILE__, __FUNCTION__, __LINE__, croppedImagePath.toStdString().c_str(), imageFile.size());
    
    // Extract features from the cropped image
    return ExtractAndStoreFaceFeatures(employeeId, croppedImagePath);  // Correct
}

bool RegisteredFacesDB::ExtractAndStoreFaceFeatures(const QString &employeeId, const QString &croppedImagePath)
{
    LogD("%s %s[%d] === Starting ExtractAndStoreFaceFeatures ===\n", __FILE__, __FUNCTION__, __LINE__);
    LogD("%s %s[%d] Employee ID: %s, Image: %s\n", 
         __FILE__, __FUNCTION__, __LINE__, employeeId.toStdString().c_str(), croppedImagePath.toStdString().c_str());
    
    // Get BaiduFaceManager instance
    BaiduFaceManager* faceManager = (BaiduFaceManager*)qXLApp->GetAlgoFaceManager();
    if (!faceManager) {
        LogE("%s %s[%d] BaiduFaceManager not available\n", __FILE__, __FUNCTION__, __LINE__);
        return false;
    }
    
    // Extract face features from cropped image
    QByteArray faceFeature;
    double quality = 0.0;
    
    LogD("%s %s[%d] Extracting features from cropped image...\n", __FILE__, __FUNCTION__, __LINE__);
    bool extractResult = faceManager->extractFeaturesFromImagePath(croppedImagePath, faceFeature, quality);
    
    if (!extractResult) {
        LogE("%s %s[%d] Failed to extract features from cropped image\n", __FILE__, __FUNCTION__, __LINE__);
        return false;
    }
    
    if (faceFeature.isEmpty()) {
        LogE("%s %s[%d] Extracted feature is empty\n", __FILE__, __FUNCTION__, __LINE__);
        return false;
    }
    
    LogD("%s %s[%d] Features extracted successfully: %d bytes, quality: %f\n", 
         __FILE__, __FUNCTION__, __LINE__, faceFeature.size(), quality);
    
    // Read the cropped image data for database storage
    QFile imageFile(croppedImagePath);
    QByteArray imageData;
    if (imageFile.open(QIODevice::ReadOnly)) {
        imageData = imageFile.readAll();
        imageFile.close();
        LogD("%s %s[%d] Image data read: %d bytes\n", 
             __FILE__, __FUNCTION__, __LINE__, imageData.size());
    } else {
        LogE("%s %s[%d] Failed to read cropped image data\n", __FILE__, __FUNCTION__, __LINE__);
        return false;
    }
    
    // Find the user in database by employee ID
    QString uuid = findUuidByEmployeeId(employeeId);
    if (uuid.isEmpty()) {
        LogE("%s %s[%d] User not found in database for employee ID: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, employeeId.toStdString().c_str());
        return false;
    }
    
    LogD("%s %s[%d] Found user UUID: %s for employee ID: %s\n", 
         __FILE__, __FUNCTION__, __LINE__, uuid.toStdString().c_str(), employeeId.toStdString().c_str());
    
    // Update the user with face features and image data
    bool updateResult = UpdatePersonFaceFeature(uuid, faceFeature, imageData, "jpg");
    
    if (updateResult) {
        LogD("%s %s[%d] === Face features successfully stored in database ===\n", __FILE__, __FUNCTION__, __LINE__);
        LogD("%s %s[%d] Employee: %s, UUID: %s, Feature size: %d bytes\n", 
             __FILE__, __FUNCTION__, __LINE__, employeeId.toStdString().c_str(), 
             uuid.toStdString().c_str(), faceFeature.size());
    } else {
        LogE("%s %s[%d] Failed to update user with face features\n", __FILE__, __FUNCTION__, __LINE__);
    }
    
    return updateResult;
}

bool RegisteredFacesDB::UpdateUserWithCroppedImageFeatures(const QString &employeeId)
{
    LogD("%s %s[%d] === Starting UpdateUserWithCroppedImageFeatures ===\n", __FILE__, __FUNCTION__, __LINE__);
    LogD("%s %s[%d] Employee ID: %s\n", __FILE__, __FUNCTION__, __LINE__, employeeId.toStdString().c_str());
    
    // This is a convenience function that combines image checking and feature extraction
    QString croppedImagePath = QString("/mnt/user/reg_face_image/%1.jpg").arg(employeeId);
    
    // Verify image exists and is valid
    QFileInfo imageInfo(croppedImagePath);
    if (!imageInfo.exists()) {
        LogE("%s %s[%d] Cropped image does not exist: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, croppedImagePath.toStdString().c_str());
        return false;
    }
    
    if (imageInfo.size() < 1000) { // Less than 1KB probably indicates a problem
        LogE("%s %s[%d] Cropped image too small (likely corrupted): %lld bytes\n", 
             __FILE__, __FUNCTION__, __LINE__, imageInfo.size());
        return false;
    }
    
    LogD("%s %s[%d] Cropped image validated: %s (%lld bytes)\n", 
         __FILE__, __FUNCTION__, __LINE__, croppedImagePath.toStdString().c_str(), imageInfo.size());
    
    // Extract and store features
    return ExtractAndStoreFaceFeatures(employeeId, croppedImagePath);
}

bool RegisteredFacesDB::ProcessFaceRegistrationComplete(const QString &employeeId, const QByteArray &liveFeature)
{
    LogD("%s %s[%d] === Starting ProcessFaceRegistrationComplete ===\n", __FILE__, __FUNCTION__, __LINE__);
    LogD("%s %s[%d] Employee ID: %s, Live feature size: %d\n", 
         __FILE__, __FUNCTION__, __LINE__, employeeId.toStdString().c_str(), liveFeature.size());
    
    bool preferCroppedFeatures = true; // Flag to prefer cropped image features over live features
    
    // Method 1: Try to use features from cropped image (higher quality)
    if (preferCroppedFeatures) {
        LogD("%s %s[%d] Attempting to use cropped image features...\n", __FILE__, __FUNCTION__, __LINE__);
        
        bool croppedResult = ProcessFaceRegistrationFromCroppedImage(employeeId);
        if (croppedResult) {
            LogD("%s %s[%d] Successfully used cropped image features\n", __FILE__, __FUNCTION__, __LINE__);
            return true;
        } else {
            LogD("%s %s[%d] Cropped image features failed, falling back to live features\n", 
                 __FILE__, __FUNCTION__, __LINE__);
        }
    }
    
    // Method 2: Fallback to live features if cropped image method failed
    if (!liveFeature.isEmpty()) {
        LogD("%s %s[%d] Using live capture features as fallback\n", __FILE__, __FUNCTION__, __LINE__);
        
        QString uuid = findUuidByEmployeeId(employeeId);
        if (!uuid.isEmpty()) {
            // Try to get cropped image data for storage
            QString croppedImagePath = QString("/mnt/user/reg_face_image/%1.jpg").arg(employeeId);
            QByteArray imageData;
            
            QFile imageFile(croppedImagePath);
            if (imageFile.open(QIODevice::ReadOnly)) {
                imageData = imageFile.readAll();
                imageFile.close();
                LogD("%s %s[%d] Using cropped image data with live features\n", __FILE__, __FUNCTION__, __LINE__);
            } else {
                LogD("%s %s[%d] No cropped image data available for live features\n", __FILE__, __FUNCTION__, __LINE__);
            }
            
            bool result = UpdatePersonFaceFeature(uuid, liveFeature, imageData, "jpg");
            if (result) {
                LogD("%s %s[%d] Successfully used live features as fallback\n", __FILE__, __FUNCTION__, __LINE__);
            }
            return result;
        } else {
            LogE("%s %s[%d] Could not find UUID for employee ID: %s\n", 
                 __FILE__, __FUNCTION__, __LINE__, employeeId.toStdString().c_str());
        }
    }
    
    LogE("%s %s[%d] Both cropped and live feature methods failed\n", __FILE__, __FUNCTION__, __LINE__);
    return false;
}

bool RegisteredFacesDB::RegPersonToDBAndRAM(const QString &uuid, const QString &Name, const QString &idCard, 
                                          const QString &icCard, const QString &Sex, const QString &department, 
                                          const QString &timeOfAccess, const QByteArray &FaceFeature, 
                                          const QByteArray &faceImageData, const QString &imageFormat,
                                          const QString &attendanceMode, const QString &tenantId, 
                                          const QString &id, const QString &status,
                                          int personalModuleId)  // ✅ ADD PARAMETER
{
    qDebug() << "=== DB_PERSONALMODULE: RegPersonToDBAndRAM START ===";
    qDebug() << "DB_PERSONALMODULE: employeeId:" << idCard;
    qDebug() << "DB_PERSONALMODULE: name:" << Name;
    qDebug() << "DB_PERSONALMODULE: personalModuleId:" << personalModuleId;
    
    Q_D(RegisteredFacesDB);
    QMutexLocker lock(&d->sync);

    LogD("%s %s[%d] Starting RegPersonToDBAndRAM for idCard: %s, Name: %s, personalModuleId: %d\n", 
         __FILE__, __FUNCTION__, __LINE__, idCard.toStdString().c_str(), Name.toStdString().c_str(), personalModuleId);
    
    // Ensure database schema is up to date
    createOrUpdateDatabaseSchema();
    
    // Save face image first (existing logic)
    bool imageSaved = false;
    QString imagePath = QString("/mnt/user/");
    
    if (!faceImageData.isEmpty() && !imageFormat.isEmpty()) {
        qDebug() << "DB_PERSONALMODULE: Saving face image...";
        imageSaved = saveFaceImage(idCard, faceImageData, imageFormat);
        if (imageSaved) {
            imagePath = getFaceImagePath(idCard, imageFormat);
            qDebug() << "DB_PERSONALMODULE: Face image saved to:" << imagePath;
        }
    }

    // Check if user already exists
    QSqlQuery checkQuery(QSqlDatabase::database("isc_arcsoft_face"));
    checkQuery.prepare("SELECT personid, uuid, feature, image FROM person WHERE idcardnum = ?");
    checkQuery.bindValue(0, idCard);
    
    bool userExists = false;
    int existingPersonId = 1;
    QString existingUuid = "";
    QByteArray existingFeature;
    QString existingImage = "";
    
    if (checkQuery.exec() && checkQuery.next()) {
        userExists = true;
        existingPersonId = checkQuery.value("personid").toInt();
        existingUuid = checkQuery.value("uuid").toString();
        existingFeature = checkQuery.value("feature").toByteArray();
        existingImage = checkQuery.value("image").toString();
        qDebug() << "DB_PERSONALMODULE: User exists in database";
    }
    
    PERSONS_t t{};
    // FIX: Preserve existing feature and image if they are not provided in this update
    if (FaceFeature.isEmpty() && userExists && !existingFeature.isEmpty()) {
        t.feature = existingFeature;
    } else {
        t.feature = FaceFeature;
    }
    
    if (!imageSaved && userExists && !existingImage.isEmpty() && existingImage != "/mnt/user/") {
        imagePath = existingImage;
    }
    
    // Look up the user in RAM to preserve metadata that isn't provided in this update
    bool foundInRam = false;
    PERSONS_t existingRamUser;
    for (int i = 0; i < d->mPersons.size(); i++) {
        if (d->mPersons[i].idcard == idCard) {
            existingRamUser = d->mPersons[i];
            foundInRam = true;
            break;
        }
    }
    
    t.name = Name.isEmpty() && foundInRam ? existingRamUser.name : Name;
    t.sex = Sex.isEmpty() && foundInRam ? existingRamUser.sex : (Sex.isEmpty() ? "Unknown" : Sex);
    t.idcard = idCard;
    t.iccard = icCard.isEmpty() && foundInRam ? existingRamUser.iccard : (icCard.isEmpty() ? "000000" : icCard);
    
    // Set new column values (preserve existing if not provided)
    t.attendanceMode = attendanceMode.isEmpty() && foundInRam ? existingRamUser.attendanceMode : attendanceMode;
    t.tenantId = tenantId.isEmpty() && foundInRam ? existingRamUser.tenantId : tenantId;
    t.id = id.isEmpty() && foundInRam ? existingRamUser.id : id;
    t.status = status.isEmpty() && foundInRam ? existingRamUser.status : status;
    t.personalModuleId = personalModuleId == 0 && foundInRam ? existingRamUser.personalModuleId : personalModuleId;
    t.timeOfAccess = timeOfAccess.isEmpty() && foundInRam ? existingRamUser.timeOfAccess : timeOfAccess;
    t.department = department.isEmpty() && foundInRam ? existingRamUser.department : department;
    
    // Preserve biometrics and arrays that are not explicitly passed via RegPersonToDBAndRAM
    if (foundInRam) {
        t.fingerprint = existingRamUser.fingerprint;
        t.finger_id = existingRamUser.finger_id;
        t.qr_code = existingRamUser.qr_code;
        t.gids = existingRamUser.gids;
        t.pids = existingRamUser.pids;
        t.persontype = existingRamUser.persontype;
    } else {
        t.fingerprint = QByteArray();
        t.finger_id = 0;
        t.qr_code = QString();
        t.gids = QString("0");
        t.pids = QString("2");
        t.persontype = 0;
    }
    
    // Handle UUID assignment
    if (userExists) {
        t.uuid = existingUuid;
        t.personid = existingPersonId;
    } else {
        if (uuid.size() > 3) {
            t.uuid = uuid;
        } else {
            t.uuid = QUuid::createUuid().toString();
        }
        t.personid = getPersonMaxid();
    }
    
    // Use individual server time
    QString serverTime = getServerDateUpdatedTime();
    if (!serverTime.isEmpty()) {
        t.createtime = serverTime;
        qDebug() << "DB_PERSONALMODULE: Using server time:" << t.createtime;
    } else {
        t.createtime = QDateTime::currentDateTime().toString("yyyy/MM/dd HH:mm:ss");
        qDebug() << "DB_PERSONALMODULE: Using device time:" << t.createtime;
    }

    // Database insertion with personalModuleId
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    QString strSql;
    if (userExists) {
        strSql.append("UPDATE person SET ");
        strSql.append("persontype = ?, ");
        strSql.append("name = ?, ");
        strSql.append("sex = ?, ");
        strSql.append("image = ?, ");
        strSql.append("createtime = ?, ");
        strSql.append("iccardnum = ?, ");
        strSql.append("feature = ?, ");
        strSql.append("featuresize = ?, ");
        strSql.append("gids = ?, ");
        strSql.append("aids = ?, ");
        strSql.append("time_of_access = ?, ");
        strSql.append("department = ?, ");
        strSql.append("attendanceMode = ?, ");
        strSql.append("tenantId = ?, ");
        strSql.append("id = ?, ");
        strSql.append("status = ?, ");
        strSql.append("personalModuleId = ? ");
        strSql.append("WHERE idcardnum = ?");

        query.prepare(strSql);
        query.bindValue(0, t.persontype);
        query.bindValue(1, t.name);
        query.bindValue(2, t.sex);
        query.bindValue(3, imagePath);
        query.bindValue(4, t.createtime);
        query.bindValue(5, t.iccard);
        query.bindValue(6, t.feature);
        query.bindValue(7, FaceFeature.size());
        query.bindValue(8, t.gids);
        query.bindValue(9, t.pids);
        query.bindValue(10, t.timeOfAccess);
        query.bindValue(11, t.department);
        query.bindValue(12, t.attendanceMode);
        query.bindValue(13, t.tenantId);
        query.bindValue(14, t.id);
        query.bindValue(15, t.status);
        query.bindValue(16, t.personalModuleId);
        query.bindValue(17, t.idcard);
    } else {
        strSql.append("INSERT INTO person");
        strSql.append("(personid,");
        strSql.append("uuid,");
        strSql.append("persontype,");
        strSql.append("name,");
        strSql.append("sex,");
        strSql.append("image,");
        strSql.append("createtime,");
        strSql.append("idcardnum,");
        strSql.append("iccardnum,");
        strSql.append("feature,");
        strSql.append("featuresize,");
        strSql.append("gids,");
        strSql.append("aids,");
        strSql.append("time_of_access,");
        strSql.append("department,");
        strSql.append("attendanceMode,");
        strSql.append("tenantId,");
        strSql.append("id,");
        strSql.append("status,");
        strSql.append("personalModuleId)");
        strSql.append(" VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
        
        query.prepare(strSql);
        query.bindValue(0, t.personid);
        query.bindValue(1, t.uuid);
        query.bindValue(2, t.persontype);
        query.bindValue(3, t.name);
        query.bindValue(4, t.sex);
        query.bindValue(5, imagePath);
        query.bindValue(6, t.createtime);
        query.bindValue(7, t.idcard);
        query.bindValue(8, t.iccard);
        query.bindValue(9, t.feature);
        query.bindValue(10, FaceFeature.size());
        query.bindValue(11, t.gids);
        query.bindValue(12, t.pids);
        query.bindValue(13, t.timeOfAccess);
        query.bindValue(14, t.department);
        query.bindValue(15, t.attendanceMode);
        query.bindValue(16, t.tenantId);
        query.bindValue(17, t.id);
        query.bindValue(18, t.status);
        query.bindValue(19, t.personalModuleId);
    }

    qDebug() << "DB_PERSONALMODULE: Executing with personalModuleId:" << t.personalModuleId;

    bool ret = query.exec();
    
    if (!ret) {
        QSqlError error = query.lastError();
        qDebug() << "DB_PERSONALMODULE: Database operation failed:" << error.text();
        LogE("%s %s[%d] ERROR: Database operation failed: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, error.text().toStdString().c_str());
        
        if (imageSaved) {
            deleteFaceImage(idCard, imageFormat);
        }
        return false;
    } else {
        qDebug() << "DB_PERSONALMODULE: SUCCESS with personalModuleId:" << t.personalModuleId;
    }

    // Update in-memory list
    if (userExists) {
        bool found = false;
        for (int i = 0; i < d->mPersons.size(); i++) {
            if (d->mPersons[i].idcard == idCard) {
                d->mPersons[i] = t;
                found = true;
                qDebug() << "DB_PERSONALMODULE: Updated memory with personalModuleId";
                break;
            }
        }
        if (!found) {
            d->mPersons.append(t);
        }
    } else {
        d->mPersons.append(t);
        qDebug() << "DB_PERSONALMODULE: Added to memory with personalModuleId";
    }

    // Update UI
    qXLApp->GetFaceMainFrm()->updateHome_PersonNum();

#ifdef Q_OS_LINUX
    system("sync");
#endif
    
    qDebug() << "=== DB_PERSONALMODULE: RegPersonToDBAndRAM SUCCESS ===";
    return ret;
}

// Updated StoreUserWithoutFaceData function with new columns
bool RegisteredFacesDB::StoreUserWithoutFaceData(const QString &uuid, const QString &Name, const QString &idCard, 
                                                const QString &icCard, const QString &Sex, const QString &department, 
                                                const QString &timeOfAccess, const QString &attendanceMode, 
                                                const QString &tenantId, const QString &id, const QString &status,
                                                int personalModuleId)  // ✅ ADD PARAMETER
{
    Q_D(RegisteredFacesDB);
    QMutexLocker lock(&d->sync);

    qDebug() << "DB_PERSONALMODULE: StoreUserWithoutFaceData - personalModuleId:" << personalModuleId;

    // Ensure database schema is up to date
    createOrUpdateDatabaseSchema();

    PERSONS_t t{};
    t.feature = QByteArray();
    t.name = Name;
    t.sex = Sex.isEmpty() ? "Unknown" : Sex;
    t.idcard = idCard;
    t.iccard = icCard.isEmpty() ? "000000" : icCard;
    
    // Set new column values
    t.attendanceMode = attendanceMode;
    t.tenantId = tenantId;
    t.id = id;
    t.status = status;
    t.personalModuleId = personalModuleId;  // ✅ STORE IT
    
    if(uuid.size() > 3) {
        t.uuid = uuid;
    } else {
        t.uuid = QUuid::createUuid().toString();
    }

    t.persontype = 0;
    t.personid = getPersonMaxid();
    t.gids = QString("0");
    t.pids = QString("2");
    t.timeOfAccess = timeOfAccess;
    t.createtime = QDateTime::currentDateTime().toString("yyyy/MM/dd hh:mm:ss");
    t.department = department;

    // Check if user exists in RAM
    bool bFind = false;
    for(int i = 0; i<d->mPersons.count(); i++) {
        if(d->mPersons.at(i).idcard == idCard || d->mPersons.at(i).uuid == t.uuid) {
            bFind = true;
            t.uuid = d->mPersons.at(i).uuid; // ensure uuid matches
            t.feature = d->mPersons.at(i).feature; // preserve feature
            t.personid = d->mPersons.at(i).personid;
            break;
        }
    }

    // Insert into database with personalModuleId
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    QString strSql;
    if (bFind) {
        // user exists, do UPDATE
        strSql.append("UPDATE person SET ");
        strSql.append("persontype = ?, ");
        strSql.append("name = ?, ");
        strSql.append("sex = ?, ");
        strSql.append("image = ?, ");
        strSql.append("createtime = ?, ");
        strSql.append("iccardnum = ?, ");
        strSql.append("feature = ?, ");
        strSql.append("featuresize = ?, ");
        strSql.append("gids = ?, ");
        strSql.append("aids = ?, ");
        strSql.append("time_of_access = ?, ");
        strSql.append("department = ?, ");
        strSql.append("attendanceMode = ?, ");
        strSql.append("tenantId = ?, ");
        strSql.append("id = ?, ");
        strSql.append("status = ?, ");
        strSql.append("personalModuleId = ? ");
        strSql.append("WHERE uuid = ?");

        query.prepare(strSql);
        query.bindValue(0, t.persontype);
        query.bindValue(1, t.name);
        query.bindValue(2, t.sex);
        query.bindValue(3, QString("/mnt/user/"));
        query.bindValue(4, t.createtime);
        query.bindValue(5, t.iccard);
        query.bindValue(6, t.feature);
        query.bindValue(7, t.feature.size()); // featuresize
        query.bindValue(8, t.gids);
        query.bindValue(9, t.pids);
        query.bindValue(10, t.timeOfAccess);
        query.bindValue(11, t.department);
        query.bindValue(12, t.attendanceMode);
        query.bindValue(13, t.tenantId);
        query.bindValue(14, t.id);
        query.bindValue(15, t.status);
        query.bindValue(16, t.personalModuleId);
        query.bindValue(17, t.uuid);
    } else {
        strSql.append("INSERT INTO person");
        strSql.append("(personid,");
        strSql.append("uuid,");
        strSql.append("persontype,");
        strSql.append("name,");
        strSql.append("sex,");
        strSql.append("image,");
        strSql.append("createtime,");
        strSql.append("idcardnum,");
        strSql.append("iccardnum,");
        strSql.append("feature,");
        strSql.append("featuresize,");
        strSql.append("gids,");
        strSql.append("aids,");
        strSql.append("time_of_access,");
        strSql.append("department,");
        strSql.append("attendanceMode,");
        strSql.append("tenantId,");
        strSql.append("id,");
        strSql.append("status,");
        strSql.append("personalModuleId)");
        strSql.append(" VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
        
        query.prepare(strSql);
        query.bindValue(0, t.personid);
        query.bindValue(1, t.uuid);
        query.bindValue(2, t.persontype);
        query.bindValue(3, t.name);
        query.bindValue(4, t.sex);
        query.bindValue(5, QString("/mnt/user/"));
        query.bindValue(6, t.createtime);
        query.bindValue(7, t.idcard);
        query.bindValue(8, t.iccard);
        query.bindValue(9, t.feature);
        query.bindValue(10, 0); // featuresize
        query.bindValue(11, t.gids);
        query.bindValue(12, t.pids);
        query.bindValue(13, t.timeOfAccess);
        query.bindValue(14, t.department);
        query.bindValue(15, t.attendanceMode);
        query.bindValue(16, t.tenantId);
        query.bindValue(17, t.id);
        query.bindValue(18, t.status);
        query.bindValue(19, t.personalModuleId);
    }
    
    LogD("%s %s[%d] Adding user without face data with personalModuleId: %d\n", 
         __FILE__, __FUNCTION__, __LINE__, personalModuleId);
    
    d->mPersons.append(t);

    bool ret = query.exec();
    if(ret == false) {
        QSqlError error = query.lastError();
        LogE("%s %s[%d] error %s \n", __FILE__, __FUNCTION__, __LINE__, error.text().toStdString().c_str());
        return false;
    } else {
        qXLApp->GetFaceMainFrm()->updateHome_PersonNum();
    }

#ifdef Q_OS_LINUX
    system("sync");
#endif
    return ret;
}

QString RegisteredFacesDB::getServerDateUpdatedTime()
{
    qDebug() << "DEBUG: getServerDateUpdatedTime - Checking for individual user server time";
    
    // *** PRIORITY 1: Use individual user's server time if set ***
    QString individualTime = getCurrentUserServerTime();
    if (!individualTime.isEmpty()) {
        qDebug() << "SUCCESS: getServerDateUpdatedTime - Using INDIVIDUAL user server time:" << individualTime;
        return individualTime;
    }
    
    // *** PRIORITY 2: Fallback to global server time from heartbeat ***
    QString serverTimeFile = "/mnt/user/sync_data/server_lastmodified.txt";
    QFile file(serverTimeFile);
    
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        QString serverTime = in.readLine().trimmed();
        QString source = in.readLine().trimmed();
        
        if (!serverTime.isEmpty() && source.contains("SERVER_DATE_UPDATED")) {
            file.close();
            qDebug() << "SUCCESS: getServerDateUpdatedTime - Using GLOBAL server time as fallback:" << serverTime;
            return serverTime;
        }
        file.close();
    }
    
    qDebug() << "DEBUG: getServerDateUpdatedTime - No server time found, will return empty string";
    return QString(); // Return empty string if no server time available
}

bool RegisteredFacesDB::createOrUpdateDatabaseSchema()
{
    LogD("%s %s[%d] === Checking and updating database schema ===\n", 
         __FILE__, __FUNCTION__, __LINE__);
    
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    
    // Check existing columns
    query.exec("PRAGMA table_info(person)");
    bool hasAttendanceMode = false;
    bool hasTenantId = false;
    bool hasId = false;
    bool hasStatus = false;
    bool hasFingerprint = false;
    bool hasFingerId = false;
    bool hasPersonalModuleId = false;  // ✅ CHECK FOR personalModuleId
    bool hasFingerprintJsonTimestamp = false;  // ✅ NEW
    bool hasFourDoorJsonTimestamp = false;     // ✅ NEW
    bool hasQrCode = false;                     // ✅ NEW
    
    while (query.next()) {
        QString columnName = query.value("name").toString();
        if (columnName == "attendanceMode") hasAttendanceMode = true;
        if (columnName == "tenantId") hasTenantId = true;
        if (columnName == "id") hasId = true;
        if (columnName == "status") hasStatus = true;
        if (columnName == "fingerprint") hasFingerprint = true;
        if (columnName == "finger_id") hasFingerId = true;
        if (columnName == "personalModuleId") hasPersonalModuleId = true;  // ✅ CHECK
        if (columnName == "fingerprint_json_last_modified") hasFingerprintJsonTimestamp = true;  // ✅
        if (columnName == "four_door_controller_json_last_modified") hasFourDoorJsonTimestamp = true;  // ✅
        if (columnName == "qr_code") hasQrCode = true;  // ✅
    }
    
    bool needsUpdate = false;
    
    // Add missing columns
    if (!hasAttendanceMode) {
        query.exec("ALTER TABLE person ADD COLUMN attendanceMode TEXT");
        LogD("%s %s[%d] Added attendanceMode column\n", __FILE__, __FUNCTION__, __LINE__);
        needsUpdate = true;
    }
    
    if (!hasTenantId) {
        query.exec("ALTER TABLE person ADD COLUMN tenantId TEXT");
        LogD("%s %s[%d] Added tenantId column\n", __FILE__, __FUNCTION__, __LINE__);
        needsUpdate = true;
    }
    
    if (!hasId) {
        query.exec("ALTER TABLE person ADD COLUMN id TEXT");
        LogD("%s %s[%d] Added id column\n", __FILE__, __FUNCTION__, __LINE__);
        needsUpdate = true;
    }
    
    if (!hasStatus) {
        query.exec("ALTER TABLE person ADD COLUMN status TEXT");
        LogD("%s %s[%d] Added status column\n", __FILE__, __FUNCTION__, __LINE__);
        needsUpdate = true;
    }
    
    if (!hasFingerprint) {
        query.exec("ALTER TABLE person ADD COLUMN fingerprint BLOB");
        LogD("%s %s[%d] Added fingerprint column\n", __FILE__, __FUNCTION__, __LINE__);
        needsUpdate = true;
    }
    
    if (!hasFingerId) {
        query.exec("ALTER TABLE person ADD COLUMN finger_id INTEGER DEFAULT -1");
        LogD("%s %s[%d] Added finger_id column\n", __FILE__, __FUNCTION__, __LINE__);
        needsUpdate = true;
    }
    
    // ✅ ADD personalModuleId COLUMN
    if (!hasPersonalModuleId) {
        query.exec("ALTER TABLE person ADD COLUMN personalModuleId INTEGER DEFAULT 0");
        LogD("%s %s[%d] Added personalModuleId column\n", __FILE__, __FUNCTION__, __LINE__);
        qDebug() << "DB_SCHEMA: Added personalModuleId column to database";
        needsUpdate = true;
    }

     if (!hasFingerprintJsonTimestamp) {
        query.exec("ALTER TABLE person ADD COLUMN fingerprint_json_last_modified TEXT");
        LogD("%s %s[%d] Added fingerprint_json_last_modified column\n", __FILE__, __FUNCTION__, __LINE__);
        qDebug() << "DB_SCHEMA: Added fingerprint_json_last_modified column";
        needsUpdate = true;
    }
    
    // ✅ ADD FOUR DOOR CONTROLLER JSON TIMESTAMP COLUMN
    if (!hasFourDoorJsonTimestamp) {
        query.exec("ALTER TABLE person ADD COLUMN four_door_controller_json_last_modified TEXT");
        LogD("%s %s[%d] Added four_door_controller_json_last_modified column\n", __FILE__, __FUNCTION__, __LINE__);
        qDebug() << "DB_SCHEMA: Added four_door_controller_json_last_modified column";
        needsUpdate = true;
    }
    
    // ✅ ADD QR CODE COLUMN
    if (!hasQrCode) {
        query.exec("ALTER TABLE person ADD COLUMN qr_code TEXT");
        LogD("%s %s[%d] Added qr_code column\n", __FILE__, __FUNCTION__, __LINE__);
        qDebug() << "DB_SCHEMA: Added qr_code column for four_door_controller.json";
        needsUpdate = true;
    }
    
    if (needsUpdate) {
        LogD("%s %s[%d] Database schema updated successfully\n", 
             __FILE__, __FUNCTION__, __LINE__);
    } else {
        LogD("%s %s[%d] Database schema is up to date\n", 
             __FILE__, __FUNCTION__, __LINE__);
    }
    
    return true;
}


void RegisteredFacesDB::setCurrentUserServerTime(const QString& serverTime)
{
    m_currentUserServerTime = serverTime;
    qDebug() << "DEBUG: setCurrentUserServerTime - Set to:" << serverTime;
}

QString RegisteredFacesDB::getCurrentUserServerTime()
{
    qDebug() << "DEBUG: getCurrentUserServerTime - Returning:" << m_currentUserServerTime;
    return m_currentUserServerTime;
}


QString RegisteredFacesDB::getGlobalServerDateUpdatedTime()
{
    // Read global server date_updated time from heartbeat (existing file)
    QString serverTimeFile = "/mnt/user/sync_data/server_lastmodified.txt";
    QFile file(serverTimeFile);
    
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        QString serverTime = in.readLine().trimmed();
        QString source = in.readLine().trimmed();
        
        if (!serverTime.isEmpty() && source.contains("SERVER_DATE_UPDATED")) {
            file.close();
            qDebug() << "SUCCESS: getGlobalServerDateUpdatedTime - Found global server time:" << serverTime;
            return serverTime;
        }
        file.close();
    }
    
    return QString();
}

QString RegisteredFacesDB::getMostRecentUserServerTime()
{
    QString userTimeDir = "/mnt/user/sync_data/user_times/";
    QDir dir(userTimeDir);
    
    if (!dir.exists()) {
        qDebug() << "DEBUG: getMostRecentUserServerTime - User times directory does not exist";
        return QString();
    }
    
    QFileInfoList files = dir.entryInfoList(QStringList("*_server_time.txt"), QDir::Files);
    
    QDateTime mostRecentDateTime;
    QString mostRecentTimeString;
    
    qDebug() << "DEBUG: getMostRecentUserServerTime - Found" << files.size() << "user time files";
    
    for (const QFileInfo& fileInfo : files) {
        QFile file(fileInfo.absoluteFilePath());
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            QString timeString = in.readLine().trimmed();
            
            if (!timeString.isEmpty()) {
                QDateTime dateTime = QDateTime::fromString(timeString, "yyyy/MM/dd HH:mm:ss");
                if (dateTime.isValid() && (!mostRecentDateTime.isValid() || dateTime > mostRecentDateTime)) {
                    mostRecentDateTime = dateTime;
                    mostRecentTimeString = timeString;
                    
                    qDebug() << "DEBUG: getMostRecentUserServerTime - Updated most recent time:" << timeString;
                }
            }
            file.close();
        }
    }
    
    if (!mostRecentTimeString.isEmpty()) {
        qDebug() << "SUCCESS: getMostRecentUserServerTime - Most recent server time:" << mostRecentTimeString;
    }
    
    return mostRecentTimeString;
}

int RegisteredFacesDB::UpdatePersonToDBAndRAM(const QString &uuid, const QString &name, const QString &idCard, 
                                              const QString &icCard, const QString &sex, const QString &department, 
                                              const QString &timeOfAccess, const QString &jpeg, 
                                              const QByteArray &faceEmbedding, const QString &attendanceMode, 
                                              const QString &tenantId, const QString &id, const QString &status,
                                              int personalModuleId)  // ✅ ADD PARAMETER
{
    Q_D(RegisteredFacesDB);
    QMutexLocker lock(&d->sync);
    int result = 0;
    PERSONS_t stPerson = {0};
    QByteArray faceFeatureFinal;

    qDebug() << "=== DB_UPDATE_PERSONALMODULE: UpdatePersonToDBAndRAM START ===";
    qDebug() << "DB_UPDATE_PERSONALMODULE: uuid:" << uuid;
    qDebug() << "DB_UPDATE_PERSONALMODULE: personalModuleId:" << personalModuleId;

    // Ensure database schema is up to date
    createOrUpdateDatabaseSchema();

    LogV("%s %s[%d] uuid %s personalModuleId %d\n", __FILE__, __FUNCTION__, __LINE__, 
         uuid.toStdString().c_str(), personalModuleId);
    
    if(uuid.size() <= 0) {
        return -1;
    }

    int index = -1;
    for(int i = 0; i < d->mPersons.size(); i++) {
        auto &t = d->mPersons.at(i);
        if(t.uuid == uuid) {
            index = i;
            stPerson = t;
            break;
        }
    }

    if(index < 0) {
        return ISC_UPDATE_PERSON_NOT_EXIST;
    }

    // Update person data including personalModuleId
    stPerson.uuid = uuid;
    if(name.size() > 0) stPerson.name = name;
    if(idCard.size() > 0) stPerson.idcard = idCard;
    if(icCard.size() > 0) stPerson.iccard = icCard;
    if(sex.size() > 0) stPerson.sex = sex;
    if(department.size() > 0) stPerson.department = department;
    if(timeOfAccess.size() > 0) stPerson.timeOfAccess = timeOfAccess;
    
    // Update new columns
    if(attendanceMode.size() > 0) stPerson.attendanceMode = attendanceMode;
    if(tenantId.size() > 0) stPerson.tenantId = tenantId;
    if(id.size() > 0) stPerson.id = id;
    if(status.size() > 0) stPerson.status = status;
    
    // ✅ UPDATE personalModuleId (always update if provided, even if 0)
    stPerson.personalModuleId = personalModuleId;
    qDebug() << "DB_UPDATE_PERSONALMODULE: Updated personalModuleId to:" << personalModuleId;

    // Face feature processing (existing logic)
    bool hasProvidedEmbedding = !faceEmbedding.isEmpty();
    bool hasImagePath = (jpeg.size() > 0 && !access(jpeg.toStdString().c_str(), F_OK));
    
    if (hasProvidedEmbedding) {
        faceFeatureFinal = faceEmbedding;
        result = 0;
    } else if (hasImagePath) {
        int faceNum = 0;
        double threshold = 0;
        QByteArray extractedFeature;
        result = ((BaiduFaceManager *)qXLApp->GetAlgoFaceManager())->RegistPerson(jpeg, faceNum, threshold, extractedFeature);
        if(result == 0 && !extractedFeature.isEmpty()) {
            faceFeatureFinal = extractedFeature;
        } else {
            return result - 10;
        }
    } else {
        faceFeatureFinal = stPerson.feature;  // FIX: Preserve existing face feature instead of wiping it out with QByteArray()
        result = 0;
    }

    stPerson.feature = faceFeatureFinal;

    // Use individual server time
    QString serverTime = getServerDateUpdatedTime();
    if (!serverTime.isEmpty()) {
        stPerson.createtime = serverTime;
    } else {
        stPerson.createtime = QDateTime::currentDateTime().toString("yyyy/MM/dd HH:mm:ss");
    }

    // Database update with personalModuleId
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    QString strSql;
    strSql.append("UPDATE person SET ");
    strSql.append("name=?,");
    strSql.append("sex=?,");
    strSql.append("idcardnum=?,");
    strSql.append("iccardnum=?,");
    strSql.append("feature=?,");
    strSql.append("featuresize=?,");
    strSql.append("time_of_access=?,");
    strSql.append("department=?,");
    strSql.append("createtime=?,");
    strSql.append("attendanceMode=?,");
    strSql.append("tenantId=?,");
    strSql.append("id=?,");
    strSql.append("status=?,");
    strSql.append("personalModuleId=?");  // ✅ ADD COLUMN
    strSql.append(" where uuid=?");
    
    query.prepare(strSql);
    query.bindValue(0, stPerson.name);
    query.bindValue(1, stPerson.sex);
    query.bindValue(2, stPerson.idcard);
    query.bindValue(3, stPerson.iccard);
    query.bindValue(4, stPerson.feature);
    query.bindValue(5, stPerson.feature.size());
    query.bindValue(6, stPerson.timeOfAccess);
    query.bindValue(7, stPerson.department);
    query.bindValue(8, stPerson.createtime);
    query.bindValue(9, stPerson.attendanceMode);
    query.bindValue(10, stPerson.tenantId);
    query.bindValue(11, stPerson.id);
    query.bindValue(12, stPerson.status);
    query.bindValue(13, stPerson.personalModuleId);  // ✅ BIND VALUE
    query.bindValue(14, stPerson.uuid);
    
    qDebug() << "DB_UPDATE_PERSONALMODULE: Executing update with personalModuleId:" << stPerson.personalModuleId;
    
    if(query.exec() == false) {
        QSqlError error = query.lastError();
        qDebug() << "DB_UPDATE_PERSONALMODULE: Database update FAILED:" << error.text();
        LogE("%s %s[%d] error %s \n",__FILE__,__FUNCTION__,__LINE__,error.text().toStdString().c_str());
        return ISC_UPDATE_PERSON_DB_ERROR;
    } else {
        qDebug() << "DB_UPDATE_PERSONALMODULE: Database update SUCCESS!";
    }
    
    // Update in-memory list
    d->mPersons.replace(index, stPerson);
    
    qDebug() << "DB_UPDATE_PERSONALMODULE: Updated in-memory person with personalModuleId:" << personalModuleId;

#ifdef Q_OS_LINUX
    system("sync");
#endif
    
    qDebug() << "=== DB_UPDATE_PERSONALMODULE: UpdatePersonToDBAndRAM SUCCESS ===";
    
    if (result == 0)
        return 1;
    else 
        return result;
}


bool RegisteredFacesDB::UpdatePersonDirectly(const QString &uuid, const QString &name, const QString &idCard, 
                                           const QString &sex, const QByteArray &faceFeature)
{
    qDebug() << "=== DB_FACE_DEBUG: UpdatePersonDirectly START (with server time) ===";
    qDebug() << "DB_FACE_DEBUG: Update parameters:";
    qDebug() << "  uuid:" << uuid;
    qDebug() << "  name:" << name;
    qDebug() << "  idCard:" << idCard;
    qDebug() << "  sex:" << sex;
    qDebug() << "  faceFeature.size():" << faceFeature.size();
    
    Q_D(RegisteredFacesDB);
    QMutexLocker lock(&d->sync);
    
    // Check current state (existing logic)
    QSqlQuery preCheckQuery(QSqlDatabase::database("isc_arcsoft_face"));
    preCheckQuery.prepare("SELECT name, idcardnum, sex, feature, featuresize FROM person WHERE uuid = ?");
    preCheckQuery.bindValue(0, uuid);
    
    QByteArray currentFeature;
    bool hasCurrentFeature = false;
    
    if (preCheckQuery.exec() && preCheckQuery.next()) {
        currentFeature = preCheckQuery.value("feature").toByteArray();
        hasCurrentFeature = !currentFeature.isEmpty();
        qDebug() << "DB_FACE_DEBUG: Current feature size:" << currentFeature.size();
    }
    
    // Decide which face feature to use (existing logic)
    QByteArray featureToStore;
    QString updateStrategy;
    
    if (!faceFeature.isEmpty()) {
        featureToStore = faceFeature;
        updateStrategy = "USE_NEW_FEATURE";
        qDebug() << "DB_FACE_DEBUG: Using new face feature";
    } else if (hasCurrentFeature) {
        featureToStore = currentFeature;
        updateStrategy = "PRESERVE_EXISTING_FEATURE";
        qDebug() << "DB_FACE_DEBUG: Preserving existing face feature";
    } else {
        featureToStore = QByteArray();
        updateStrategy = "NO_FEATURE_DATA";
        qDebug() << "DB_FACE_DEBUG: No face feature data available";
    }
    
    // *** MODIFIED: Use server date_updated time instead of current device time ***
    QString updateTime = getServerDateUpdatedTime();
    if (updateTime.isEmpty()) {
        updateTime = QDateTime::currentDateTime().toString("yyyy/MM/dd HH:mm:ss");
        qDebug() << "DB_FACE_DEBUG: Using DEVICE time as fallback:" << updateTime;
    } else {
        qDebug() << "DB_FACE_SUCCESS: Using SERVER date_updated time:" << updateTime;
    }
    
    // Execute update with server time
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    QString updateSql = "UPDATE person SET name = ?, idcardnum = ?, sex = ?, feature = ?, featuresize = ?, createtime = ? WHERE uuid = ?";
    query.prepare(updateSql);
    query.bindValue(0, name);
    query.bindValue(1, idCard);
    query.bindValue(2, sex);
    query.bindValue(3, featureToStore);
    query.bindValue(4, featureToStore.size());
    query.bindValue(5, updateTime);  // *** SERVER TIME STORED HERE ***
    query.bindValue(6, uuid);
    
    qDebug() << "DB_FACE_DEBUG: UPDATE query with SERVER time:" << updateTime;
    
    bool success = query.exec();
    
    if (success) {
        qDebug() << "DB_FACE_SUCCESS: Update query executed with SERVER time!";
    } else {
        QSqlError error = query.lastError();
        qDebug() << "DB_FACE_ERROR: Update query failed:" << error.text();
        return false;
    }
    
    // Update in-memory list (existing logic but with server time)
    for (int i = 0; i < d->mPersons.size(); i++) {
        if (d->mPersons[i].uuid == uuid) {
            d->mPersons[i].name = name;
            d->mPersons[i].idcard = idCard;
            d->mPersons[i].sex = sex;
            d->mPersons[i].feature = featureToStore;
            d->mPersons[i].createtime = updateTime;  // *** SERVER TIME STORED HERE ***
            
            qDebug() << "DB_FACE_DEBUG: Updated person in memory with SERVER time:" << updateTime;
            break;
        }
    }
    
    qDebug() << "DB_FACE_SUCCESS: UpdatePersonDirectly completed with SERVER date_updated time!";
    qDebug() << "=== DB_FACE_DEBUG: UpdatePersonDirectly END ===";
    return success;
}


bool RegisteredFacesDB::RegPersonToDBAndRAM(const QString &name, const QString &sex, const QString &nation, const QString &idcardnum, const QString &Marital, const QString &education, const QString &location, const QString &phone, const QString &politics_status, const QString &date_birth, const QString &number, const QString &branch, const QString &hiredate, const QString &extension_phone, const QString &mailbox, const QString &status, const QString &persontype, const QString &iccardnum, const QByteArray &FaceFeature)
{
    return this->RegPersonToDBAndRAM("",name, idcardnum, iccardnum, sex, "","",FaceFeature);
}

bool RegisteredFacesDB::ComparisonPersonFaceFeature_baidu(QString &name, QString &sex, QString &idcard, QString &iccard, QString &uuid, int &persontype, 
   int &personid, QString &gids, QString &pids, QString &id, unsigned char *FaceFeature, int FaceFeatureSize)
{
    Q_D(RegisteredFacesDB);
    QMutexLocker lock(&d->sync);

    

    gAlogStateFaceSimilar = 0;
    
    
    for(int i = 0; i < d->mPersons.count(); i++)
    {
        
        auto &t = d->mPersons.at(i);
        
        // Validate person data
        if (t.feature.isEmpty() || t.feature.size() <= 0) {
            continue;
        }
        
        double similar = ((BaiduFaceManager *)qXLApp->GetAlgoFaceManager())->getFaceFeatureCompare_baidu(
            (uchar *)t.feature.data(), t.feature.size(), FaceFeature, FaceFeatureSize);
        
        if (similar > gAlogStateFaceSimilar) {
            gAlogStateFaceSimilar = similar;
        }

        if (similar >= d->mThanthreshold) // 0.8
        {
            
            name = t.name;
            sex = t.sex;
            idcard = t.idcard;
            iccard = t.iccard;
            uuid = t.uuid;
            persontype = t.persontype;
            personid = t.personid;
            gids = t.gids;
            pids = t.pids;
            id = t.id;
            
            return true;
        }
        
    }

    return false;
}   

bool RegisteredFacesDB::ComparisonPersonFaceFeature(QString &name, QString &sex, QString &idcard, QString &iccard, QString &uuid, int &persontype, int &personid, QString &gids, QString &pids, const QByteArray &FaceFeature)
{
    Q_D(RegisteredFacesDB);
    QMutexLocker lock(&d->sync);

    gAlogStateFaceSimilar = 0;
    for(int i = 0; i<d->mPersons.count(); i++)
    {
        auto &t = d->mPersons.at(i);
     

        double similar = ((BaiduFaceManager *)qXLApp->GetAlgoFaceManager())->getFaceFeatureCompare((uchar *)t.feature.data(), t.feature.size(), FaceFeature);
        if(similar > gAlogStateFaceSimilar)
        {
        	gAlogStateFaceSimilar = similar;
        }
        if(similar>=d->mThanthreshold)
        {
            name = t.name;
            sex = t.sex;
            idcard = t.idcard;
            iccard = t.iccard;
            uuid = t.uuid;
            persontype = t.persontype;
            personid = t.personid;
            gids = t.gids;
            pids = t.pids;
            return true;
        }
    }
    return false;
}

bool RegisteredFacesDB::DelPersonDBInfo(const QString &name, const QString &createtime)
{/*删除数据库信息*/
    Q_D(RegisteredFacesDB);
    QMutexLocker lock(&d->sync);
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    query.prepare(QString("DELETE FROM person WHERE name='%1' AND createtime='%2'")
                  .arg(name).arg(createtime));
    bool ret = query.exec();

    for(int i = 0; i<d->mPersons.count(); i++)
    {
        auto &t = d->mPersons.at(i);

        if(t.name == name && t.createtime == createtime)
        {
            d->mPersons.removeAt(i);

            qXLApp->GetFaceMainFrm()->updateHome_PersonNum();
            return ret;
        }
    }

    return ret;
}

bool RegisteredFacesDB::QueryNameRepetition(const QString &name) const
{
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    query.exec(QString("select name from person where name='%1'").arg(name));
    while(query.next())
    {
        return true;
    }
    return false;
}

void RegisteredFacesDB::run()
{
    Q_D(RegisteredFacesDB);
    while (!isInterruptionRequested())
    {
        d->sync.lock();
        if (d->is_pause) d->pauseCond.wait(&d->sync);
        
        // Ensure database schema is up to date when loading
        createOrUpdateDatabaseSchema();
        
        QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
        
        // Migrate old data if needed
        QSqlQuery migrateQuery(QSqlDatabase::database("isc_arcsoft_face"));
        migrateQuery.exec("SELECT COUNT(*) FROM person WHERE personid = 0");
        if (migrateQuery.next() && migrateQuery.value(0).toInt() > 0) {
            LogD("%s %s[%d] Migrating person IDs from 0 to 1\n", __FILE__, __FUNCTION__, __LINE__);
            migrateQuery.exec("UPDATE person SET personid = 1 WHERE personid = 0");
        }
        
        query.exec("select * from person");
        while(query.next())
        {
            PERSONS_t t{};
            t.feature = query.value("feature").toByteArray();
            t.name = query.value("name").toString();
            t.sex = query.value("sex").toString();
            if(t.sex == "") {
                t.sex = "unknow";
            }
            t.idcard = query.value("idcardnum").toString();
            t.iccard = query.value("iccardnum").toString();
            t.uuid = query.value("uuid").toString();
            t.persontype = query.value("persontype").toInt();
            t.personid = query.value("personid").toInt();
            if (t.personid < 1) {
                t.personid = 1;
            }
            t.gids = query.value("gids").toString();
            t.pids = query.value("aids").toString();
            t.createtime = query.value("createtime").toString();
            t.timeOfAccess = query.value("time_of_access").toString();
            t.department = query.value("department").toString();
            
            // Load existing columns
            t.attendanceMode = query.value("attendanceMode").toString();
            t.tenantId = query.value("tenantId").toString();
            t.id = query.value("id").toString();
            t.status = query.value("status").toString();
            
            // Load fingerprint data
            t.fingerprint = query.value("fingerprint").toByteArray();
            t.finger_id = query.value("finger_id").toInt();
            
            // ✅ LOAD personalModuleId FROM DATABASE
            t.personalModuleId = query.value("personalModuleId").toInt();
            
            // Debug log for first few entries
            static int loadCount = 0;
            if (loadCount < 5) {
                qDebug() << "DB_LOAD: Employee" << t.idcard 
                         << "personalModuleId:" << t.personalModuleId;
                loadCount++;
            }

            d->mPersons.append(t);
        }
        
        qDebug() << "DB_LOAD: Loaded" << d->mPersons.size() << "persons with personalModuleId";
        
        d->is_pause = true;
        d->sync.unlock();
    }
}

PERSONS_t RegisteredFacesDB::getPersonByUuid(const QString &uuid)
{
    Q_D(RegisteredFacesDB);
    QMutexLocker lock(&d->sync);
    
    for (int i = 0; i < d->mPersons.size(); i++) {
        if (d->mPersons[i].uuid == uuid) {
            return d->mPersons[i];
        }
    }
    
    return PERSONS_t{}; // Return empty struct if not found
}

// Helper function to update only new columns
bool RegisteredFacesDB::updatePersonNewColumns(const QString &uuid, const QString &attendanceMode, 
                                             const QString &tenantId, const QString &id, const QString &status)
{
    Q_D(RegisteredFacesDB);
    QMutexLocker lock(&d->sync);
    
    // Ensure database schema is up to date
    createOrUpdateDatabaseSchema();
    
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    QString updateSql = "UPDATE person SET attendanceMode = ?, tenantId = ?, id = ?, status = ? WHERE uuid = ?";
    query.prepare(updateSql);
    query.bindValue(0, attendanceMode);
    query.bindValue(1, tenantId);
    query.bindValue(2, id);
    query.bindValue(3, status);
    query.bindValue(4, uuid);
    
    bool success = query.exec();
    
    if (success) {
        // Update in-memory list (no default values)
        for (int i = 0; i < d->mPersons.size(); i++) {
            if (d->mPersons[i].uuid == uuid) {
                d->mPersons[i].attendanceMode = attendanceMode;
                d->mPersons[i].tenantId = tenantId;
                d->mPersons[i].id = id;
                d->mPersons[i].status = status;
                break;
            }
        }
        
        LogD("%s %s[%d] Updated new columns for person with UUID: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, uuid.toStdString().c_str());
    } else {
        QSqlError error = query.lastError();
        LogE("%s %s[%d] Failed to update new columns: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, error.text().toStdString().c_str());
    }
    
    return success;
}

QList<PERSONS_t> RegisteredFacesDB::GetAllPersonFromRAM()
{
	QList<PERSONS_t> result;
	Q_D(RegisteredFacesDB);
	d->sync.lock();
	result = d->mPersons;

	d->sync.unlock();
	return result;
}

QList<PERSONS_t> RegisteredFacesDB::GetPersonDataByNameFromRAM(int nCurrPage,int nPerPage,QString name)
{
	QList<PERSONS_t> result;
	Q_D(RegisteredFacesDB);
	d->sync.lock();
	for (int i = nCurrPage; i < nCurrPage + nPerPage; i++)
	{
		if(i < 0 || d->mPersons.size() <= i)
		{
			break;
		}
		auto &t = d->mPersons.at(i);
		if (t.name == name)
		{
			result.append(t);
		}
	}
	d->sync.unlock();
	return result;
}

int RegisteredFacesDB::GetPersonTotalNumByNameFromRAM(QString name)
{
	Q_D(RegisteredFacesDB);
	int count = 0;
	d->sync.lock();
    for(int i = 0; i<d->mPersons.count(); i++)
    {
        auto &t = d->mPersons.at(i);
        if(t.name == name)
        {
        	count++;
        }
    }

	d->sync.unlock();
	return count;
}

QList<PERSONS_t> RegisteredFacesDB::GetPersonDataByPersonUUIDFromRAM(QString uuid)
{
	Q_D(RegisteredFacesDB);
	QList<PERSONS_t> result;
	d->sync.lock();
	for (int i = 0; i < d->mPersons.size(); i++)
    {
		if(i < 0 || d->mPersons.size() <= i)
		{
			break;
		}
        auto &t = d->mPersons.at(i);
        if(t.uuid == uuid)
        {
        	result.append(t);
        }
    }

	d->sync.unlock();
	return result;
}

int RegisteredFacesDB::GetPersonTotalNumByPersonUUIDFromRAM(QString uuid)
{
	Q_D(RegisteredFacesDB);
	int count = 0;
	d->sync.lock();
    for(int i = 0; i<d->mPersons.count(); i++)
    {
        auto &t = d->mPersons.at(i);
        if(t.uuid == uuid)
        {
        	count++;
        }
    }

	d->sync.unlock();
	return count;
}

QList<PERSONS_t> RegisteredFacesDB::GetPersonDataByTimeFromRAM(int nCurrPage,int nPerPage,QTime startTime,QTime endTime)
{
	Q_D(RegisteredFacesDB);
	QList<PERSONS_t> result;
	d->sync.lock();
	for (int i = nCurrPage; i < nCurrPage + nPerPage; i++)
    {
		if(i < 0 || d->mPersons.size() <= i)
		{
			break;
		}
        auto &t = d->mPersons.at(i);
        QTime tTime = QTime::fromString(t.createtime,"yyyy/MM/dd hh:mm:ss");
        if(tTime >= startTime && tTime <=  endTime)
        {
        	result.append(t);
        }
    }

	d->sync.unlock();
	return result;
}

int RegisteredFacesDB::GetPersonTotalNumByTimeFromRAM(QTime startTime,QTime endTime)
{
	Q_D(RegisteredFacesDB);
	int count = 0;
	d->sync.lock();
	for (int i = 0; i < d->mPersons.size(); i++)
    {
        auto &t = d->mPersons.at(i);
        QTime tTime = QTime::fromString(t.createtime,"yyyy/MM/dd hh:mm:ss");
        if(tTime >= startTime && tTime <=  endTime)
        {
        	count++;
        }
    }

	d->sync.unlock();
	return count;
}

bool RegisteredFacesDB::DelPersonByPersionUUIDFromDBAndRAM(QString uuid)
{/*删除数据库信息*/
    Q_D(RegisteredFacesDB);
    QMutexLocker lock(&d->sync);
    LogD("%s %s[%d] delete %s \n", __FILE__, __FUNCTION__, __LINE__, uuid.toStdString().c_str());
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    query.prepare(QString("DELETE FROM person WHERE uuid='%1'").arg(uuid));
    bool ret = query.exec();

    for(int i = 0; i<d->mPersons.count(); i++)
    {
        auto &t = d->mPersons.at(i);

        if(t.uuid == uuid)
        {
            d->mPersons.removeAt(i);
            qXLApp->GetFaceMainFrm()->updateHome_PersonNum();
            return ret;
        }
    }
    return ret;
}

bool RegisteredFacesDB::UpdatePersonFaceFeature(const QString &uuid, const QByteArray &faceFeature, 
                                               const QByteArray &faceImageData, const QString &imageFormat)
{
    Q_D(RegisteredFacesDB);
    QMutexLocker lock(&d->sync);

    LogD("%s %s[%d] === Starting UpdatePersonFaceFeature ===\n", __FILE__, __FUNCTION__, __LINE__);
    LogD("%s %s[%d] UUID: %s\n", __FILE__, __FUNCTION__, __LINE__, uuid.toStdString().c_str());
    LogD("%s %s[%d] Face feature size: %d bytes\n", __FILE__, __FUNCTION__, __LINE__, faceFeature.size());
    LogD("%s %s[%d] Face image data size: %d bytes\n", __FILE__, __FUNCTION__, __LINE__, faceImageData.size());
    LogD("%s %s[%d] Image format: %s\n", __FILE__, __FUNCTION__, __LINE__, imageFormat.toStdString().c_str());

    // Find the person to get their employee ID
    QString employeeId;
    QString personName;
    for (int i = 0; i < d->mPersons.size(); i++) {
        if (d->mPersons[i].uuid == uuid) {
            employeeId = d->mPersons[i].idcard;
            personName = d->mPersons[i].name;
            break;
        }
    }

    if (employeeId.isEmpty()) {
        LogE("%s %s[%d] Person with UUID %s not found in memory\n", 
             __FILE__, __FUNCTION__, __LINE__, uuid.toStdString().c_str());
        return false;
    }
    
    LogD("%s %s[%d] Found person - Name: %s, Employee ID: %s\n", 
         __FILE__, __FUNCTION__, __LINE__, personName.toStdString().c_str(), employeeId.toStdString().c_str());

    // Save face image if provided
    bool imageSaved = false;
    QString imagePath = QString("/mnt/user/");
    
    if (!faceImageData.isEmpty() && !imageFormat.isEmpty()) {
        LogD("%s %s[%d] Attempting to save face image...\n", __FILE__, __FUNCTION__, __LINE__);
        imageSaved = saveFaceImage(employeeId, faceImageData, imageFormat);
        
        if (imageSaved) {
            imagePath = getFaceImagePath(employeeId, imageFormat);
            LogD("%s %s[%d] Face image saved successfully, path: %s\n", 
                 __FILE__, __FUNCTION__, __LINE__, imagePath.toStdString().c_str());
        } else {
            LogE("%s %s[%d] Failed to save face image for employee: %s\n", 
                 __FILE__, __FUNCTION__, __LINE__, employeeId.toStdString().c_str());
        }
    } else {
        LogD("%s %s[%d] No image data provided (imageData: %d bytes, format: %s)\n", 
             __FILE__, __FUNCTION__, __LINE__, faceImageData.size(), imageFormat.toStdString().c_str());
    }

    // Update database
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    if (imageSaved) {
        LogD("%s %s[%d] Updating database with feature and image path\n", __FILE__, __FUNCTION__, __LINE__);
        query.prepare("UPDATE person SET feature = ?, featuresize = ?, image = ? WHERE uuid = ?");
        query.bindValue(0, faceFeature);
        query.bindValue(1, faceFeature.size());
        query.bindValue(2, imagePath);
        query.bindValue(3, uuid);
    } else {
        LogD("%s %s[%d] Updating database with feature only\n", __FILE__, __FUNCTION__, __LINE__);
        query.prepare("UPDATE person SET feature = ?, featuresize = ? WHERE uuid = ?");
        query.bindValue(0, faceFeature);
        query.bindValue(1, faceFeature.size());
        query.bindValue(2, uuid);
    }
    
    if (!query.exec()) {
        QSqlError error = query.lastError();
        LogE("%s %s[%d] Database update error: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, error.text().toStdString().c_str());
        return false;
    } else {
        LogD("%s %s[%d] Database updated successfully\n", __FILE__, __FUNCTION__, __LINE__);
    }

    // Update in-memory list
    for (int i = 0; i < d->mPersons.size(); i++) {
        if (d->mPersons[i].uuid == uuid) {
            d->mPersons[i].feature = faceFeature;
            LogD("%s %s[%d] Successfully updated face feature in memory for person: %s\n", 
                 __FILE__, __FUNCTION__, __LINE__, d->mPersons[i].name.toStdString().c_str());
            
            LogD("%s %s[%d] === UpdatePersonFaceFeature completed successfully ===\n", __FILE__, __FUNCTION__, __LINE__);
            return true;
        }
    }
    
    LogE("%s %s[%d] Failed to find person in memory for final update\n", __FILE__, __FUNCTION__, __LINE__);
    return false;
}

bool RegisteredFacesDB::ensureFingerprintColumn()
{
    LogD("%s %s[%d] === Checking fingerprint column ===\n", __FILE__, __FUNCTION__, __LINE__);
    
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    
    // Check if fingerprint column exists
    query.exec("PRAGMA table_info(person)");
    bool hasFingerprint = false;
    
    while (query.next()) {
        QString columnName = query.value("name").toString();
        if (columnName == "fingerprint") {
            hasFingerprint = true;
            break;
        }
    }
    
    // Add fingerprint column if it doesn't exist
    if (!hasFingerprint) {
        LogD("%s %s[%d] Adding fingerprint column to database\n", __FILE__, __FUNCTION__, __LINE__);
        
        if (!query.exec("ALTER TABLE person ADD COLUMN fingerprint BLOB")) {
            LogE("%s %s[%d] Failed to add fingerprint column: %s\n", 
                 __FILE__, __FUNCTION__, __LINE__, 
                 query.lastError().text().toStdString().c_str());
            return false;
        }
        
        LogD("%s %s[%d] Fingerprint column added successfully\n", __FILE__, __FUNCTION__, __LINE__);
    } else {
        LogD("%s %s[%d] Fingerprint column already exists\n", __FILE__, __FUNCTION__, __LINE__);
    }
    
    return true;
}

// Function to update person's fingerprint data
bool RegisteredFacesDB::UpdatePersonFingerprint(const QString &uuid, const QByteArray &fingerprintData)
{
    Q_D(RegisteredFacesDB);
    QMutexLocker lock(&d->sync);
    
    LogD("%s %s[%d] === Updating fingerprint for UUID: %s ===\n", 
         __FILE__, __FUNCTION__, __LINE__, uuid.toStdString().c_str());
    LogD("%s %s[%d] Fingerprint data size: %d bytes\n", 
         __FILE__, __FUNCTION__, __LINE__, fingerprintData.size());
    
    if (uuid.isEmpty()) {
        LogE("%s %s[%d] UUID is empty\n", __FILE__, __FUNCTION__, __LINE__);
        return false;
    }
    
    if (fingerprintData.isEmpty()) {
        LogE("%s %s[%d] Fingerprint data is empty\n", __FILE__, __FUNCTION__, __LINE__);
        return false;
    }
    
    // Ensure fingerprint column exists
    if (!ensureFingerprintColumn()) {
        LogE("%s %s[%d] Failed to ensure fingerprint column exists\n", __FILE__, __FUNCTION__, __LINE__);
        return false;
    }
    
    // Update database
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    query.prepare("UPDATE person SET fingerprint = ? WHERE uuid = ?");
    query.bindValue(0, fingerprintData);
    query.bindValue(1, uuid);
    
    if (!query.exec()) {
        QSqlError error = query.lastError();
        LogE("%s %s[%d] Database update failed: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, error.text().toStdString().c_str());
        return false;
    }
    
    if (query.numRowsAffected() == 0) {
        LogE("%s %s[%d] No rows affected - UUID not found: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, uuid.toStdString().c_str());
        return false;
    }
    
    // Update in-memory list
    for (int i = 0; i < d->mPersons.size(); i++) {
        if (d->mPersons[i].uuid == uuid) {
            d->mPersons[i].fingerprint = fingerprintData;
            LogD("%s %s[%d] Updated fingerprint in memory for person: %s\n", 
                 __FILE__, __FUNCTION__, __LINE__, d->mPersons[i].name.toStdString().c_str());
            break;
        }
    }
    
    LogD("%s %s[%d] === Fingerprint update successful ===\n", __FILE__, __FUNCTION__, __LINE__);
    
#ifdef Q_OS_LINUX
    system("sync");
#endif
    
    return true;
}

// Function to retrieve person's fingerprint data
bool RegisteredFacesDB::GetPersonFingerprint(const QString &uuid, QByteArray &fingerprintData)
{
    Q_D(RegisteredFacesDB);
    QMutexLocker lock(&d->sync);
    
    LogD("%s %s[%d] Getting fingerprint for UUID: %s\n", 
         __FILE__, __FUNCTION__, __LINE__, uuid.toStdString().c_str());
    
    if (uuid.isEmpty()) {
        return false;
    }
    
    // First check in-memory list (faster)
    for (int i = 0; i < d->mPersons.size(); i++) {
        if (d->mPersons[i].uuid == uuid) {
            fingerprintData = d->mPersons[i].fingerprint;
            LogD("%s %s[%d] Found fingerprint in memory: %d bytes\n", 
                 __FILE__, __FUNCTION__, __LINE__, fingerprintData.size());
            return !fingerprintData.isEmpty();
        }
    }
    
    // Fallback to database query
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    query.prepare("SELECT fingerprint FROM person WHERE uuid = ?");
    query.bindValue(0, uuid);
    
    if (!query.exec()) {
        LogE("%s %s[%d] Query failed: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, 
             query.lastError().text().toStdString().c_str());
        return false;
    }
    
    if (query.next()) {
        fingerprintData = query.value("fingerprint").toByteArray();
        LogD("%s %s[%d] Found fingerprint in database: %d bytes\n", 
             __FILE__, __FUNCTION__, __LINE__, fingerprintData.size());
        return !fingerprintData.isEmpty();
    }
    
    LogD("%s %s[%d] No fingerprint found for UUID: %s\n", 
         __FILE__, __FUNCTION__, __LINE__, uuid.toStdString().c_str());
    return false;
}

// Function to delete person's fingerprint data
bool RegisteredFacesDB::DeletePersonFingerprint(const QString &uuid)
{
    Q_D(RegisteredFacesDB);
    QMutexLocker lock(&d->sync);
    
    LogD("%s %s[%d] Deleting fingerprint for UUID: %s\n", 
         __FILE__, __FUNCTION__, __LINE__, uuid.toStdString().c_str());
    
    if (uuid.isEmpty()) {
        return false;
    }
    
    // Update database - set fingerprint to NULL
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    query.prepare("UPDATE person SET fingerprint = NULL WHERE uuid = ?");
    query.bindValue(0, uuid);
    
    if (!query.exec()) {
        LogE("%s %s[%d] Failed to delete fingerprint: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, 
             query.lastError().text().toStdString().c_str());
        return false;
    }
    
    // Update in-memory list
    for (int i = 0; i < d->mPersons.size(); i++) {
        if (d->mPersons[i].uuid == uuid) {
            d->mPersons[i].fingerprint.clear();
            LogD("%s %s[%d] Cleared fingerprint in memory\n", __FILE__, __FUNCTION__, __LINE__);
            break;
        }
    }
    
    LogD("%s %s[%d] Fingerprint deleted successfully\n", __FILE__, __FUNCTION__, __LINE__);
    return true;
}
bool RegisteredFacesDB::UpdatePersonFingerId(const QString &uuid, int fingerId)
{
    Q_D(RegisteredFacesDB);
    QMutexLocker lock(&d->sync);
    
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    query.prepare("UPDATE person SET finger_id = ? WHERE uuid = ?");
    query.bindValue(0, fingerId);
    query.bindValue(1, uuid);
    
    if (!query.exec()) {
        return false;
    }
    
    // Update memory
    for (int i = 0; i < d->mPersons.size(); i++) {
        if (d->mPersons[i].uuid == uuid) {
            d->mPersons[i].finger_id = fingerId;
            break;
        }
    }
    
    return true;
}

int RegisteredFacesDB::getNextAvailableFingerId()
{
    QMutexLocker lock(&g_personIdMutex);
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    query.exec("SELECT MAX(finger_id) FROM person WHERE finger_id > 0");
    
    if (query.next()) {
        int maxId = query.value(0).toInt();
        return maxId >= 1 ? maxId + 1 : 1;
    }
    return 1;
}

// 3. Find user by finger_id (for identification)
bool RegisteredFacesDB::GetPersonByFingerId(int fingerId, PERSONS_t &person)
{
    Q_D(RegisteredFacesDB);
    QMutexLocker lock(&d->sync);
    
    for (int i = 0; i < d->mPersons.size(); i++) {
        if (d->mPersons[i].finger_id == fingerId) {
            person = d->mPersons[i];
            return true;
        }
    }
    return false;
}

// Add to RegisteredFacesDB.cpp
bool RegisteredFacesDB::clearFingerprint(const QString& uuid)
{
    LogD("%s %s[%d] Clearing fingerprint for UUID: %s\n", 
         __FILE__, __FUNCTION__, __LINE__, uuid.toStdString().c_str());
    
    // Clear both fingerprint data and finger_id
    bool deleteResult = DeletePersonFingerprint(uuid);
    bool idResult = UpdatePersonFingerId(uuid, 0); // Set to 0 (no fingerprint)
    
    return deleteResult && idResult;
}

// Add to RegisteredFacesDB.cpp
uint16_t RegisteredFacesDB::getFingerId(const QString& uuid)
{
    Q_D(RegisteredFacesDB);
    QMutexLocker lock(&d->sync);
    
    // Check in memory first
    for (int i = 0; i < d->mPersons.size(); i++) {
        if (d->mPersons[i].uuid == uuid) {
            return d->mPersons[i].finger_id;
        }
    }
    
    // Fallback to database
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    query.prepare("SELECT finger_id FROM person WHERE uuid = ?");
    query.bindValue(0, uuid);
    
    if (query.exec() && query.next()) {
        return query.value(0).toUInt();
    }
    
    return 0; // No fingerprint enrolled
}

bool RegisteredFacesDB::clearAllFingerIds()
{
    Q_D(RegisteredFacesDB);
    QMutexLocker lock(&d->sync);
    
    LogD("%s %s[%d] === Clearing all finger IDs from database ===\n", 
         __FILE__, __FUNCTION__, __LINE__);
    
    // Clear all finger_id values in database
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    if (!query.exec("UPDATE person SET finger_id = 0")) {
        LogE("%s %s[%d] Failed to clear finger IDs: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, 
             query.lastError().text().toStdString().c_str());
        return false;
    }
    
    // Clear in-memory list
    for (int i = 0; i < d->mPersons.size(); i++) {
        d->mPersons[i].finger_id = 0;
    }
    
    LogD("%s %s[%d] All finger IDs cleared successfully\n", 
         __FILE__, __FUNCTION__, __LINE__);
    return true;
}
bool RegisteredFacesDB::ClearAllRfidCards()
{
    qDebug() << "RFID_SYNC: Clearing all RFID card numbers";
    
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    query.prepare("UPDATE person SET iccardnum = NULL");
    
    if (!query.exec()) {
        QSqlError error = query.lastError();
        qDebug() << "RFID_SYNC: Failed to clear RFID cards:" << error.text();
        return false;
    }
    
    // Also update in-memory data
    Q_D(RegisteredFacesDB);
    QMutexLocker lock(&d->sync);
    for (int i = 0; i < d->mPersons.size(); i++) {
        d->mPersons[i].iccard.clear();
    }
    
    qDebug() << "RFID_SYNC: Successfully cleared all RFID cards";
    return true;
}

bool RegisteredFacesDB::ClearAllDataForTenantChange()
{
    Q_D(RegisteredFacesDB);
    QMutexLocker lock(&d->sync);
    
    LogD("%s %s[%d] ==================================================\n", __FILE__, __FUNCTION__, __LINE__);
    LogD("%s %s[%d]   TENANT CHANGE: WIPING ALL LOCAL DATA\n", __FILE__, __FUNCTION__, __LINE__);
    LogD("%s %s[%d] ==================================================\n", __FILE__, __FUNCTION__, __LINE__);
    
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    bool success = true;

    // 1. Delete all persons
    if (!query.exec("DELETE FROM person")) {
        LogE("❌ Failed to delete from person table: %s\n", query.lastError().text().toStdString().c_str());
        success = false;
    } else {
        LogD("✅ Successfully wiped person table\n");
    }

    // 2. Delete all fingerprints from SQLite
    if (!query.exec("DELETE FROM fingerprints")) {
        LogE("❌ Failed to delete from fingerprints table: %s\n", query.lastError().text().toStdString().c_str());
        success = false;
    } else {
        LogD("✅ Successfully wiped fingerprints table\n");
    }

    // 3. Clear in-memory persons list
    d->mPersons.clear();
    LogD("✅ Successfully cleared in-memory persons list\n");

    // 4. Wipe physical fingerprint sensor
    FingerprintManager fpManager;
    if (fpManager.deleteAllFingerprints()) {
        LogD("✅ Successfully wiped physical fingerprint sensor\n");
    } else {
        LogE("❌ Failed to wipe physical fingerprint sensor\n");
        success = false;
    }

    return success;
}

bool RegisteredFacesDB::UpdateRfidJsonTimestamp(const QString& rfidJsonLastModified)
{
    LogD("%s %s[%d] Updating RFID JSON last modified timestamp\n", 
         __FILE__, __FUNCTION__, __LINE__);
    LogD("%s %s[%d]   RFID JSON: %s\n", 
         __FILE__, __FUNCTION__, __LINE__, 
         rfidJsonLastModified.toStdString().c_str());

    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    
    // First check if column exists, if not add it
    query.exec("PRAGMA table_info(person)");
    bool hasRfidColumn = false;
    while (query.next()) {
        QString colName = query.value(1).toString();
        if (colName == "rfid_json_last_modified") {
            hasRfidColumn = true;
            break;
        }
    }
    
    // Add column if it doesn't exist
    if (!hasRfidColumn) {
        qDebug() << "Adding rfid_json_last_modified column to person table";
        query.exec("ALTER TABLE person ADD COLUMN rfid_json_last_modified TEXT");
    }
    
    // Update the timestamp for all persons
    query.prepare("UPDATE person SET rfid_json_last_modified = ?");
    query.bindValue(0, rfidJsonLastModified);
    
    if (!query.exec()) {
        QSqlError error = query.lastError();
        LogE("%s %s[%d] Failed to update RFID JSON timestamp: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, error.text().toStdString().c_str());
        return false;
    }

    // Update in-memory list
    Q_D(RegisteredFacesDB);
    QMutexLocker lock(&d->sync);
    for (int i = 0; i < d->mPersons.size(); i++) {
        d->mPersons[i].rfid_json_last_modified = rfidJsonLastModified;
    }

    LogD("%s %s[%d] Successfully updated RFID JSON timestamp for all persons\n", 
         __FILE__, __FUNCTION__, __LINE__);
    return true;
}
bool RegisteredFacesDB::UpdateJsonLastModifiedTimestamps(
    const QString& employeeJsonLastModified,
    const QString& faceJsonLastModified)
{
    LogD("%s %s[%d] Updating JSON last modified timestamps\n", 
         __FILE__, __FUNCTION__, __LINE__);
    LogD("%s %s[%d]   Employees JSON: %s\n", 
         __FILE__, __FUNCTION__, __LINE__, 
         employeeJsonLastModified.toStdString().c_str());
    LogD("%s %s[%d]   Faces JSON: %s\n", 
         __FILE__, __FUNCTION__, __LINE__, 
         faceJsonLastModified.toStdString().c_str());

    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    query.prepare("UPDATE person SET "
                  "employee_json_last_modified = ?, "
                  "face_json_last_modified = ?");
    query.bindValue(0, employeeJsonLastModified);
    query.bindValue(1, faceJsonLastModified);
    
    if (!query.exec()) {
        QSqlError error = query.lastError();
        LogE("%s %s[%d] Failed to update JSON timestamps: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, error.text().toStdString().c_str());
        return false;
    }

    // Update in-memory list
    Q_D(RegisteredFacesDB);
    QMutexLocker lock(&d->sync);
    for (int i = 0; i < d->mPersons.size(); i++) {
        d->mPersons[i].employee_json_last_modified = employeeJsonLastModified;
        d->mPersons[i].face_json_last_modified = faceJsonLastModified;
    }

    LogD("%s %s[%d] Successfully updated JSON timestamps for all persons\n", 
         __FILE__, __FUNCTION__, __LINE__);
    return true;
}

bool RegisteredFacesDB::GetJsonLastModifiedTimestamps(
    QString& employeeJsonLastModified,
    QString& faceJsonLastModified)
{
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    query.exec("SELECT employee_json_last_modified, face_json_last_modified "
               "FROM person LIMIT 1");
    
    if (query.next()) {
        employeeJsonLastModified = query.value(0).toString();
        faceJsonLastModified = query.value(1).toString();
        
        LogD("%s %s[%d] Retrieved JSON timestamps - Employees: %s, Faces: %s\n",
             __FILE__, __FUNCTION__, __LINE__,
             employeeJsonLastModified.toStdString().c_str(),
             faceJsonLastModified.toStdString().c_str());
        
        return true;
    }
    
    LogD("%s %s[%d] No records found in person table\n", 
         __FILE__, __FUNCTION__, __LINE__);
    return false;
}

QString RegisteredFacesDB::GetJsonData(const QString& dataType)
{
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    QString jsonStr = "[";
    bool first = true;
    
    if (dataType == "employees") {
        // Adjust column names to match YOUR actual table structure
        query.prepare("SELECT emp_id, name, gender, last_modified FROM person");
        
        if (query.exec()) {
            while (query.next()) {
                if (!first) jsonStr += ",";
                
                QString empId = query.value(0).toString();
                QString name = query.value(1).toString();
                QString gender = query.value(2).toString();
                QString lastMod = query.value(3).toString();
                
                jsonStr += QString("{\"empId\":\"%1\",\"name\":\"%2\",\"gender\":\"%3\",\"lastModified\":\"%4\"}")
                          .arg(empId).arg(name).arg(gender).arg(lastMod);
                first = false;
            }
        }
    }
    else if (dataType == "faces") {
        // Adjust to match YOUR face data table structure
        query.prepare("SELECT emp_id, face_feature, last_modified FROM person WHERE face_feature IS NOT NULL");
        
        if (query.exec()) {
            while (query.next()) {
                if (!first) jsonStr += ",";
                
                QString empId = query.value(0).toString();
                QString faceData = query.value(1).toString();
                QString lastMod = query.value(2).toString();
                
                jsonStr += QString("{\"empId\":\"%1\",\"faceData\":\"%2\",\"lastModified\":\"%3\"}")
                          .arg(empId).arg(faceData).arg(lastMod);
                first = false;
            }
        }
    }
    
    jsonStr += "]";
    return jsonStr;
}

bool RegisteredFacesDB::UpsertEmployee(const QString& empId, const QString& name, 
                                       const QString& gender, const QString& lastModified)
{
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    
    // First check if employee exists
    query.prepare("SELECT COUNT(*) FROM person WHERE emp_id = ?");
    query.addBindValue(empId);
    
    bool exists = false;
    if (query.exec() && query.next()) {
        exists = (query.value(0).toInt() > 0);
    }
    
    if (exists) {
        // UPDATE existing employee
        query.prepare("UPDATE person SET name = ?, gender = ?, last_modified = ? WHERE emp_id = ?");
        query.addBindValue(name);
        query.addBindValue(gender);
        query.addBindValue(lastModified);
        query.addBindValue(empId);
    } else {
        // INSERT new employee - adjust columns to match YOUR table
        query.prepare("INSERT INTO person (emp_id, name, gender, last_modified) VALUES (?, ?, ?, ?)");
        query.addBindValue(empId);
        query.addBindValue(name);
        query.addBindValue(gender);
        query.addBindValue(lastModified);
    }
    
    bool success = query.exec();
    
    if (success) {
        qDebug() << "DB: Upserted employee" << empId << name;
    } else {
        qDebug() << "DB: Failed to upsert employee" << empId << query.lastError().text();
    }
    
    return success;
}

bool RegisteredFacesDB::UpsertFaceData(const QString& empId, const QString& faceData, 
                                       const QString& lastModified)
{
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    
    // Update face data for existing employee
    // Adjust column names to match YOUR actual table structure
    query.prepare("UPDATE person SET face_feature = ?, last_modified = ? WHERE emp_id = ?");
    query.addBindValue(faceData);
    query.addBindValue(lastModified);
    query.addBindValue(empId);
    
    bool success = query.exec();
    
    if (success) {
        qDebug() << "DB: Upserted face data for" << empId;
    } else {
        qDebug() << "DB: Failed to upsert face data" << empId << query.lastError().text();
    }
    
    return success;
}

bool RegisteredFacesDB::UpdateFingerprintJsonTimestamp(const QString& fingerprintJsonLastModified)
{
    LogD("%s %s[%d] Updating Fingerprint JSON last modified timestamp: %s\n", 
         __FILE__, __FUNCTION__, __LINE__, fingerprintJsonLastModified.toStdString().c_str());

    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    
    // Update the timestamp for all persons
    query.prepare("UPDATE person SET fingerprint_json_last_modified = ?");
    query.bindValue(0, fingerprintJsonLastModified);
    
    if (!query.exec()) {
        QSqlError error = query.lastError();
        LogE("%s %s[%d] Failed to update Fingerprint JSON timestamp: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, error.text().toStdString().c_str());
        return false;
    }

    // Update in-memory list
    Q_D(RegisteredFacesDB);
    QMutexLocker lock(&d->sync);
    for (int i = 0; i < d->mPersons.size(); i++) {
        d->mPersons[i].fingerprint_json_last_modified = fingerprintJsonLastModified;
    }

    LogD("%s %s[%d] Successfully updated Fingerprint JSON timestamp\n", 
         __FILE__, __FUNCTION__, __LINE__);
    return true;
}

bool RegisteredFacesDB::UpdateFourDoorJsonTimestamp(const QString& fourDoorJsonLastModified)
{
    LogD("%s %s[%d] Updating Four Door Controller JSON last modified timestamp: %s\n", 
         __FILE__, __FUNCTION__, __LINE__, fourDoorJsonLastModified.toStdString().c_str());

    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    
    // Update the timestamp for all persons
    query.prepare("UPDATE person SET four_door_controller_json_last_modified = ?");
    query.bindValue(0, fourDoorJsonLastModified);
    
    if (!query.exec()) {
        QSqlError error = query.lastError();
        LogE("%s %s[%d] Failed to update Four Door JSON timestamp: %s\n", 
             __FILE__, __FUNCTION__, __LINE__, error.text().toStdString().c_str());
        return false;
    }

    // Update in-memory list
    Q_D(RegisteredFacesDB);
    QMutexLocker lock(&d->sync);
    for (int i = 0; i < d->mPersons.size(); i++) {
        d->mPersons[i].four_door_controller_json_last_modified = fourDoorJsonLastModified;
    }

    LogD("%s %s[%d] Successfully updated Four Door JSON timestamp\n", 
         __FILE__, __FUNCTION__, __LINE__);
    return true;
}


// ============================================================================
// ALSO ADD GET FUNCTION (if needed)
// ============================================================================

bool RegisteredFacesDB::GetFingerprintJsonTimestamp(QString& fingerprintJsonLastModified)
{
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    query.exec("SELECT fingerprint_json_last_modified FROM person LIMIT 1");
    
    if (query.next()) {
        fingerprintJsonLastModified = query.value(0).toString();
        
        LogD("%s %s[%d] Retrieved Fingerprint JSON timestamp: %s\n",
             __FILE__, __FUNCTION__, __LINE__,
             fingerprintJsonLastModified.toStdString().c_str());
        return true;
    }
    
    return false;
}

bool RegisteredFacesDB::GetFourDoorJsonTimestamp(QString& fourDoorJsonLastModified)
{
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    query.exec("SELECT four_door_controller_json_last_modified FROM person LIMIT 1");
    
    if (query.next()) {
        fourDoorJsonLastModified = query.value(0).toString();
        
        LogD("%s %s[%d] Retrieved Four Door JSON timestamp: %s\n",
             __FILE__, __FUNCTION__, __LINE__,
             fourDoorJsonLastModified.toStdString().c_str());
        return true;
    }
    
    return false;
}

bool RegisteredFacesDB::GetAllJsonTimestamps(
    QString& employeeJsonLastModified,
    QString& faceJsonLastModified,
    QString& rfidJsonLastModified,
    QString& fingerprintJsonLastModified,
    QString& fourDoorJsonLastModified)
{
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    query.exec("SELECT employee_json_last_modified, face_json_last_modified, "
               "rfid_json_last_modified, fingerprint_json_last_modified, "
               "four_door_controller_json_last_modified FROM person LIMIT 1");
    
    if (query.next()) {
        employeeJsonLastModified = query.value(0).toString();
        faceJsonLastModified = query.value(1).toString();
        rfidJsonLastModified = query.value(2).toString();
        fingerprintJsonLastModified = query.value(3).toString();
        fourDoorJsonLastModified = query.value(4).toString();
        
        LogD("%s %s[%d] Retrieved all JSON timestamps\n", __FILE__, __FUNCTION__, __LINE__);
        LogD("%s %s[%d]   Employees: %s\n", __FILE__, __FUNCTION__, __LINE__, 
             employeeJsonLastModified.toStdString().c_str());
        LogD("%s %s[%d]   Faces: %s\n", __FILE__, __FUNCTION__, __LINE__, 
             faceJsonLastModified.toStdString().c_str());
        LogD("%s %s[%d]   RFID: %s\n", __FILE__, __FUNCTION__, __LINE__, 
             rfidJsonLastModified.toStdString().c_str());
        LogD("%s %s[%d]   Fingerprint: %s\n", __FILE__, __FUNCTION__, __LINE__, 
             fingerprintJsonLastModified.toStdString().c_str());
        LogD("%s %s[%d]   Four Door: %s\n", __FILE__, __FUNCTION__, __LINE__, 
             fourDoorJsonLastModified.toStdString().c_str());
        
        return true;
    }
    
    return false;
}

// ✅ GET RFID JSON TIMESTAMP
bool RegisteredFacesDB::GetRfidJsonTimestamp(QString& rfidJsonLastModified)
{
    QSqlQuery query(QSqlDatabase::database("isc_arcsoft_face"));
    query.exec("SELECT rfid_json_last_modified FROM person LIMIT 1");
    
    if (query.next()) {
        rfidJsonLastModified = query.value(0).toString();
        
        LogD("%s %s[%d] Retrieved RFID JSON timestamp: %s\n",
             __FILE__, __FUNCTION__, __LINE__,
             rfidJsonLastModified.toStdString().c_str());
        return true;
    }
    
    return false;
}
