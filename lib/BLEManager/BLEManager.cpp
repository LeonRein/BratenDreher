#include "BLEManager.h"
#include "StepperController.h"
#include <ArduinoJson.h>

// BLE Service and Characteristic UUIDs
const char* BLEManager::SERVICE_UUID = "12345678-1234-1234-1234-123456789abc";
const char* BLEManager::COMMAND_CHARACTERISTIC_UUID = "12345678-1234-1234-1234-123456789ab1";

// Server Callbacks
class BLEManager::ServerCallbacks : public BLEServerCallbacks {
private:
    BLEManager* bleManager;
    
public:
    ServerCallbacks(BLEManager* manager) : bleManager(manager) {}
    
    void onConnect(BLEServer* pServer) override {
        bleManager->deviceConnected = true;
        // No Serial print in time-critical callback
    }
    
    void onDisconnect(BLEServer* pServer) override {
        bleManager->deviceConnected = false;
        
        // Emergency stop on disconnect (critical safety feature)
        // Use thread-safe interface - emergency stop will be processed immediately
        if (bleManager->stepperController) {
            bleManager->stepperController->emergencyStop();
        }
        
        // Restart advertising immediately (safe in task context)
        pServer->startAdvertising();
        // No Serial print in time-critical callback
    }
};

// Command Characteristic Callbacks
class BLEManager::CommandCharacteristicCallbacks : public BLECharacteristicCallbacks {
private:
    BLEManager* bleManager;
    
public:
    CommandCharacteristicCallbacks(BLEManager* manager) : bleManager(manager) {}
    
    void onWrite(BLECharacteristic* pCharacteristic) override {
        std::string value = pCharacteristic->getValue();
        
        if (value.length() > 0 && value.length() <= BLEManager::MAX_COMMAND_LENGTH) {
            // Queue the command for processing in the BLE task instead of processing here
            // This avoids stack issues in the BLE callback context
            // No Serial prints in time-critical callback context
            bleManager->queueCommand(value);
        }
    }
};

BLEManager::BLEManager() 
    : Task("BLE_Task", 12288, 2, 0), // Task name, 12KB stack, priority 2, core 0
      server(nullptr), service(nullptr), commandCharacteristic(nullptr),
      deviceConnected(false), oldDeviceConnected(false),
      stepperController(nullptr), lastStatusUpdate(0) {
    
    // Create FreeRTOS queue for commands
    commandQueue = xQueueCreate(MAX_QUEUE_SIZE, MAX_COMMAND_LENGTH);
    if (commandQueue == nullptr) {
        Serial.println("ERROR: Failed to create command queue!");
    }
}

BLEManager::~BLEManager() {
    if (commandQueue != nullptr) {
        vQueueDelete(commandQueue);
    }
}

bool BLEManager::begin(const char* deviceName) {
    Serial.println("Initializing BLE...");
    
    // Initialize BLE
    BLEDevice::init(deviceName);
    
    // Create BLE Server
    server = BLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks(this));
    
    // Create BLE Service
    service = server->createService(SERVICE_UUID);
    
    // Create Command Characteristic (bidirectional)
    Serial.println("Creating command characteristic...");
    commandCharacteristic = service->createCharacteristic(
        COMMAND_CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ | 
        BLECharacteristic::PROPERTY_WRITE | 
        BLECharacteristic::PROPERTY_NOTIFY
    );
    commandCharacteristic->setCallbacks(new CommandCharacteristicCallbacks(this));
    commandCharacteristic->addDescriptor(new BLE2902());
    Serial.println("Command characteristic created");
    
    // Verify characteristic was created successfully
    if (!commandCharacteristic) {
        Serial.println("ERROR: Failed to create command characteristic!");
        return false;
    }
    
    Serial.println("BLE characteristic created successfully");
    
    // Start the service
    Serial.println("Starting BLE service...");
    service->start();
    
    // Start advertising
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(false);
    pAdvertising->setMinPreferred(0x0);
    BLEDevice::startAdvertising();
    
    Serial.println("BLE service started. Waiting for client connection...");
    Serial.printf("Device name: %s\n", deviceName);
    
    return true;
}

void BLEManager::setStepperController(StepperController* controller) {
    stepperController = controller;
}

