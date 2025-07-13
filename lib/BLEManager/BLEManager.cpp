#include "BLEManager.h"
#include "StepperController.h"
#include <ArduinoJson.h>

// BLE Service and Characteristic UUIDs
const char* BLEManager::SERVICE_UUID = "12345678-1234-1234-1234-123456789abc";
const char* BLEManager::SPEED_CHARACTERISTIC_UUID = "12345678-1234-1234-1234-123456789ab1";
const char* BLEManager::DIRECTION_CHARACTERISTIC_UUID = "12345678-1234-1234-1234-123456789ab2";
const char* BLEManager::ENABLE_CHARACTERISTIC_UUID = "12345678-1234-1234-1234-123456789ab3";
const char* BLEManager::STATUS_CHARACTERISTIC_UUID = "12345678-1234-1234-1234-123456789ab4";
const char* BLEManager::MICROSTEPS_CHARACTERISTIC_UUID = "12345678-1234-1234-1234-123456789ab5";
const char* BLEManager::CURRENT_CHARACTERISTIC_UUID = "12345678-1234-1234-1234-123456789ab6";
const char* BLEManager::RESET_CHARACTERISTIC_UUID = "12345678-1234-1234-1234-123456789ab7";

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

// Speed Characteristic Callbacks
class BLEManager::SpeedCharacteristicCallbacks : public BLECharacteristicCallbacks {
private:
    BLEManager* bleManager;
    
public:
    SpeedCharacteristicCallbacks(BLEManager* manager) : bleManager(manager) {}
    
    void onWrite(BLECharacteristic* pCharacteristic) override {
        std::string value = pCharacteristic->getValue();
        
        if (value.length() > 0) {
            float speed = atof(value.c_str());
            Serial.printf("Received speed command: %.2f RPM\n", speed);
            
            if (bleManager->stepperController) {
                bleManager->stepperController->setSpeed(speed);
            }
        }
    }
};

// Direction Characteristic Callbacks
class BLEManager::DirectionCharacteristicCallbacks : public BLECharacteristicCallbacks {
private:
    BLEManager* bleManager;
    
public:
    DirectionCharacteristicCallbacks(BLEManager* manager) : bleManager(manager) {}
    
    void onWrite(BLECharacteristic* pCharacteristic) override {
        std::string value = pCharacteristic->getValue();
        
        if (value.length() > 0) {
            bool clockwise = (value[0] == '1');
            Serial.printf("Received direction command: %s\n", clockwise ? "clockwise" : "counter-clockwise");
            
            if (bleManager->stepperController) {
                bleManager->stepperController->setDirection(clockwise);
            }
        }
    }
};

// Enable Characteristic Callbacks
class BLEManager::EnableCharacteristicCallbacks : public BLECharacteristicCallbacks {
private:
    BLEManager* bleManager;
    
public:
    EnableCharacteristicCallbacks(BLEManager* manager) : bleManager(manager) {}
    
    void onWrite(BLECharacteristic* pCharacteristic) override {
        std::string value = pCharacteristic->getValue();
        
        if (value.length() > 0) {
            bool enable = (value[0] == '1');
            Serial.printf("Received enable command: %s\n", enable ? "enable" : "disable");
            
            if (bleManager->stepperController) {
                if (enable) {
                    bleManager->stepperController->enable();
                } else {
                    bleManager->stepperController->disable();
                }
            }
        }
    }
};

// Microsteps Characteristic Callbacks
class BLEManager::MicrostepsCharacteristicCallbacks : public BLECharacteristicCallbacks {
private:
    BLEManager* bleManager;
    
public:
    MicrostepsCharacteristicCallbacks(BLEManager* manager) : bleManager(manager) {}
    
    void onWrite(BLECharacteristic* pCharacteristic) override {
        std::string value = pCharacteristic->getValue();
        
        if (value.length() > 0) {
            int microsteps = atoi(value.c_str());
            Serial.printf("Received microsteps command: %d\n", microsteps);
            
            if (bleManager->stepperController && microsteps > 0) {
                bleManager->stepperController->setMicroSteps(microsteps);
            }
        }
    }
};

