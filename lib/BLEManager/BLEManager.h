#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string>
#include "Task.h"

// Forward declaration
class StepperController;

class BLEManager : public Task {
private:
    // BLE Service and Characteristic UUIDs
    static const char* SERVICE_UUID;
    static const char* COMMAND_CHARACTERISTIC_UUID;
    
    // BLE objects
    BLEServer* server;
    BLEService* service;
    BLECharacteristic* commandCharacteristic;
    
    // State
    bool deviceConnected;
    bool oldDeviceConnected;
    
    // Reference to stepper controller
    StepperController* stepperController;
    
    // Status update timing
    unsigned long lastStatusUpdate;
    static const unsigned long STATUS_UPDATE_INTERVAL = 1000; // 1 second
    
    // Command queue for safe processing using FreeRTOS queue
    QueueHandle_t commandQueue;
    static const size_t MAX_QUEUE_SIZE = 10;
    static const size_t MAX_COMMAND_LENGTH = 256;

protected:
    // Task implementation
    void run() override;
    
public:
    BLEManager();
    ~BLEManager();
    
    // Initialization
    bool begin(const char* deviceName = "BratenDreher");
    void setStepperController(StepperController* controller);
    
    // Connection status
    bool isConnected() const { return deviceConnected; }
    
    // Status updates
    void update();
    void updateStatus();
    void sendStatus();
    
    // Handle incoming commands
    bool queueCommand(const std::string& command);
    void handleCommand(const std::string& command);
    void processQueuedCommands();
    
    // Server callbacks
    class ServerCallbacks;
    friend class ServerCallbacks;
    
    // Characteristic callbacks
    class CommandCharacteristicCallbacks;
    friend class CommandCharacteristicCallbacks;
};

#endif // BLE_MANAGER_H