void BLEManager::handleCommand(const std::string& command) {
    if (!stepperController) return;
    
    Serial.printf("Processing command: %s (length: %d)\n", command.c_str(), command.length());
    
    // Use smaller JSON document to reduce stack usage
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, command);
    
    if (error) {
        Serial.printf("JSON parse error: %s\n", error.c_str());
        return;
    }
    
    const char* type = doc["type"];
    if (!type) {
        Serial.println("Missing command type");
        return;
    }
    
    Serial.printf("Processing command type: %s\n", type);
    
    if (strcmp(type, "speed") == 0) {
        float speed = doc["value"];
        if (stepperController->setSpeed(speed)) {
            Serial.printf("Speed command queued: %.2f RPM\n", speed);
        } else {
            Serial.println("Failed to queue speed command");
        }
    }
    else if (strcmp(type, "direction") == 0) {
        bool clockwise = doc["value"];
        if (stepperController->setDirection(clockwise)) {
            Serial.printf("Direction command queued: %s\n", clockwise ? "clockwise" : "counter-clockwise");
        } else {
            Serial.println("Failed to queue direction command");
        }
    }
    else if (strcmp(type, "enable") == 0) {
        bool enable = doc["value"];
        bool success = false;
        if (enable) {
            success = stepperController->enable();
        } else {
            success = stepperController->disable();
        }
        if (success) {
            Serial.printf("Motor %s command queued\n", enable ? "enable" : "disable");
        } else {
            Serial.printf("Failed to queue motor %s command\n", enable ? "enable" : "disable");
        }
    }
    else if (strcmp(type, "microsteps") == 0) {
        int microsteps = doc["value"];
        if (microsteps > 0) {
            if (stepperController->setMicroSteps(microsteps)) {
                Serial.printf("Microsteps command queued: %d\n", microsteps);
            } else {
                Serial.println("Failed to queue microsteps command");
            }
        }
    }
    else if (strcmp(type, "current") == 0) {
        int current = doc["value"];
        if (current >= 10 && current <= 100) {
            if (stepperController->setRunCurrent(current)) {
                Serial.printf("Current command queued: %d%%\n", current);
            } else {
                Serial.println("Failed to queue current command");
            }
        }
    }
    else if (strcmp(type, "reset") == 0) {
        if (stepperController->resetCounters()) {
            Serial.println("Reset counters command queued");
        } else {
            Serial.println("Failed to queue reset counters command");
        }
    }
    else if (strcmp(type, "status_request") == 0) {
        sendStatus();
    }
    else {
        Serial.printf("Unknown command type: %s\n", type);
    }
}

void BLEManager::sendStatus() {
    if (!commandCharacteristic || !stepperController || !deviceConnected) {
        Serial.println("Cannot send status: characteristic, controller, or connection not available");
        return;
    }
    
    // Use static JSON document to reduce stack usage
    JsonDocument statusDoc;
    statusDoc["type"] = "status";
    statusDoc["enabled"] = stepperController->isEnabled();
    statusDoc["speed"] = stepperController->getSpeed();
    statusDoc["direction"] = stepperController->isClockwise() ? "cw" : "ccw";
    statusDoc["running"] = stepperController->isRunning();
    statusDoc["connected"] = deviceConnected;
    statusDoc["totalRevolutions"] = stepperController->getTotalRevolutions();
    statusDoc["runtime"] = stepperController->getRunTime();
    statusDoc["microsteps"] = stepperController->getMicroSteps();
    statusDoc["current"] = stepperController->getRunCurrent();
    statusDoc["timestamp"] = millis();
    
    String statusString;
    serializeJson(statusDoc, statusString);
    
    // Check if the message is too long for BLE
    if (statusString.length() > 500) {
        Serial.printf("Warning: Status message is long (%d bytes)\n", statusString.length());
    }
    
    try {
        commandCharacteristic->setValue(statusString.c_str());
        commandCharacteristic->notify();
        Serial.printf("Status sent: %s\n", statusString.c_str());
    } catch (...) {
        Serial.println("Failed to send status notification");
    }
}

void BLEManager::updateStatus() {
    sendStatus();
}

void BLEManager::run() {
    Serial.println("BLE Task started");
    
    // Initialize BLE manager
    if (!begin("BratenDreher")) {
        Serial.println("Failed to initialize BLE manager!");
        return;
    }
    
    Serial.println("BLE Manager initialized successfully!");
    
    while (true) {
        update(); // update() now includes its own delay
    }
}

void BLEManager::update() {
    // Process any queued commands with timeout (non-blocking)
    processQueuedCommands();
    
    // Handle connection state changes
    if (!deviceConnected && oldDeviceConnected) {
        server->startAdvertising(); // Restart advertising
        Serial.println("BLE client disconnected - restarted advertising");
        oldDeviceConnected = deviceConnected;
    }
    
    if (deviceConnected && !oldDeviceConnected) {
        oldDeviceConnected = deviceConnected;
        Serial.println("BLE client connected");
        // Send initial status in next update cycle
    }
    
    // Update status periodically
    unsigned long currentTime = millis();
    if (currentTime - lastStatusUpdate >= STATUS_UPDATE_INTERVAL) {
        updateStatus();
        lastStatusUpdate = currentTime;
    }
    
    // Small delay to prevent busy waiting when no commands are queued
    vTaskDelay(pdMS_TO_TICKS(10));
}

bool BLEManager::queueCommand(const std::string& command) {
    if (commandQueue == nullptr || command.length() > MAX_COMMAND_LENGTH) {
        return false;
    }
    
    // Convert string to char array for queue
    char commandBuffer[MAX_COMMAND_LENGTH];
    strncpy(commandBuffer, command.c_str(), MAX_COMMAND_LENGTH - 1);
    commandBuffer[MAX_COMMAND_LENGTH - 1] = '\0';
    
    // Try to send to queue without blocking (called from ISR context)
    BaseType_t result = xQueueSendFromISR(commandQueue, commandBuffer, nullptr);
    return result == pdTRUE;
}

void BLEManager::processQueuedCommands() {
    char commandBuffer[MAX_COMMAND_LENGTH];
    
    // Process commands with timeout to avoid busy waiting
    while (xQueueReceive(commandQueue, commandBuffer, pdMS_TO_TICKS(5)) == pdTRUE) {
        std::string command(commandBuffer);
        Serial.printf("Processing queued command: %s\n", command.c_str());
        handleCommand(command);
    }
}
