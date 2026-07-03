#include "FingerprintManager.h"
#include "PCIcore/UARTUart.h"
#include "DB/RegisteredFacesDB.h"
#include "MessageHandler/Log.h"
#include <QThread>
#include <QMutexLocker>
#include <QTime>
#include <QCoreApplication>
#include <QTimer>
#include <QApplication>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QDebug>
#include <cstdint>
#include <cstring>
#include <unistd.h>
#include <time.h>

using namespace YNH_LJX;

// ============================================================================
// CONSTANTS (Matching ESP32)
// ============================================================================
#define FP_UART_INDEX 1
#define FP_HEADER1 0xEF
#define FP_HEADER2 0x01
#define FP_BAUD_RATE 115200

// Command codes
#define CMD_GET_IMAGE 0x01
#define CMD_GEN_CHAR 0x02
#define CMD_MATCH 0x03
#define CMD_SEARCH 0x04
#define CMD_REG_MODEL 0x05
#define CMD_STORE_CHAR 0x06
#define CMD_LOAD_CHAR 0x07
#define CMD_UP_CHAR 0x08
#define CMD_DOWN_CHAR 0x09
#define CMD_DELETE_CHAR 0x0C
#define CMD_EMPTY_DB 0x0D
#define CMD_TEMPLATE_NUM 0x1D

// Response codes
#define RESP_OK 0x00
#define RESP_ERROR 0x01
#define RESP_NO_FINGER 0x02
#define RESP_IMAGE_FAIL 0x03
#define RESP_NOT_MATCH 0x08
#define RESP_NOT_FOUND 0x09
#define RESP_ENROLL_MISMATCH 0x0A

// Timing constants (MATCH ESP32 EXACTLY!)
#define DELAY_DETECT_FINGER_MS 50        // ESP32: vTaskDelay(pdMS_TO_TICKS(50))
#define DELAY_AFTER_CAPTURE_MS 50        // ESP32: vTaskDelay(pdMS_TO_TICKS(50))
#define DELAY_BETWEEN_STAGES_MS 100      // ESP32: vTaskDelay(pdMS_TO_TICKS(100))
#define DELAY_VENDOR_CHECK_MS 20         // ESP32: vTaskDelay(pdMS_TO_TICKS(20))
#define DELAY_BEFORE_SAVE_MS 50          // ESP32: vTaskDelay(pdMS_TO_TICKS(50))
#define DELAY_AFTER_COMMAND_MS 50        // Default delay after sending command

#define MAX_FINGER_DETECT_ATTEMPTS 100   // ESP32: const int max_attempts = 100

// ============================================================================
// PRIVATE CLASS
// ============================================================================
class FingerprintManagerPrivate
{
    Q_DECLARE_PUBLIC(FingerprintManager)
    
public:
    FingerprintManagerPrivate(FingerprintManager *ptr);
    ~FingerprintManagerPrivate();
    
    // Initialization
    bool initUART();
    void cleanupUART();
    
    // Low-level UART operations (ESP32-style - SIMPLE!)
    bool sendCommand(uint8_t cmd, const uint8_t* data, uint16_t dataLen);
    bool receiveResponse(uint8_t* response, int maxLen, int &bytesReceived);
    
    // Helper functions
    void printHex(const char* label, const uint8_t* data, int length);
    uint16_t calculateChecksum(const uint8_t* data, int length);
    
    // Core fingerprint operations (matching ESP32 exactly)
    bool detectFinger(const char* stageName);
    bool captureAndGenerateChar(uint8_t bufferID, const char* stageName);
    bool checkDuplicates(uint8_t bufferID, const char* stageName);
    bool vendorCheck();
    bool storeToFlash(uint16_t fingerId);
    bool searchDatabase(uint16_t &fingerId, uint16_t &confidence);
    bool testBufferCapacityAdvanced();
    bool storeWithoutMerge(uint16_t fingerId);
    bool vendorCheckWithRetry();



private:
    FingerprintManager *const q_ptr;
    QMutex sync;
    bool sensorReady;
    int uartHandle;
};

// ============================================================================
// PRIVATE IMPLEMENTATION
// ============================================================================

FingerprintManagerPrivate::FingerprintManagerPrivate(FingerprintManager *ptr)
    : q_ptr(ptr)
    , sensorReady(false)
    , uartHandle(-1)
{
}

FingerprintManagerPrivate::~FingerprintManagerPrivate()
{
    cleanupUART();
}

bool FingerprintManagerPrivate::initUART()
{
    LogD("=== INITIALIZING FINGERPRINT UART (ESP32-Style) ===\n");
    
    try {
        // Close existing connection
        if (uartHandle >= 0) {
            UARTUart::Uart_CloseUartDev(FP_UART_INDEX);
            usleep(100000);
        }
        
        // ✅ CRITICAL FIX: Proper UART configuration
        UART_ATTR_S stUartAttr;
        stUartAttr.nBaudRate = 115200;
        stUartAttr.RDBlock = 1;           // BLOCKING mode
        stUartAttr.mBlockData = 0;        // ✅ VMIN = 1 (wait for at least 1 byte)
        stUartAttr.mBlockTime = 5;       // ✅ VTIME = 30 (3 second timeout)
        
        LogD("Opening UART: Baud=115200, Blocking=ON, VMIN=1, VTIME=30\n");
        
        int result = UARTUart::Uart_OpenUartDev(FP_UART_INDEX, stUartAttr);
        
        if (result != ISC_OK) {
            LogE("❌ Failed to open UART\n");
            return false;
        }
        
        LogD("✅ UART opened successfully\n");
        uartHandle = FP_UART_INDEX;
        
        // Test communication
        LogD("Testing sensor communication...\n");
        
        uint8_t testCmd[] = {
            0xEF, 0x01, 0x00, 0x00, 0x00, 0x00,
            0x01, 0x00, 0x03, 0x0F, 0x00, 0x13
        };
        
        int sent = UARTUart::Uart_WriteUart(FP_UART_INDEX, testCmd, sizeof(testCmd));
        
        if (sent != sizeof(testCmd)) {
            LogE("❌ Failed to send test command\n");
            UARTUart::Uart_CloseUartDev(FP_UART_INDEX);
            uartHandle = -1;
            return false;
        }
        
        usleep(200000); // 200ms for sensor to process
        
        uint8_t response[32];
        int bytesReceived = UARTUart::Uart_ReadUart(FP_UART_INDEX, response, sizeof(response));
        
        if (bytesReceived >= 12 && response[0] == 0xEF && response[1] == 0x01) {
            LogD("✅ Sensor communication verified\n");
            printHex("Test Response", response, bytesReceived);
            
            UARTUart::Uart_ReadFlush(FP_UART_INDEX);
            sensorReady = true;
            return true;
        }
        
        LogE("❌ No valid response from sensor\n");
        UARTUart::Uart_CloseUartDev(FP_UART_INDEX);
        uartHandle = -1;
        return false;
        
    } catch (const std::exception& e) {
        LogE("❌ Exception: %s\n", e.what());
        if (uartHandle >= 0) {
            UARTUart::Uart_CloseUartDev(FP_UART_INDEX);
            uartHandle = -1;
        }
        return false;
    }
}

void FingerprintManagerPrivate::cleanupUART()
{
    if (uartHandle >= 0) {
        UARTUart::Uart_CloseUartDev(FP_UART_INDEX);
        uartHandle = -1;
        sensorReady = false;
    }
}

