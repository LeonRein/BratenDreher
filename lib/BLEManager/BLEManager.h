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
    
    // Command queue for safe processing using FreeRTOS queue
    QueueHandle_t commandQueue;
    static const size_t MAX_QUEUE_SIZE = 10;
    static const size_t MAX_COMMAND_LENGTH = 256;
    
    // Status update batching configuration
    static const size_t MAX_BLE_PACKET_SIZE = 500;            // Conservative BLE MTU size

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
    void processCommandResults(); // Process command results from StepperController
    void processStatusUpdates(); // Process status updates from StepperController
    void addStatusToJson(JsonDocument& doc, const StatusUpdateData& statusUpdate); // Helper to add status to JSON
    void sendStatusUpdate(JsonDocument& statusDoc); // Send a status update JSON
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
