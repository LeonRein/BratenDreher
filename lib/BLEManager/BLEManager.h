#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <string>
#include <ArduinoJson.h>
#include "Task.h"
#include "SystemStatus.h"
#include "SystemCommand.h"

// Forward declaration (header will be included in .cpp file)
class SystemStatus;
class SystemCommand;
struct NotificationData;
struct StatusUpdateData;

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
    
    // State. deviceConnected is written from the BLE stack's callback context
    // and read from the BLE task, so it must not be cached in a register.
    volatile bool deviceConnected;
    bool oldDeviceConnected;
    
    // Cached reference to SystemStatus singleton
    SystemStatus& systemStatus;
    
    // Cached reference to SystemCommand singleton  
    SystemCommand& systemCommand;
    
    // Status update batching configuration
    static constexpr uint16_t BLE_MTU = 517;              // Largest MTU we ask for
    static constexpr size_t STATUS_BATCH_HEADROOM = 32;   // Stop batching this far from the limit
    static constexpr size_t ATT_NOTIFY_OVERHEAD = 3;      // ATT opcode + handle

    // Largest notification payload the current connection can actually carry
    size_t maxNotifyPayload() const;
    // Serialized MsgPack size of a document, without writing it anywhere
    static size_t measureMsgPackSize(const JsonDocument& doc);

    BLEManager();
    ~BLEManager();
    BLEManager(const BLEManager&) = delete;
    BLEManager& operator=(const BLEManager&) = delete;

    // Task implementation
    
    void processNotifications(); // Process notifications from StepperController (warnings and errors only)
    void processStatusUpdates(); // Process status updates from StepperController
    void update();
    bool isConnected() const { return deviceConnected; }
    void addStatusToJson(JsonDocument& doc, const StatusUpdateData& statusUpdate); // Helper to add status to JSON
    void sendStatusUpdate(uint8_t* buffer, size_t len); // Send a status update
    void sendNotification(const char* level, const char* message = "");
    void handleCommand(const std::string& command);

    friend class ServerCallbacks;
    friend class CommandCharacteristicCallbacks;

protected:    
    void run() override;
public:
    bool begin(const char* deviceName = "BratenDreher");

    static BLEManager& getInstance() {
        static BLEManager instance;
        return instance;
    }
};

#endif // BLE_MANAGER_H
