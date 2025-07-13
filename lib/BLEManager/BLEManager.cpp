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
        Serial.println("BLE Client connected");
    }
    
    void onDisconnect(BLEServer* pServer) override {
        bleManager->deviceConnected = false;
        Serial.println("BLE Client disconnected");
        
        // Emergency stop on disconnect
        if (bleManager->stepperController) {
            bleManager->stepperController->emergencyStop();
        }
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
        
        if (value.length() > 0) {
            Serial.printf("Received command: %s\n", value.c_str());
            bleManager->handleCommand(value);
        }
    }
};

BLEManager::BLEManager() 
    : server(nullptr), service(nullptr), commandCharacteristic(nullptr),
      deviceConnected(false), oldDeviceConnected(false),
      stepperController(nullptr), lastStatusUpdate(0) {
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
    
    // Parse JSON command
    StaticJsonDocument<200> doc;
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
    
    Serial.printf("Processing command: %s\n", type);
    
    if (strcmp(type, "speed") == 0) {
        float speed = doc["value"];
        stepperController->setSpeed(speed);
        Serial.printf("Speed set to: %.2f RPM\n", speed);
    }
    else if (strcmp(type, "direction") == 0) {
        bool clockwise = doc["value"];
        stepperController->setDirection(clockwise);
        Serial.printf("Direction set to: %s\n", clockwise ? "clockwise" : "counter-clockwise");
    }
    else if (strcmp(type, "enable") == 0) {
        bool enable = doc["value"];
        if (enable) {
            stepperController->enable();
        } else {
            stepperController->disable();
        }
        Serial.printf("Motor %s\n", enable ? "enabled" : "disabled");
    }
    else if (strcmp(type, "microsteps") == 0) {
        int microsteps = doc["value"];
        if (microsteps > 0) {
            stepperController->setMicroSteps(microsteps);
            Serial.printf("Microsteps set to: %d\n", microsteps);
        }
    }
    else if (strcmp(type, "current") == 0) {
        int current = doc["value"];
        if (current >= 10 && current <= 100) {
            stepperController->setRunCurrent(current);
            Serial.printf("Current set to: %d%%\n", current);
        }
    }
    else if (strcmp(type, "reset") == 0) {
        stepperController->resetCounters();
        Serial.println("Counters reset");
    }
    else if (strcmp(type, "status_request") == 0) {
        sendStatus();
    }
    else {
        Serial.printf("Unknown command type: %s\n", type);
    }
}

void BLEManager::sendStatus() {
    if (!commandCharacteristic || !stepperController) return;
    
    // Create JSON status object
    StaticJsonDocument<400> statusDoc;
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
    
    commandCharacteristic->setValue(statusString.c_str());
    
    if (deviceConnected) {
        commandCharacteristic->notify();
    }
}

void BLEManager::updateStatus() {
    sendStatus();
}

void BLEManager::update() {
    // Handle connection state changes
    if (!deviceConnected && oldDeviceConnected) {
        delay(500); // Give the bluetooth stack time to get things ready
        server->startAdvertising(); // Restart advertising
        Serial.println("Start advertising");
        oldDeviceConnected = deviceConnected;
    }
    
    if (deviceConnected && !oldDeviceConnected) {
        oldDeviceConnected = deviceConnected;
    }
    
    // Update status periodically
    unsigned long currentTime = millis();
    if (currentTime - lastStatusUpdate >= STATUS_UPDATE_INTERVAL) {
        updateStatus();
        lastStatusUpdate = currentTime;
    }
}
