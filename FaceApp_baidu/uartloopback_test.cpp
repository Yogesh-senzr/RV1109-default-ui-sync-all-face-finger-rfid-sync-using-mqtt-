#include "UARTUart.h"
#include <iostream>
#include <string.h>
#include <unistd.h>

using namespace YNH_LJX;

// Simple UART loopback test function - add this to your existing project
bool quickUARTTest(int portIndex = 0, int baudRate = 115200) {
    UARTUart uart;
    UART_ATTR_S attr;
    
    // Configure UART attributes
    attr.nBaudRate = baudRate;
    attr.RDBlock = 1;        // Blocking mode
    attr.mBlockData = 1;     // Minimum bytes to read
    attr.mBlockTime = 5;     // 0.5 second timeout
    
    std::cout << "Testing UART port " << portIndex << " at " << baudRate << " baud..." << std::endl;
    
    // Open UART
    if (uart.Uart_OpenUartDev(portIndex, attr) != ISC_OK) {
        std::cout << "Failed to open UART port " << portIndex << std::endl;
        return false;
    }
    
    // Test data
    const char* testMsg = "TEST123";
    unsigned char sendBuffer[32];
    unsigned char receiveBuffer[32];
    
    strcpy((char*)sendBuffer, testMsg);
    memset(receiveBuffer, 0, sizeof(receiveBuffer));
    
    // Clear buffers
    uart.Uart_ReadFlush(portIndex);
    uart.Uart_WriteFlush(portIndex);
    
    // Send test data
    int bytesSent = uart.Uart_WriteUart(portIndex, sendBuffer, strlen(testMsg));
    if (bytesSent != strlen(testMsg)) {
        std::cout << "Send failed: " << bytesSent << "/" << strlen(testMsg) << " bytes" << std::endl;
        uart.Uart_CloseUartDev(portIndex);
        return false;
    }
    
    // Wait a bit for loopback
    usleep(10000); // 10ms
    
    // Try to receive data back
    int bytesReceived = uart.Uart_ReadUart(portIndex, receiveBuffer, strlen(testMsg));
    
    // Close UART
    uart.Uart_CloseUartDev(portIndex);
    
    // Check results
    if (bytesReceived == strlen(testMsg) && 
        memcmp(sendBuffer, receiveBuffer, strlen(testMsg)) == 0) {
        std::cout << "UART Test PASSED - Sent: " << testMsg 
                  << ", Received: " << (char*)receiveBuffer << std::endl;
        return true;
    } else {
        std::cout << "UART Test FAILED - Sent: " << testMsg 
                  << ", Received: " << bytesReceived << " bytes: " 
                  << (char*)receiveBuffer << std::endl;
        return false;
    }
}

// Test all available UART ports
void testAllUARTPorts() {
    std::cout << "\n=== Testing All UART Ports ===" << std::endl;
    std::cout << "Note: Connect TX and RX pins together for loopback test" << std::endl;
    
    for (int port = 0; port < 6; port++) {
        std::cout << "\nPort " << port << ": ";
        if (quickUARTTest(port)) {
            std::cout << "✓ WORKING" << std::endl;
        } else {
            std::cout << "✗ FAILED" << std::endl;
        }
    }
}
