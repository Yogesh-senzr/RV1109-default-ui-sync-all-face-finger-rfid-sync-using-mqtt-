#ifndef FINGERPRINTMANAGER_H
#define FINGERPRINTMANAGER_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QMutex>
#include <QScopedPointer>
#include <QWidget>

// Forward declarations
class FingerprintManagerPrivate;


class FingerprintManager : public QObject
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(FingerprintManager)
    
public:
    
    explicit FingerprintManager(QObject *parent = nullptr);
    
   
    ~FingerprintManager();
    
   
    static inline FingerprintManager *GetInstance() {
        static FingerprintManager instance;
        return &instance;
    }
    
   
    bool initFingerprintSensor();
    

    bool isSensorReady() const;

    bool startEnrollment(uint16_t fingerId);
    

    bool identifyFingerprint(uint16_t &matchedFingerId, float &confidence);
    

    bool identifyAndShowResult(QWidget* parent = nullptr);
    
   
    bool deleteFingerprintTemplate(uint16_t fingerId);
    

    bool deleteAllFingerprints();
    
 
    int getTemplateCount();
    
    bool downloadFingerprintTemplate(uint16_t fingerId, QByteArray &templateData);
    
    bool uploadTemplateToSensor(uint16_t fingerId, const QByteArray &templateData);
    
   
    bool captureFingerprint(QByteArray &fingerprintTemplate);
    
    
    bool storeFingerprintTemplate(uint16_t fingerId, const QByteArray &templateData);
    
    
    bool downloadTemplateFromSensor(uint16_t fingerId, QByteArray &templateData);
    

signals:
    
    void sigEnrollmentProgress(int stage, const QString &message);
    void sigEnrollmentComplete(uint16_t fingerId);
    void sigEnrollmentFailed(const QString &error);
    void sigFingerprintMatched(uint16_t fingerId, float confidence);
    void sigFingerprintNotMatched();
    void sigFingerDetected();
    void sigSensorError(const QString &error);
    
private:
    QScopedPointer<FingerprintManagerPrivate> d_ptr;
    
    // Prevent copying
    FingerprintManager(const FingerprintManager&) = delete;
    FingerprintManager& operator=(const FingerprintManager&) = delete;
};

#endif // FINGERPRINTMANAGER_H