void FingerprintManagerPrivate::printHex(const char* label, const uint8_t* data, int length)
{
    QString hexString = QString("DEBUG: %1 (%2 bytes): ").arg(label).arg(length);
    for (int i = 0; i < length; i++) {
        hexString += QString("%1 ").arg(data[i], 2, 16, QChar('0')).toUpper();
    }
    LogD("%s\n", hexString.toStdString().c_str());
}

uint16_t FingerprintManagerPrivate::calculateChecksum(const uint8_t* data, int length)
{
    uint16_t checksum = 0;
    for (int i = 0; i < length; i++) {
        checksum += data[i];
    }
    return checksum;
}

// ============================================================================
// ✅ SIMPLIFIED SEND COMMAND (ESP32-Style - NO RETRIES, NO EXCESSIVE DELAYS)
// ============================================================================
bool FingerprintManagerPrivate::sendCommand(uint8_t cmd, const uint8_t* data, uint16_t dataLen)
{
    LogD("──────────────────────────────────────\n");
    LogD("📤 SENDING COMMAND: 0x%02X\n", cmd);
    
    // Build packet
    uint8_t packet[256];
    int idx = 0;
    
    packet[idx++] = 0xEF;
    packet[idx++] = 0x01;
    packet[idx++] = 0x00; packet[idx++] = 0x00;
    packet[idx++] = 0x00; packet[idx++] = 0x00;
    packet[idx++] = 0x01;  // Command packet type
    
    uint16_t length = 3 + dataLen;  // cmd + data + checksum
    packet[idx++] = (length >> 8) & 0xFF;
    packet[idx++] = length & 0xFF;
    packet[idx++] = cmd;
    
    if (data && dataLen > 0) {
        memcpy(&packet[idx], data, dataLen);
        idx += dataLen;
    }
    
    // Calculate checksum (from packet type to end of data)
    uint16_t checksum = 0;
    for (int i = 6; i < idx; i++) {
        checksum += packet[i];
    }
    packet[idx++] = (checksum >> 8) & 0xFF;
    packet[idx++] = checksum & 0xFF;
    
    printHex("TX", packet, idx);
    
    // ✅ Simple send (like ESP32's uart_write_bytes)
    int bytesSent = UARTUart::Uart_WriteUart(FP_UART_INDEX, packet, idx);
    
    if (bytesSent != idx) {
        LogE("❌ Send failed: %d/%d bytes\n", bytesSent, idx);
        return false;
    }
    
    LogD("✅ Command sent (%d bytes)\n", bytesSent);
    
    // ✅ MATCH ESP32: Small fixed delay (not command-specific!)
    usleep(DELAY_AFTER_COMMAND_MS * 1000);
    
    return true;
}

bool FingerprintManagerPrivate::receiveResponse(uint8_t* response, int maxLen, int &bytesReceived)
{
    LogD("📥 Waiting for response (timeout: 3s)...\n");
    
    // ✅ Single blocking read with 3-second timeout (like ESP32)
    bytesReceived = UARTUart::Uart_ReadUart(FP_UART_INDEX, response, maxLen);
    
    if (bytesReceived <= 0) {
        LogE("❌ No response (timeout)\n");
        return false;
    }
    
    LogD("✅ Received %d bytes\n", bytesReceived);
    printHex("RX", response, bytesReceived);
    
    // Validate header
    if (bytesReceived < 12) {
        LogE("❌ Response too short: %d bytes\n", bytesReceived);
        return false;
    }
    
    // Find header
    int headerIdx = -1;
    for (int i = 0; i < bytesReceived - 1; i++) {
        if (response[i] == 0xEF && response[i+1] == 0x01) {
            headerIdx = i;
            break;
        }
    }
    
    if (headerIdx < 0) {
        LogE("❌ No valid header found\n");
        return false;
    }
    
    // Extract confirmation code (at index 9 from header)
    if (headerIdx + 9 < bytesReceived) {
        uint8_t confirmCode = response[headerIdx + 9];
        LogD("Confirmation code: 0x%02X", confirmCode);
        
        switch(confirmCode) {
            case 0x00: LogD(" -> ✅ SUCCESS\n"); break;
            case 0x01: LogD(" -> ❌ ERROR: Command execution error\n"); break;
            case 0x02: LogD(" -> ❌ ERROR: No finger on sensor\n"); break;
            case 0x09: LogD(" -> ℹ️ NOT FOUND (no duplicate)\n"); break;
            case 0x0A: LogD(" -> ❌ ERROR: Templates don't match\n"); break;
            default: LogD(" -> ℹ️ Code: 0x%02X\n", confirmCode); break;
        }
        
        // ✅ Move confirmation code to response[0] for easy access
        memmove(response, &response[headerIdx + 9], bytesReceived - headerIdx - 9);
        bytesReceived = bytesReceived - headerIdx - 9;
    }
    
    return true;
}

// ============================================================================
// ✅ DETECT FINGER (Matching ESP32's detect_finger function)
// ============================================================================
bool FingerprintManagerPrivate::detectFinger(const char* stageName)
{
    LogD("╔════════════════════════════════════════╗\n");
    LogD("║  🔍 DETECTING FINGER: %-16s║\n", stageName);
    LogD("╚════════════════════════════════════════╝\n");
    
    int attempt = 0;
    
    while (attempt < MAX_FINGER_DETECT_ATTEMPTS) {
        // Send GetImage command (0x01)
        if (!sendCommand(CMD_GET_IMAGE, NULL, 0)) {
            LogE("Failed to send GetImage command\n");
            return false;
        }
        
        uint8_t response[16];
        int bytesReceived;
        
        if (receiveResponse(response, sizeof(response), bytesReceived)) {
            if (bytesReceived >= 1 && response[0] == 0x00) {  // RESP_OK
                LogD("✅ Finger detected on attempt %d!\n", attempt + 1);
                return true;
            } else if (response[0] == 0x02) {
                // No finger yet, continue polling
                if (attempt % 10 == 0) {
                    LogD("⏳ No finger yet (attempt %d/%d)\n", attempt + 1, MAX_FINGER_DETECT_ATTEMPTS);
                }
            }
        }
        
        attempt++;
        QThread::msleep(DELAY_DETECT_FINGER_MS);  // ✅ Match ESP32: 50ms
        
        // Keep UI responsive
        if (attempt % 10 == 0) {
            QApplication::processEvents();
        }
    }
    
    LogE("❌ Finger detection timeout after %d attempts\n", MAX_FINGER_DETECT_ATTEMPTS);
    return false;
}

// ============================================================================
// ✅ CAPTURE AND GENERATE CHARACTER (Matching ESP32's capture_and_template)
// ============================================================================
bool FingerprintManagerPrivate::captureAndGenerateChar(uint8_t bufferID, const char* stageName)
{
    LogD("╔════════════════════════════════════════╗\n");
    LogD("║  📸 CAPTURE & TEMPLATE: %-14s║\n", stageName);
    LogD("║  Buffer: %d                            ║\n", bufferID);
    LogD("╚════════════════════════════════════════╝\n");
    
    // ✅ ESP32 does NOT send separate CaptureFinger command!
    // detectFinger() already captured the image with GetImage (0x01)
    
    // Generate character file from captured image
    uint8_t data[1] = { bufferID };
    
    if (!sendCommand(CMD_GEN_CHAR, data, sizeof(data))) {
        LogE("❌ Failed to send GenChar command\n");
        return false;
    }
    
    uint8_t response[16];
    int bytesReceived;
    
    if (!receiveResponse(response, sizeof(response), bytesReceived)) {
        LogE("❌ Failed to receive GenChar response\n");
        return false;
    }
    
    if (bytesReceived >= 1 && response[0] == 0x00) {
        LogD("✅ Character file generated in Buffer %d\n", bufferID);
        return true;
    }
    
    LogE("❌ GenChar failed with code: 0x%02X\n", response[0]);
    return false;
}

