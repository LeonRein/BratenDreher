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

// Forward declaration (header will be included in .cpp file)
class StepperController;
struct CommandResultData;

class BLEManager : public Task {
private:
    // BLE Service and Characteristic UUIDs
    static const char* SERVICE_UUID;
    static const char* COMMAND_CHARACTERISTIC_UUID;
    
    // BLE objects
    BLEServer* server;
    BLEService* service;
    BLECharacteristic* commandCharacteristic;
    
    // Forward declaration of callback classes
    class ServerCallbacks;
    class CommandCharacteristicCallbacks;
    
    // BLE callback objects (stored for proper cleanup)
    ServerCallbacks* serverCallbacks;
    CommandCharacteristicCallbacks* commandCallbacks;
    
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
    
    // Cached status from StepperController for thread-safe access
    struct {
        float speed = 1.0f;
        uint32_t acceleration = 0;
        bool enabled = false;
        bool clockwise = true;
        int current = 30;
        bool speedVariationEnabled = false;
        float speedVariationStrength = 0.0f;
        float speedVariationPhase = 0.0f;
        float totalRevolutions = 0.0f;
        unsigned long runTime = 0;
        bool isRunning = false;
        bool stallDetected = false;
        uint16_t stallCount = 0;
        bool tmc2209Status = false;
        float currentVariableSpeed = 1.0f;
    } cachedStatus;

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
    void processCommandResults(); // Process command results from StepperController
    void processStatusUpdates(); // Process status updates from StepperController
    void sendCommandResult(uint32_t commandId, const String& status, const String& message = "");
    
    // Handle incoming commands
    bool queueCommand(const std::string& command);
    void handleCommand(const std::string& command);
    void processQueuedCommands();
    
    // Friend declarations for callback access
    friend class ServerCallbacks;
    friend class CommandCharacteristicCallbacks;
};

#endif // BLE_MANAGER_H