// Current Characteristic Callbacks
class BLEManager::CurrentCharacteristicCallbacks : public BLECharacteristicCallbacks {
private:
    BLEManager* bleManager;
    
public:
    CurrentCharacteristicCallbacks(BLEManager* manager) : bleManager(manager) {}
    
    void onWrite(BLECharacteristic* pCharacteristic) override {
        std::string value = pCharacteristic->getValue();
        
        if (value.length() > 0) {
            int current = atoi(value.c_str());
            Serial.printf("Received current command: %d%%\n", current);
            
            if (bleManager->stepperController && current >= 10 && current <= 100) {
                bleManager->stepperController->setRunCurrent(current);
            }
        }
    }
};

// Reset Characteristic Callbacks
class BLEManager::ResetCharacteristicCallbacks : public BLECharacteristicCallbacks {
private:
    BLEManager* bleManager;
    
public:
    ResetCharacteristicCallbacks(BLEManager* manager) : bleManager(manager) {}
    
    void onWrite(BLECharacteristic* pCharacteristic) override {
        std::string value = pCharacteristic->getValue();
        
        if (value.length() > 0 && value[0] == '1') {
            Serial.println("Received reset command");
            
            if (bleManager->stepperController) {
                bleManager->stepperController->resetCounters();
            }
        }
    }
};

BLEManager::BLEManager() 
    : server(nullptr), service(nullptr), speedCharacteristic(nullptr),
      directionCharacteristic(nullptr), enableCharacteristic(nullptr),
      statusCharacteristic(nullptr), microstepsCharacteristic(nullptr),
      currentCharacteristic(nullptr), resetCharacteristic(nullptr),
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
    
    // Create Speed Characteristic
    speedCharacteristic = service->createCharacteristic(
        SPEED_CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
    );
    speedCharacteristic->setCallbacks(new SpeedCharacteristicCallbacks(this));
    speedCharacteristic->setValue("1.0"); // Default speed
    
    // Create Direction Characteristic
    directionCharacteristic = service->createCharacteristic(
        DIRECTION_CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
    );
    directionCharacteristic->setCallbacks(new DirectionCharacteristicCallbacks(this));
    directionCharacteristic->setValue("1"); // Default clockwise
    
    // Create Enable Characteristic
    enableCharacteristic = service->createCharacteristic(
        ENABLE_CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
    );
    enableCharacteristic->setCallbacks(new EnableCharacteristicCallbacks(this));
    enableCharacteristic->setValue("0"); // Default disabled
    
    // Create Status Characteristic (read/notify only)
    statusCharacteristic = service->createCharacteristic(
        STATUS_CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    statusCharacteristic->addDescriptor(new BLE2902());
    
    // Create Microsteps Characteristic
    microstepsCharacteristic = service->createCharacteristic(
        MICROSTEPS_CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
    );
    microstepsCharacteristic->setCallbacks(new MicrostepsCharacteristicCallbacks(this));
    microstepsCharacteristic->setValue("32"); // Default microsteps
    
    // Create Current Characteristic
    currentCharacteristic = service->createCharacteristic(
        CURRENT_CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
    );
    currentCharacteristic->setCallbacks(new CurrentCharacteristicCallbacks(this));
    currentCharacteristic->setValue("30"); // Default current percentage
    
    // Create Reset Characteristic
    resetCharacteristic = service->createCharacteristic(
        RESET_CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    resetCharacteristic->setCallbacks(new ResetCharacteristicCallbacks(this));
    
    // Start the service
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

void BLEManager::updateStatus() {
    if (!statusCharacteristic || !stepperController) return;
    
    // Create JSON status object
    StaticJsonDocument<300> statusDoc;
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
    
    statusCharacteristic->setValue(statusString.c_str());
    
    if (deviceConnected) {
        statusCharacteristic->notify();
    }
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