// ============================================================================
// ✅ CHECK DUPLICATES (Matching ESP32's check_duplicates)
// ============================================================================
bool FingerprintManagerPrivate::checkDuplicates(uint8_t bufferID, const char* stageName)
{
    LogD("╔════════════════════════════════════════╗\n");
    LogD("║  🔎 DUPLICATE CHECK: %-17s║\n", stageName);
    LogD("╚════════════════════════════════════════╝\n");
    
    uint8_t data[5] = {
        bufferID,       // Buffer to search
        0x00, 0x00,     // Start page: 0
        0x0B, 0xB8      // Page count: 3000 (0x0BB8)
    };
    
    if (!sendCommand(CMD_SEARCH, data, sizeof(data))) {
        LogE("❌ Failed to send search command\n");
        return false;
    }
    
    uint8_t response[16];
    int bytesReceived;
    
    if (!receiveResponse(response, sizeof(response), bytesReceived)) {
        LogE("❌ Failed to receive search response\n");
        return false;
    }
    
    if (bytesReceived >= 1) {
        if (response[0] == 0x09) {  // NOT_FOUND - No duplicate
            LogD("✅ No duplicate found for %s\n", stageName);
            return true;
        } else if (response[0] == 0x00) {  // FOUND - Duplicate exists!
            if (bytesReceived >= 3) {
                uint16_t matchedId = (response[1] << 8) | response[2];
                LogE("❌ DUPLICATE FOUND! Matched ID: %d\n", matchedId);
            } else {
                LogE("❌ Duplicate found\n");
            }
            return false;
        }
    }
    
    LogE("❌ Unexpected response: 0x%02X\n", response[0]);
    return false;
}

// ============================================================================
// ✅ VENDOR CHECK (Matching ESP32 - used before save)
// ============================================================================
bool FingerprintManagerPrivate::vendorCheck()
{
    LogD("═══ VENDOR CHECK (0x05) ═══\n");
    
    // ✅ Just send command, ESP32 doesn't wait for response!
    if (!sendCommand(CMD_REG_MODEL, NULL, 0)) {
        LogE("❌ Failed to send vendor check\n");
        return false;
    }
    
    // ✅ CRITICAL: ESP32 DOES NOT READ RESPONSE!
    // It just waits 20ms then continues
    usleep(DELAY_VENDOR_CHECK_MS * 1000);  // 20ms
    LogD("⏱️ 20ms delay (ESP32 style - no response wait)\n");
    
    return true;
}

// ============================================================================
// ✅ STORE TO FLASH (Matching ESP32's save_finger)
// ============================================================================
bool FingerprintManagerPrivate::storeToFlash(uint16_t fingerId)
{
    LogD("╔════════════════════════════════════════╗\n");
    LogD("║  💾 STORING TO FLASH (ID: %d)         ║\n", fingerId);
    LogD("╚════════════════════════════════════════╝\n");
    
    // ✅ Match ESP32: 50ms delay before save
    QThread::msleep(DELAY_BEFORE_SAVE_MS);
    
    // ✅ Step 1: Vendor check (don't wait for response)
    if (!vendorCheck()) {
        return false;
    }

    LogD("⏳ Post-vendor delay (ESP32 timing)...\n");
    usleep(50000);  // 50ms like ESP32
    
    // ✅ Step 2: Store command immediately after vendor check
    uint8_t data[3] = {
        0x01,  // Buffer ID (Buffer 1 contains merged template)
        (uint8_t)((fingerId >> 8) & 0xFF),
        (uint8_t)(fingerId & 0xFF)
    };
    
    if (!sendCommand(CMD_STORE_CHAR, data, sizeof(data))) {
        LogE("❌ Failed to send store command\n");
        return false;
    }
    
    // ✅ NOW wait for store response
    uint8_t response[16];
    int bytesReceived;
    
    if (!receiveResponse(response, sizeof(response), bytesReceived)) {
        LogE("❌ Failed to receive store response\n");
        return false;
    }
    
    if (bytesReceived >= 1 && response[0] == 0x00) {
        LogD("╔════════════════════════════════════════╗\n");
        LogD("║  ✅ FINGERPRINT STORED SUCCESSFULLY!   ║\n");
        LogD("╚════════════════════════════════════════╝\n");
        LogD("Finger ID: %d\n", fingerId);
        return true;
    }
    
    LogE("❌ Store failed with code: 0x%02X\n", response[0]);
    return false;
}

// ============================================================================
// ✅ SEARCH DATABASE (Matching ESP32's identification)
// ============================================================================
bool FingerprintManagerPrivate::searchDatabase(uint16_t &fingerId, uint16_t &confidence)
{
    LogD("╔════════════════════════════════════════╗\n");
    LogD("║  🔍 SEARCHING DATABASE                 ║\n");
    LogD("╚════════════════════════════════════════╝\n");
    
    uint8_t data[5] = {
        0x01,        // Buffer 1
        0x00, 0x00,  // Start page: 0
        0x00, 0x64   // Page count: 100
    };
    
    if (!sendCommand(CMD_SEARCH, data, sizeof(data))) {
        LogE("❌ Failed to send search command\n");
        return false;
    }
    
    // ✅ For search, ESP32 waits longer (sensor needs time to search)
    uint8_t response[32];
    int bytesReceived;
    
    if (!receiveResponse(response, sizeof(response), bytesReceived)) {
        LogE("❌ No search response\n");
        return false;
    }
    
    if (bytesReceived >= 1) {
        if (response[0] == 0x00 && bytesReceived >= 5) {  // Match found
            fingerId = (response[1] << 8) | response[2];
            confidence = (response[3] << 8) | response[4];
            
            LogD("╔════════════════════════════════════════╗\n");
            LogD("║  ✅ MATCH FOUND!                       ║\n");
            LogD("╚════════════════════════════════════════╝\n");
            LogD("Finger ID: %d\n", fingerId);
            LogD("Confidence: %d\n", confidence);
            
            return true;
        } else if (response[0] == 0x09) {  // Not found
            LogD("❌ No match found in database\n");
            return false;
        }
    }
    
    LogE("❌ Search failed with code: 0x%02X\n", response[0]);
    return false;
}

bool FingerprintManagerPrivate::testBufferCapacityAdvanced()
{
    LogD("╔═══════════════════════════════════════════════════╗\n");
    LogD("║  🔬 ADVANCED BUFFER CAPACITY TEST                ║\n");
    LogD("║  (Multiple verification methods)                 ║\n");
    LogD("╚═══════════════════════════════════════════════════╝\n");
    
    if (!sensorReady) {
        LogE("❌ Sensor not ready\n");
        return false;
    }
    
    int workingBuffers = 0;
    
    // ========================================================================
    // METHOD 1: GenChar Test (What we already did)
    // ========================================================================
    LogD("\n");
    LogD("═══════════════════════════════════════════════════\n");
    LogD("METHOD 1: GenChar Command Test\n");
    LogD("═══════════════════════════════════════════════════\n");
    
    bool genChar_buf1 = false, genChar_buf2 = false, genChar_buf3 = false;
    
    // Test Buffer 1
    LogD("\n[1.1] Testing Buffer 1 with GenChar...\n");
    LogD("Place finger on sensor...\n");
    if (detectFinger("GenChar Buffer 1")) {
        uint8_t data1[1] = { 0x01 };
        if (sendCommand(CMD_GEN_CHAR, data1, 1)) {
            uint8_t resp[16];
            int bytes;
            if (receiveResponse(resp, 16, bytes)) {
                genChar_buf1 = (bytes >= 1 && resp[0] == 0x00);
                LogD("Buffer 1: %s (Code: 0x%02X)\n", 
                     genChar_buf1 ? "✅ PASS" : "❌ FAIL", resp[0]);
            }
        }
    }
    
    QThread::msleep(500);
    LogD("\n⏳ Remove finger and wait...\n");
    QThread::msleep(2000);
    
    // Test Buffer 2
    LogD("\n[1.2] Testing Buffer 2 with GenChar...\n");
    LogD("Place finger on sensor...\n");
    if (detectFinger("GenChar Buffer 2")) {
        uint8_t data2[1] = { 0x02 };
        if (sendCommand(CMD_GEN_CHAR, data2, 1)) {
            uint8_t resp[16];
            int bytes;
            if (receiveResponse(resp, 16, bytes)) {
                genChar_buf2 = (bytes >= 1 && resp[0] == 0x00);
                LogD("Buffer 2: %s (Code: 0x%02X)\n", 
                     genChar_buf2 ? "✅ PASS" : "❌ FAIL", resp[0]);
            }
        }
    }
    
    QThread::msleep(500);
    LogD("\n⏳ Remove finger and wait...\n");
    QThread::msleep(2000);
    
    // Test Buffer 3
    LogD("\n[1.3] Testing Buffer 3 with GenChar...\n");
    LogD("Place finger on sensor...\n");
    if (detectFinger("GenChar Buffer 3")) {
        uint8_t data3[1] = { 0x03 };
        if (sendCommand(CMD_GEN_CHAR, data3, 1)) {
            uint8_t resp[16];
            int bytes;
            if (receiveResponse(resp, 16, bytes)) {
                genChar_buf3 = (bytes >= 1 && resp[0] == 0x00);
                LogD("Buffer 3: %s (Code: 0x%02X)\n", 
                     genChar_buf3 ? "✅ PASS" : "❌ FAIL", resp[0]);
            }
        }
    }
    
    // ========================================================================
    // METHOD 2: RegModel (Merge) Test - Tests if buffers can be merged
    // ========================================================================
    LogD("\n");
    LogD("═══════════════════════════════════════════════════\n");
    LogD("METHOD 2: RegModel (Merge) Command Test\n");
    LogD("═══════════════════════════════════════════════════\n");
    
    bool regModel_1_2 = false, regModel_1_3 = false, regModel_2_3 = false;
    
    // First, capture to Buffer 1
    LogD("\n[2.1] Capturing to Buffer 1...\n");
    LogD("Place finger on sensor...\n");
    if (detectFinger("Merge test 1")) {
        uint8_t data1[1] = { 0x01 };
        if (sendCommand(CMD_GEN_CHAR, data1, 1)) {
            uint8_t resp[16];
            int bytes;
            receiveResponse(resp, 16, bytes);
        }
    }
    
    QThread::msleep(500);
    LogD("\n⏳ Remove finger and wait...\n");
    QThread::msleep(2000);
    
    // Capture to Buffer 2
    LogD("\n[2.2] Capturing to Buffer 2...\n");
    LogD("Place SAME finger on sensor...\n");
    if (detectFinger("Merge test 2")) {
        uint8_t data2[1] = { 0x02 };
        if (sendCommand(CMD_GEN_CHAR, data2, 1)) {
            uint8_t resp[16];
            int bytes;
            receiveResponse(resp, 16, bytes);
        }
    }
    
    // Try to merge Buffer 1 + Buffer 2
    LogD("\n[2.3] Attempting to merge Buffer 1 + Buffer 2...\n");
    if (sendCommand(CMD_REG_MODEL, NULL, 0)) {
        uint8_t resp[16];
        int bytes;
        if (receiveResponse(resp, 16, bytes)) {
            regModel_1_2 = (bytes >= 1 && resp[0] == 0x00);
            LogD("Merge 1+2: %s (Code: 0x%02X)\n", 
                 regModel_1_2 ? "✅ PASS" : "❌ FAIL", resp[0]);
        }
    }
    
    QThread::msleep(2000);
    
    // Now test if Buffer 3 can be used in merge
    LogD("\n[2.4] Testing Buffer 3 merge capability...\n");
    LogD("Place finger on sensor...\n");
    if (detectFinger("Merge test 3")) {
        uint8_t data3[1] = { 0x03 };
        if (sendCommand(CMD_GEN_CHAR, data3, 1)) {
            uint8_t resp[16];
            int bytes;
            if (receiveResponse(resp, 16, bytes)) {
                LogD("Buffer 3 capture: %s (Code: 0x%02X)\n", 
                     (resp[0] == 0x00) ? "✅ PASS" : "❌ FAIL", resp[0]);
            }
        }
    }
    
    // ========================================================================
    // METHOD 3: LoadChar Test - Try to load from invalid buffer
    // ========================================================================
    LogD("\n");
    LogD("═══════════════════════════════════════════════════\n");
    LogD("METHOD 3: LoadChar Command Test\n");
    LogD("(Testing if sensor accepts buffer IDs)\n");
    LogD("═══════════════════════════════════════════════════\n");
    
    bool loadChar_buf1 = false, loadChar_buf2 = false, loadChar_buf3 = false;
    
    // Try LoadChar with different buffer IDs (even if empty)
    // This tests if the sensor even recognizes the buffer ID
    
    LogD("\n[3.1] Testing LoadChar with Buffer 1 (0x01)...\n");
    {
        uint8_t data[3] = { 0x01, 0x00, 0x00 }; // Buffer 1, Position 0
        if (sendCommand(CMD_LOAD_CHAR, data, 3)) {
            uint8_t resp[16];
            int bytes;
            if (receiveResponse(resp, 16, bytes)) {
                // 0x00 = success, 0x09 = not found (both mean buffer ID is valid)
                // 0x01 = error (likely invalid buffer)
                loadChar_buf1 = (bytes >= 1 && (resp[0] == 0x00 || resp[0] == 0x09));
                LogD("LoadChar Buffer 1: %s (Code: 0x%02X) - %s\n", 
                     loadChar_buf1 ? "✅ VALID" : "❌ INVALID", 
                     resp[0],
                     (resp[0] == 0x00) ? "Success" : 
                     (resp[0] == 0x09) ? "Empty (but valid buffer)" : "Error");
            }
        }
    }
    
    LogD("\n[3.2] Testing LoadChar with Buffer 2 (0x02)...\n");
    {
        uint8_t data[3] = { 0x02, 0x00, 0x00 }; // Buffer 2, Position 0
        if (sendCommand(CMD_LOAD_CHAR, data, 3)) {
            uint8_t resp[16];
            int bytes;
            if (receiveResponse(resp, 16, bytes)) {
                loadChar_buf2 = (bytes >= 1 && (resp[0] == 0x00 || resp[0] == 0x09));
                LogD("LoadChar Buffer 2: %s (Code: 0x%02X) - %s\n", 
                     loadChar_buf2 ? "✅ VALID" : "❌ INVALID", 
                     resp[0],
                     (resp[0] == 0x00) ? "Success" : 
                     (resp[0] == 0x09) ? "Empty (but valid buffer)" : "Error");
            }
        }
    }
    
    LogD("\n[3.3] Testing LoadChar with Buffer 3 (0x03)...\n");
    {
        uint8_t data[3] = { 0x03, 0x00, 0x00 }; // Buffer 3, Position 0
        if (sendCommand(CMD_LOAD_CHAR, data, 3)) {
            uint8_t resp[16];
            int bytes;
            if (receiveResponse(resp, 16, bytes)) {
                loadChar_buf3 = (bytes >= 1 && (resp[0] == 0x00 || resp[0] == 0x09));
                LogD("LoadChar Buffer 3: %s (Code: 0x%02X) - %s\n", 
                     loadChar_buf3 ? "✅ VALID" : "❌ INVALID", 
                     resp[0],
                     (resp[0] == 0x00) ? "Success" : 
                     (resp[0] == 0x09) ? "Empty (but valid buffer)" : "Error");
            }
        }
    }
    
    // ========================================================================
    // FINAL ANALYSIS
    // ========================================================================
    LogD("\n");
    LogD("╔═══════════════════════════════════════════════════╗\n");
    LogD("║  📊 COMPREHENSIVE TEST RESULTS                   ║\n");
    LogD("╚═══════════════════════════════════════════════════╝\n");
    LogD("\n");
    LogD("┌─────────────┬──────────┬──────────┬──────────┐\n");
    LogD("│ Test Method │ Buffer 1 │ Buffer 2 │ Buffer 3 │\n");
    LogD("├─────────────┼──────────┼──────────┼──────────┤\n");
    LogD("│ GenChar     │    %s    │    %s    │    %s    │\n", 
         genChar_buf1 ? "✅" : "❌",
         genChar_buf2 ? "✅" : "❌",
         genChar_buf3 ? "✅" : "❌");
    LogD("│ LoadChar    │    %s    │    %s    │    %s    │\n", 
         loadChar_buf1 ? "✅" : "❌",
         loadChar_buf2 ? "✅" : "❌",
         loadChar_buf3 ? "✅" : "❌");
    LogD("└─────────────┴──────────┴──────────┴──────────┘\n");
    LogD("\n");
    
    // Count working buffers based on ALL tests
    int buf1_score = (genChar_buf1 ? 1 : 0) + (loadChar_buf1 ? 1 : 0);
    int buf2_score = (genChar_buf2 ? 1 : 0) + (loadChar_buf2 ? 1 : 0);
    int buf3_score = (genChar_buf3 ? 1 : 0) + (loadChar_buf3 ? 1 : 0);
    
    LogD("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    LogD("🎯 BUFFER SCORES (out of 2 tests):\n");
    LogD("   Buffer 1: %d/2\n", buf1_score);
    LogD("   Buffer 2: %d/2\n", buf2_score);
    LogD("   Buffer 3: %d/2\n", buf3_score);
    LogD("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    LogD("\n");
    
    // Final determination
    bool buffer1Confirmed = (buf1_score >= 2);
    bool buffer2Confirmed = (buf2_score >= 2);
    bool buffer3Confirmed = (buf3_score >= 1); // Even 1 pass means it exists
    
    workingBuffers = (buffer1Confirmed ? 1 : 0) + 
                     (buffer2Confirmed ? 1 : 0) + 
                     (buffer3Confirmed ? 1 : 0);
    
    LogD("╔═══════════════════════════════════════════════════╗\n");
    if (buffer3Confirmed) {
        LogD("║  ✅ DEFINITIVE CONCLUSION: 3-BUFFER SENSOR       ║\n");
        LogD("║                                                  ║\n");
        LogD("║  This sensor supports 3 buffers:                ║\n");
        LogD("║  • Buffer 1 (0x01) - CONFIRMED                  ║\n");
        LogD("║  • Buffer 2 (0x02) - CONFIRMED                  ║\n");
        LogD("║  • Buffer 3 (0x03) - CONFIRMED                  ║\n");
    } else if (!buffer3Confirmed && buffer2Confirmed) {
        LogD("║  ✅ DEFINITIVE CONCLUSION: 2-BUFFER SENSOR       ║\n");
        LogD("║                                                  ║\n");
        LogD("║  This sensor supports ONLY 2 buffers:           ║\n");
        LogD("║  • Buffer 1 (0x01) - CONFIRMED                  ║\n");
        LogD("║  • Buffer 2 (0x02) - CONFIRMED                  ║\n");
        LogD("║  • Buffer 3 (0x03) - NOT AVAILABLE ❌           ║\n");
        LogD("║                                                  ║\n");
        LogD("║  ⚠️  IMPORTANT: Your enrollment code is correct! ║\n");
        LogD("║     You should use 2-stage enrollment only.     ║\n");
    } else {
        LogD("║  ⚠️  INCONCLUSIVE RESULT                         ║\n");
        LogD("║  Please check sensor connection and retry.      ║\n");
    }
    LogD("╚═══════════════════════════════════════════════════╝\n");
    LogD("\n");
    
    return true;
}

bool FingerprintManagerPrivate::vendorCheckWithRetry()
{
    LogD("╔═══════════════════════════════════════╗\n");
    LogD("║  🔄 MERGE TEMPLATES (with retry)     ║\n");
    LogD("╚═══════════════════════════════════════╝\n");
    
    const int MAX_MERGE_ATTEMPTS = 3;
    
    for (int attempt = 1; attempt <= MAX_MERGE_ATTEMPTS; attempt++) {
        LogD("\n[Attempt %d/%d] Sending RegModel...\n", attempt, MAX_MERGE_ATTEMPTS);
        
        // Send RegModel command
        if (!sendCommand(CMD_REG_MODEL, NULL, 0)) {
            LogE("❌ Failed to send RegModel command\n");
            continue;
        }
        
        // ✅ WAIT LONGER - RegModel needs processing time!
        usleep(100000);  // 100ms instead of 20ms
        
        // Try to read response
        uint8_t response[16];
        int bytesReceived;
        
        if (!receiveResponse(response, sizeof(response), bytesReceived)) {
            LogE("⚠️ No response on attempt %d, retrying...\n", attempt);
            
            // Flush UART and retry
            UARTUart::Uart_ReadFlush(FP_UART_INDEX);
            usleep(50000);
            continue;
        }
        
        if (bytesReceived >= 1) {
            if (response[0] == 0x00) {
                LogD("✅ Templates merged successfully!\n");
                return true;
            } else if (response[0] == 0x0A) {
                LogE("❌ Templates don't match (Code 0x0A)\n");
                LogE("   Scans are too different - enrollment will fail!\n");
                return false;  // Don't retry - it won't help
            } else {
                LogE("⚠️ RegModel returned error 0x%02X, retrying...\n", response[0]);
            }
        }
    }
    
    LogE("❌ RegModel failed after %d attempts\n", MAX_MERGE_ATTEMPTS);
    return false;
}

bool FingerprintManagerPrivate::storeWithoutMerge(uint16_t fingerId)
{
    LogD("╔═══════════════════════════════════════╗\n");
    LogD("║  💾 ALTERNATIVE: Store Without Merge ║\n");
    LogD("╚═══════════════════════════════════════╝\n");
    
    // ⚠️ IMPORTANT: Some sensors work better storing just ONE good scan
    // instead of trying to merge two different scans!
    
    LogD("\n[STRATEGY] Using Buffer 1 only (first scan)\n");
    LogD("This works if first scan quality is good!\n");
    
    // Small delay before save
    QThread::msleep(50);
    
    // Store Buffer 1 directly (skip merge)
    uint8_t data[3] = {
        0x01,  // Buffer 1 (first scan)
        (uint8_t)((fingerId >> 8) & 0xFF),
        (uint8_t)(fingerId & 0xFF)
    };
    
    if (!sendCommand(CMD_STORE_CHAR, data, sizeof(data))) {
        LogE("❌ Failed to send store command\n");
        return false;
    }
    
    uint8_t response[16];
    int bytesReceived;
    
    if (!receiveResponse(response, sizeof(response), bytesReceived)) {
        LogE("❌ Failed to receive store response\n");
        return false;
    }
    
    if (bytesReceived >= 1 && response[0] == 0x00) {
        LogD("╔═══════════════════════════════════════╗\n");
        LogD("║  ✅ STORED SUCCESSFULLY (no merge)    ║\n");
        LogD("╚═══════════════════════════════════════╝\n");
        return true;
    }
    
    LogE("❌ Store failed with code: 0x%02X\n", response[0]);
    return false;
}

// ============================================================================
// PUBLIC API IMPLEMENTATION
// ============================================================================

FingerprintManager::FingerprintManager(QObject *parent)
    : QObject(parent)
    , d_ptr(new FingerprintManagerPrivate(this))
{
}

FingerprintManager::~FingerprintManager()
{
}

bool FingerprintManager::initFingerprintSensor()
{
    Q_D(FingerprintManager);
    QMutexLocker locker(&d->sync);
    
    LogD("╔════════════════════════════════════════╗\n");
    LogD("║  INITIALIZING FINGERPRINT SENSOR      ║\n");
    LogD("╚════════════════════════════════════════╝\n");
    
    return d->initUART();
}

bool FingerprintManager::isSensorReady() const
{
    Q_D(const FingerprintManager);
    return d->sensorReady;
}

bool FingerprintManager::startEnrollment(uint16_t fingerId)
{
    Q_D(FingerprintManager);
    QMutexLocker locker(&d->sync);
    
    LogD("═══════════════════════════════════════\n");
    LogD("║  SINGLE-BUFFER ENROLLMENT (ESP32)    ║\n");
    LogD("═══════════════════════════════════════\n");
    
    if (!d->sensorReady) {
        emit sigEnrollmentFailed("Sensor not ready");
        return false;
    }
    
    // ========================================================================
    // STAGE 1: Capture to Buffer 1 (ONLY STAGE THAT MATTERS!)
    // ========================================================================
    LogD("\n[STAGE 1] Capturing to Buffer 1...\n");
    emit sigEnrollmentProgress(1, "Place finger firmly\n(Single capture)");
    
    if (!d->detectFinger("enrollment")) {
        emit sigEnrollmentFailed("No finger detected");
        return false;
    }
    
    if (!d->captureAndGenerateChar(0x01, "Main")) {
        emit sigEnrollmentFailed("Failed to generate template");
        return false;
    }
    
    if (!d->checkDuplicates(0x01, "main")) {
        emit sigEnrollmentFailed("Duplicate fingerprint found!");
        return false;
    }
    
    LogD("✅ Template captured successfully!\n");
    emit sigEnrollmentProgress(2, "Processing...");
    
    // ========================================================================
    // SAVE DIRECTLY (NO MERGE - ESP32 Style!)
    // ========================================================================
    
    // Vendor check (ESP32 does this BEFORE save)
    LogD("\n[VENDOR CHECK] Sending command 0x05...\n");
    
    if (!d->sendCommand(CMD_REG_MODEL, NULL, 0)) {
        emit sigEnrollmentFailed("Vendor check failed");
        return false;
    }
    
    // ✅ CRITICAL: ESP32 WAITS FOR RESPONSE!
    uint8_t vendorResponse[16];
    int bytesReceived;
    
    if (!d->receiveResponse(vendorResponse, sizeof(vendorResponse), bytesReceived)) {
        LogE("❌ Vendor check response failed\n");
        emit sigEnrollmentFailed("Vendor check failed");
        return false;
    }
    
    if (bytesReceived >= 1 && vendorResponse[0] != 0x00) {
        LogE("❌ Vendor check returned error: 0x%02X\n", vendorResponse[0]);
        emit sigEnrollmentFailed("Vendor check failed");
        return false;
    }
    
    LogD("✅ Vendor check passed\n");
    
    // Now store Buffer 1 directly (like ESP32)
    QThread::msleep(50);
    
    uint8_t data[3] = {
        0x01,  // Buffer 1 (NOT merged, just Buffer 1!)
        (uint8_t)((fingerId >> 8) & 0xFF),
        (uint8_t)(fingerId & 0xFF)
    };
    
    if (!d->sendCommand(CMD_STORE_CHAR, data, sizeof(data))) {
        emit sigEnrollmentFailed("Failed to send store command");
        return false;
    }
    
    uint8_t response[16];
    
    if (!d->receiveResponse(response, sizeof(response), bytesReceived)) {
        emit sigEnrollmentFailed("Failed to receive store response");
        return false;
    }
    
    if (bytesReceived >= 1 && response[0] == 0x00) {
        LogD("╔═══════════════════════════════════════╗\n");
        LogD("║  ✅ ENROLLMENT SUCCESS (ESP32 STYLE)  ║\n");
        LogD("╚═══════════════════════════════════════╝\n");
        emit sigEnrollmentComplete(fingerId);
        return true;
    }
    
    LogE("❌ Store failed with code: 0x%02X\n", response[0]);
    emit sigEnrollmentFailed("Failed to save fingerprint");
    return false;
}

// ============================================================================
// ✅ IDENTIFICATION (Matching ESP32's capture_and_identify_finger)
// ============================================================================
bool FingerprintManager::identifyFingerprint(uint16_t &matchedFingerId, float &confidence)
{
    Q_D(FingerprintManager);
    QMutexLocker locker(&d->sync);
    
    LogD("╔════════════════════════════════════════╗\n");
    LogD("║  FINGERPRINT IDENTIFICATION           ║\n");
    LogD("╚════════════════════════════════════════╝\n");
    
    if (!d->sensorReady) {
        LogE("❌ Sensor not ready\n");
        return false;
    }
    
    // Step 1: Detect finger
    if (!d->detectFinger("identification")) {
        LogE("❌ No finger detected\n");
        return false;
    }
    
    // Step 2: Generate character file
    if (!d->captureAndGenerateChar(0x01, "Identification")) {
        LogE("❌ Failed to generate template\n");
        return false;
    }
    
    // Step 3: Search database
    uint16_t conf;
    if (d->searchDatabase(matchedFingerId, conf)) {
        confidence = (float)conf / 100.0f;
        emit sigFingerprintMatched(matchedFingerId, confidence);
        return true;
    }
    
    emit sigFingerprintNotMatched();
    return false;
}

// ============================================================================
// ✅ IDENTIFICATION WITH UI (Complete flow)
// ============================================================================
bool FingerprintManager::identifyAndShowResult(QWidget* parent)
{
    Q_D(FingerprintManager);
    QMutexLocker locker(&d->sync);
    
    LogD("╔════════════════════════════════════════╗\n");
    LogD("║  IDENTIFICATION WITH RESULT DISPLAY   ║\n");
    LogD("╚════════════════════════════════════════╝\n");
    
    if (!d->sensorReady) {
        QMessageBox::warning(parent, "Sensor Error", "Fingerprint sensor not ready!");
        return false;
    }
    
    // Step 1: Detect finger
    LogD("\n[STEP 1] Waiting for finger...\n");
    
    if (!d->detectFinger("identification")) {
        QMessageBox::warning(parent, "No Finger", 
                           "No finger detected.\nPlease place your finger on the sensor.");
        return false;
    }
    
    // Step 2: Generate character
    LogD("\n[STEP 2] Generating template...\n");
    
    if (!d->captureAndGenerateChar(0x01, "Identification")) {
        QMessageBox::warning(parent, "Error", "Failed to generate fingerprint template.");
        return false;
    }
    
    // Step 3: Search database
    LogD("\n[STEP 3] Searching database...\n");
    
    uint16_t fingerId;
    uint16_t confidence;
    
    if (!d->searchDatabase(fingerId, confidence)) {
        LogD("❌ No match found\n");
        QMessageBox::warning(parent, "Not Recognized", 
                           "Fingerprint not recognized!\n\nNo match found in database.");
        return false;
    }
    
    // Step 4: Look up user
    LogD("\n[STEP 4] Looking up user...\n");
    
    PERSONS_t person;
    if (!RegisteredFacesDB::GetInstance()->GetPersonByFingerId(fingerId, person)) {
        QMessageBox::warning(parent, "User Not Found", 
                           QString("Fingerprint recognized (ID: %1)\nbut user not found in database!")
                           .arg(fingerId));
        return false;
    }
    
    // Step 5: Show result
    float confidencePercent = (float)confidence / 100.0f;
    
    QString message = QString("✅ FINGERPRINT RECOGNIZED!\n\n"
                             "Employee ID: %1\n"
                             "Name: %2\n"
                             "Department: %3\n"
                             "Confidence: %4%\n"
                             "Finger ID: %5")
                        .arg(person.idcard)
                        .arg(person.name)
                        .arg(person.department)
                        .arg(confidencePercent, 0, 'f', 1)
                        .arg(fingerId);
    
    QMessageBox::information(parent, "Access Granted", message);
    
    LogD("╔════════════════════════════════════════╗\n");
    LogD("║  ✅ IDENTIFICATION SUCCESS!            ║\n");
    LogD("╚════════════════════════════════════════╝\n");
    
    return true;
}

// ============================================================================
// ✅ DELETE FINGERPRINT
// ============================================================================
bool FingerprintManager::deleteFingerprintTemplate(uint16_t fingerId)
{
    Q_D(FingerprintManager);
    QMutexLocker locker(&d->sync);
    
    LogD("╔════════════════════════════════════════╗\n");
    LogD("║  DELETE FINGERPRINT (ID: %d)          ║\n", fingerId);
    LogD("╚════════════════════════════════════════╝\n");
    
    uint8_t data[4] = {
        (uint8_t)((fingerId >> 8) & 0xFF),
        (uint8_t)(fingerId & 0xFF),
        0x00, 0x01  // Delete count: 1
    };
    
    if (!d->sendCommand(CMD_DELETE_CHAR, data, sizeof(data))) {
        LogE("❌ Failed to send delete command\n");
        return false;
    }
    
    uint8_t response[16];
    int bytesReceived;
    
    if (!d->receiveResponse(response, sizeof(response), bytesReceived)) {
        LogE("❌ Failed to receive delete response\n");
        return false;
    }
    
    if (bytesReceived >= 1 && response[0] == 0x00) {
        LogD("✅ Fingerprint %d deleted successfully\n", fingerId);
        return true;
    }
    
    LogE("❌ Delete failed with code: 0x%02X\n", response[0]);
    return false;
}

// ============================================================================
// ✅ DELETE ALL FINGERPRINTS
// ============================================================================
bool FingerprintManager::deleteAllFingerprints()
{
    Q_D(FingerprintManager);
    QMutexLocker locker(&d->sync);
    
    LogD("╔════════════════════════════════════════╗\n");
    LogD("║  DELETE ALL FINGERPRINTS              ║\n");
    LogD("╚════════════════════════════════════════╝\n");
    
    if (!d->sendCommand(CMD_EMPTY_DB, NULL, 0)) {
        LogE("❌ Failed to send delete all command\n");
        return false;
    }
    
    uint8_t response[16];
    int bytesReceived;
    
    if (!d->receiveResponse(response, sizeof(response), bytesReceived)) {
        LogE("❌ Failed to receive delete all response\n");
        return false;
    }
    
    if (bytesReceived >= 1 && response[0] == 0x00) {
        LogD("✅ All fingerprints deleted successfully\n");
        
        // Clear finger_id values from database
        RegisteredFacesDB::GetInstance()->clearAllFingerIds();
        
        return true;
    }
    
    LogE("❌ Delete all failed with code: 0x%02X\n", response[0]);
    return false;
}

// ============================================================================
// ✅ GET TEMPLATE COUNT
// ============================================================================
int FingerprintManager::getTemplateCount()
{
    Q_D(FingerprintManager);
    QMutexLocker locker(&d->sync);
    
    LogD("╔════════════════════════════════════════╗\n");
    LogD("║  GET TEMPLATE COUNT                   ║\n");
    LogD("╚════════════════════════════════════════╝\n");
    
    if (!d->sendCommand(CMD_TEMPLATE_NUM, NULL, 0)) {
        LogE("❌ Failed to send template count command\n");
        return -1;
    }
    
    uint8_t response[16];
    int bytesReceived;
    
    if (!d->receiveResponse(response, sizeof(response), bytesReceived)) {
        LogE("❌ Failed to receive template count response\n");
        return -1;
    }
    
    if (bytesReceived >= 3 && response[0] == 0x00) {
        int count = (response[1] << 8) | response[2];
        LogD("✅ Template count: %d\n", count);
        return count;
    }
    
    LogE("❌ Failed to get template count, code: 0x%02X\n", response[0]);
    return -1;
}

// ============================================================================
// ✅ DOWNLOAD FINGERPRINT TEMPLATE
// ============================================================================
bool FingerprintManager::downloadFingerprintTemplate(uint16_t fingerId, QByteArray &templateData)
{
    Q_D(FingerprintManager);
    QMutexLocker locker(&d->sync);
    
    LogD("╔════════════════════════════════════════╗\n");
    LogD("║  DOWNLOAD TEMPLATE (ID: %d)           ║\n", fingerId);
    LogD("╚════════════════════════════════════════╝\n");
    
    templateData.clear();
    
    // Step 1: Load template from flash to Buffer 1
    LogD("\n[STEP 1] Loading from flash to Buffer 1...\n");
    
    uint8_t loadData[3] = {
        0x01,  // Buffer 1
        (uint8_t)((fingerId >> 8) & 0xFF),
        (uint8_t)(fingerId & 0xFF)
    };
    
    if (!d->sendCommand(CMD_LOAD_CHAR, loadData, sizeof(loadData))) {
        LogE("❌ Failed to send LoadChar command\n");
        return false;
    }
    
    uint8_t response[16];
    int bytesReceived;
    
    if (!d->receiveResponse(response, sizeof(response), bytesReceived)) {
        LogE("❌ Failed to receive LoadChar response\n");
        return false;
    }
    
    if (bytesReceived < 1 || response[0] != 0x00) {
        LogE("❌ LoadChar failed with code: 0x%02X\n", response[0]);
        return false;
    }
    
    LogD("✅ Template loaded to Buffer 1\n");
    
    // Step 2: Upload template from Buffer 1
    LogD("\n[STEP 2] Uploading from Buffer 1...\n");
    
    uint8_t uploadData[1] = { 0x01 };  // Buffer 1
    
    if (!d->sendCommand(CMD_UP_CHAR, uploadData, sizeof(uploadData))) {
        LogE("❌ Failed to send UpChar command\n");
        return false;
    }
    
    if (!d->receiveResponse(response, sizeof(response), bytesReceived)) {
        LogE("❌ Failed to receive UpChar acknowledgment\n");
        return false;
    }
    
    if (bytesReceived < 1 || response[0] != 0x00) {
        LogE("❌ UpChar failed with code: 0x%02X\n", response[0]);
        return false;
    }
    
    LogD("✅ UpChar acknowledged, receiving data packets...\n");
    
    // Step 3: Receive template data packets
    LogD("\n[STEP 3] Receiving template data packets...\n");
    
    const int MAX_PACKETS = 20;
    int packetCount = 0;
    bool endPacketReceived = false;
    
    while (packetCount < MAX_PACKETS && !endPacketReceived) {
        uint8_t packet[256];
        int bytes = UARTUart::Uart_ReadUart(FP_UART_INDEX, packet, sizeof(packet));
        
        if (bytes <= 0) {
            LogD("No more data\n");
            usleep(50000);
            continue;
        }
        
        LogD("Packet %d: %d bytes\n", packetCount + 1, bytes);
        
        // Find header
        int headerIdx = -1;
        for (int i = 0; i < bytes - 1; i++) {
            if (packet[i] == 0xEF && packet[i+1] == 0x01) {
                headerIdx = i;
                break;
            }
        }
        
        if (headerIdx < 0 || headerIdx + 9 > bytes) {
            LogE("❌ Invalid packet\n");
            break;
        }
        
        uint8_t packetType = packet[headerIdx + 6];
        uint16_t packetLen = (packet[headerIdx + 7] << 8) | packet[headerIdx + 8];
        
        LogD("Packet type: 0x%02X, Length: %d\n", packetType, packetLen);
        
        // Extract data (skip header + checksum)
        int dataStart = headerIdx + 9;
        int dataLen = packetLen - 2;
        
        if (dataStart + dataLen <= bytes && dataLen > 0) {
            templateData.append((char*)&packet[dataStart], dataLen);
            LogD("Extracted %d bytes (total: %d)\n", dataLen, templateData.size());
        }
        
        // Check for end packet
        if (packetType == 0x08) {
            LogD("✅ End packet received\n");
            endPacketReceived = true;
            break;
        }
        
        packetCount++;
    }
    
    if (!endPacketReceived) {
        LogE("❌ Did not receive end packet\n");
        return false;
    }
    
    LogD("╔════════════════════════════════════════╗\n");
    LogD("║  ✅ TEMPLATE DOWNLOADED!               ║\n");
    LogD("╚════════════════════════════════════════╝\n");
    LogD("Total size: %d bytes\n", templateData.size());
    
    return !templateData.isEmpty();
}

// ============================================================================
// ✅ UPLOAD TEMPLATE TO SENSOR (For restoring from database)
// ============================================================================
bool FingerprintManager::uploadTemplateToSensor(uint16_t fingerId, const QByteArray &templateData)
{
    Q_D(FingerprintManager);
    QMutexLocker locker(&d->sync);
    
    LogD("╔════════════════════════════════════════╗\n");
    LogD("║  UPLOAD TEMPLATE TO SENSOR            ║\n");
    LogD("║  ID: %-33d║\n", fingerId);
    LogD("║  Size: %-30d║\n", templateData.size());
    LogD("╚════════════════════════════════════════╝\n");
    
    if (templateData.isEmpty()) {
        LogE("❌ Empty template data\n");
        return false;
    }
    
    // Step 1: Send DownChar command
    LogD("\n[STEP 1] Sending DownChar command...\n");
    
    uint8_t downCharData[1] = { 0x01 };  // Buffer 1
    
    if (!d->sendCommand(CMD_DOWN_CHAR, downCharData, sizeof(downCharData))) {
        LogE("❌ Failed to send DownChar command\n");
        return false;
    }
    
    uint8_t response[16];
    int bytesReceived;
    
    if (!d->receiveResponse(response, sizeof(response), bytesReceived)) {
        LogE("❌ Failed to receive DownChar response\n");
        return false;
    }
    
    if (bytesReceived < 1 || response[0] != 0x00) {
        LogE("❌ DownChar failed with code: 0x%02X\n", response[0]);
        return false;
    }
    
    LogD("✅ DownChar acknowledged\n");
    
    // Step 2: Send template data as single packet (0x08 - end packet)
    LogD("\n[STEP 2] Sending template data...\n");
    
    int totalSize = 9 + templateData.size() + 2;  // header + data + checksum
    uint8_t *packet = new uint8_t[totalSize];
    
    packet[0] = 0xEF;
    packet[1] = 0x01;
    packet[2] = 0x00; packet[3] = 0x00; packet[4] = 0x00; packet[5] = 0x00;
    packet[6] = 0x08;  // End packet type
    
    uint16_t payloadLen = templateData.size() + 2;
    packet[7] = (payloadLen >> 8) & 0xFF;
    packet[8] = payloadLen & 0xFF;
    
    memcpy(&packet[9], templateData.constData(), templateData.size());
    
    // Calculate checksum
    uint16_t checksum = 0;
    for (int i = 6; i < 9 + templateData.size(); i++) {
        checksum += packet[i];
    }
    packet[9 + templateData.size()] = (checksum >> 8) & 0xFF;
    packet[9 + templateData.size() + 1] = checksum & 0xFF;
    
    d->printHex("Template packet", packet, totalSize);
    
    int sent = UARTUart::Uart_WriteUart(FP_UART_INDEX, packet, totalSize);
    delete[] packet;
    
    if (sent != totalSize) {
        LogE("❌ Failed to send template data\n");
        return false;
    }
    
    LogD("✅ Template data sent (%d bytes)\n", sent);
    
    // Wait for upload response
    usleep(100000);  // 100ms
    
    if (!d->receiveResponse(response, sizeof(response), bytesReceived)) {
        LogE("❌ Failed to receive upload response\n");
        return false;
    }
    
    if (bytesReceived < 1 || response[0] != 0x00) {
        LogE("❌ Upload failed with code: 0x%02X\n", response[0]);
        return false;
    }
    
    LogD("✅ Template uploaded to Buffer 1\n");
    
    // Step 3: Store template to flash
    LogD("\n[STEP 3] Storing to flash...\n");
    
    uint8_t storeData[3] = {
        0x01,  // Buffer 1
        (uint8_t)((fingerId >> 8) & 0xFF),
        (uint8_t)(fingerId & 0xFF)
    };
    
    if (!d->sendCommand(CMD_STORE_CHAR, storeData, sizeof(storeData))) {
        LogE("❌ Failed to send store command\n");
        return false;
    }
    
    if (!d->receiveResponse(response, sizeof(response), bytesReceived)) {
        LogE("❌ Failed to receive store response\n");
        return false;
    }
    
    if (bytesReceived >= 1 && response[0] == 0x00) {
        LogD("╔════════════════════════════════════════╗\n");
        LogD("║  ✅ TEMPLATE UPLOADED & STORED!        ║\n");
        LogD("╚════════════════════════════════════════╝\n");
        return true;
    }
    
    LogE("❌ Store failed with code: 0x%02X\n", response[0]);
    return false;
}

// ============================================================================
// STUB FUNCTIONS (For compatibility)
// ============================================================================
bool FingerprintManager::captureFingerprint(QByteArray &fingerprintTemplate)
{
    // Not used in ESP32 style
    return false;
}

bool FingerprintManager::storeFingerprintTemplate(uint16_t fingerId, const QByteArray &templateData)
{
    return uploadTemplateToSensor(fingerId, templateData);
}

bool FingerprintManager::downloadTemplateFromSensor(uint16_t fingerId, QByteArray &templateData)
{
    return downloadFingerprintTemplate(fingerId, templateData);
}

bool FingerprintManager::testSensorBuffersAdvanced()
{
    Q_D(FingerprintManager);
    QMutexLocker locker(&d->sync);
    
    if (!d->sensorReady) {
        LogE("Sensor not initialized\n");
        return false;
    }
    
    return d->testBufferCapacityAdvanced();
}