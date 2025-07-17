#include "BLEManager.h"
#include "SystemStatus.h"
#include "SystemCommand.h"
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
        
        // Send all current status to the newly connected client
        // Note: This is called from BLE task context, so it's safe to call sendAllCurrentStatus
        bleManager->sendAllCurrentStatus();
    }
    
    void onDisconnect(BLEServer* pServer) override {
        bleManager->deviceConnected = false;
        
        // Note: Removed automatic emergency stop on disconnect to allow seamless reconnection
        // Motor will continue running when web UI disconnects and reconnects
        // Emergency stop is only triggered by explicit user command
        
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
        
        if (value.length() > 0 && value.length() <= 256) {
            // Process command directly - SystemCommand handles thread-safe queuing
            // Commands are lightweight as they just queue data to SystemCommand
            bleManager->handleCommand(value);
        }
    }
};

BLEManager::BLEManager() 
    : Task("BLE_Task", 8192, 2, 0), // Task name, 8KB stack (reduced from 12KB), priority 2, core 0
      server(nullptr), service(nullptr), commandCharacteristic(nullptr),
      serverCallbacks(nullptr), commandCallbacks(nullptr),
      deviceConnected(false), oldDeviceConnected(false),
      systemStatus(SystemStatus::getInstance()), systemCommand(SystemCommand::getInstance()) {
    
    // No internal command queue needed - using SystemCommand singleton directly
}

BLEManager::~BLEManager() {
    // Clean up BLE callback objects
    if (serverCallbacks != nullptr) {
        delete serverCallbacks;
        serverCallbacks = nullptr;
    }
    
    if (commandCallbacks != nullptr) {
        delete commandCallbacks;
        commandCallbacks = nullptr;
    }
}

bool BLEManager::begin(const char* deviceName) {
    Serial.println("Initializing BLE...");
    
    // Initialize BLE
    BLEDevice::init(deviceName);
    
    // Create BLE Server
    server = BLEDevice::createServer();
    serverCallbacks = new ServerCallbacks(this);
    server->setCallbacks(serverCallbacks);
    
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
    commandCallbacks = new CommandCharacteristicCallbacks(this);
    commandCharacteristic->setCallbacks(commandCallbacks);
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

// setStepperController method removed - using SystemCommand singleton directly

void BLEManager::handleCommand(const std::string& command) {
    Serial.printf("Processing command: %s (length: %d)\n", command.c_str(), command.length());
    
    // Prevent buffer overflow attacks  
    if (command.length() > 256 || command.length() == 0) {
        Serial.printf("ERROR: Invalid command length: %d\n", command.length());
        return;
    }
    
    // Use fixed-size JSON document to prevent heap issues (compatible with ArduinoJson v6)
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
    
    // Validate that we have the required value field for most commands
    if (strcmp(type, "status_request") != 0 && doc["value"].isNull()) {
        Serial.println("ERROR: Command missing required 'value' field");
        return;
    }
    
    Serial.printf("Processing command type: %s\n", type);
    
    if (strcmp(type, "speed") == 0) {
        float speed = doc["value"];
        StepperCommandData cmd(StepperCommand::SET_SPEED, speed);
        systemCommand.sendCommand(cmd);
        Serial.printf("Speed command queued: %.2f RPM\n", speed);
    }
    else if (strcmp(type, "direction") == 0) {
        bool clockwise = doc["value"];
        StepperCommandData cmd(StepperCommand::SET_DIRECTION, clockwise);
        systemCommand.sendCommand(cmd);
        Serial.printf("Direction command queued: %s\n", clockwise ? "clockwise" : "counter-clockwise");
    }
    else if (strcmp(type, "enable") == 0) {
        bool enable = doc["value"];
        if (enable) {
            StepperCommandData cmd(StepperCommand::ENABLE);
            systemCommand.sendCommand(cmd);
        } else {
            StepperCommandData cmd(StepperCommand::DISABLE);
            systemCommand.sendCommand(cmd);
        }
        Serial.printf("Motor %s command queued\n", enable ? "enable" : "disable");
    }
    else if (strcmp(type, "current") == 0) {
        int current = doc["value"];
        if (current >= 10 && current <= 100) {
            StepperCommandData cmd(StepperCommand::SET_CURRENT, current);
            systemCommand.sendCommand(cmd);
            Serial.printf("Current command queued: %d%%\n", current);
        }
    }
    else if (strcmp(type, "reset") == 0) {
        StepperCommandData cmd(StepperCommand::RESET_COUNTERS);
        systemCommand.sendCommand(cmd);
        Serial.printf("Reset counters command queued\n");
    }
    else if (strcmp(type, "reset_stall") == 0) {
        StepperCommandData cmd(StepperCommand::RESET_STALL_COUNT);
        systemCommand.sendCommand(cmd);
        Serial.printf("Reset stall count command queued\n");
    }
    else if (strcmp(type, "status_request") == 0) {
        // Request all current status from StepperController
        Serial.println("Status request received, requesting all current status...");
        StepperCommandData cmd(StepperCommand::REQUEST_ALL_STATUS);
        systemCommand.sendCommand(cmd);
        PowerDeliveryCommandData pdCmd(PowerDeliveryCommand::REQUEST_ALL_STATUS);
        systemCommand.sendPowerDeliveryCommand(pdCmd);
    }
    else if (strcmp(type, "acceleration") == 0) {
        // Set acceleration directly in steps/s²
        uint32_t accelerationStepsPerSec2 = doc["value"];  // Acceleration in steps/s²
        
        if (accelerationStepsPerSec2 >= 100 && accelerationStepsPerSec2 <= 100000) {
            StepperCommandData cmd(StepperCommand::SET_ACCELERATION, (int)accelerationStepsPerSec2);
            systemCommand.sendCommand(cmd);
            Serial.printf("Acceleration command queued: %u steps/s²\n", accelerationStepsPerSec2);
        } else {
            Serial.println("Invalid acceleration parameters");
            sendNotification("error", "Acceleration must be 100-100000 steps/s²");
        }
    }
    else if (strcmp(type, "speed_variation_strength") == 0) {
        float strength = doc["value"];
        if (strength >= 0.0f && strength <= 1.0f) {
            StepperCommandData cmd(StepperCommand::SET_SPEED_VARIATION, strength);
            systemCommand.sendCommand(cmd);
            Serial.printf("Speed variation strength command queued: %.2f\n", strength);
        } else {
            Serial.println("Invalid speed variation strength");
            sendNotification("error", "Speed variation strength must be 0.0-1.0");
        }
    }
    else if (strcmp(type, "speed_variation_phase") == 0) {
        float phase = doc["value"];
        StepperCommandData cmd(StepperCommand::SET_SPEED_VARIATION_PHASE, phase);
        systemCommand.sendCommand(cmd);
        Serial.printf("Speed variation phase command queued: %.2f radians\n", phase);
    }
    else if (strcmp(type, "enable_speed_variation") == 0) {
        StepperCommandData cmd(StepperCommand::ENABLE_SPEED_VARIATION);
        systemCommand.sendCommand(cmd);
        Serial.printf("Enable speed variation command queued\n");
    }
    else if (strcmp(type, "disable_speed_variation") == 0) {
        StepperCommandData cmd(StepperCommand::DISABLE_SPEED_VARIATION);
        systemCommand.sendCommand(cmd);
        Serial.printf("Disable speed variation command queued\n");
    }
    else if (strcmp(type, "stallguard_threshold") == 0) {
        int threshold = doc["value"];
        if (threshold >= 0 && threshold <= 63) {
            StepperCommandData cmd(StepperCommand::SET_STALLGUARD_THRESHOLD, threshold);
            systemCommand.sendCommand(cmd);
            Serial.printf("StallGuard threshold command queued: %d\n", threshold);
        } else {
            Serial.println("Invalid StallGuard threshold");
            sendNotification("error", "StallGuard threshold must be 0-63");
        }
    }
    else if (strcmp(type, "pd_voltage") == 0) {
        // Set power delivery target voltage and start negotiation
        int voltage = doc["value"];
        if (voltage >= 5 && voltage <= 20) {
            PowerDeliveryCommandData cmd(PowerDeliveryCommand::SET_TARGET_VOLTAGE, voltage);
            systemCommand.sendPowerDeliveryCommand(cmd);
            
            Serial.printf("Power delivery voltage set to %dV and negotiation started\n", voltage);
        } else {
            Serial.printf("Invalid voltage value: %d (must be 5-20V)\n", voltage);
        }
    }
    else if (strcmp(type, "pd_auto_negotiate") == 0) {
        // Start auto-negotiation for highest available voltage
        PowerDeliveryCommandData cmd(PowerDeliveryCommand::AUTO_NEGOTIATE_HIGHEST);
        systemCommand.sendPowerDeliveryCommand(cmd);
        
        Serial.printf("Power delivery auto-negotiation started\n");
    }
    else {
        Serial.printf("Unknown command type: %s\n", type);
    }
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
    // Process notifications from StepperController (warnings and errors only)
    processNotifications();
    
    // Process status updates from StepperController (simple batching)
    processStatusUpdates();
    
    // Handle connection state changes
    if (!deviceConnected && oldDeviceConnected) {
        server->startAdvertising(); // Restart advertising
        Serial.println("BLE client disconnected - restarted advertising");
        oldDeviceConnected = deviceConnected;
    }
    
    if (deviceConnected && !oldDeviceConnected) {
        oldDeviceConnected = deviceConnected;
        Serial.println("BLE client connected");
    }
    
    // Small delay to prevent busy waiting when no commands are queued
    vTaskDelay(pdMS_TO_TICKS(10));
}

// Queue methods removed - using SystemCommand singleton directly

void BLEManager::processNotifications() {
    NotificationData notification;
    // Process all available notifications (warnings and errors only)
    while (systemStatus.getNotification(notification)) {
        String level;
        switch (notification.type) {
            case NotificationType::WARNING:
                level = "warning";
                break;
            case NotificationType::ERROR:
                level = "error";
                break;
            default:
                level = "unknown";
                break;
        }
        
        Serial.printf("Notification: %s", level.c_str());
        if (strlen(notification.message) > 0) {
            Serial.printf(" - %s", notification.message);
        }
        Serial.println();
        
        // Send notification to client
        sendNotification(level, String(notification.message));
    }
}

void BLEManager::processStatusUpdates() {
    if (!commandCharacteristic || !deviceConnected) return;
    
    StatusUpdateData statusUpdate;
    
    // Check if there are any status updates available
    if (systemStatus.getStatusUpdate(statusUpdate)) {
        // Create JSON document and add the first update
        JsonDocument statusDoc;
        statusDoc["type"] = "status_update";
        statusDoc["timestamp"] = millis();
        
        // Add the first status update
        addStatusToJson(statusDoc, statusUpdate);
        
        // Process all remaining status updates in the queue
        while (systemStatus.getStatusUpdate(statusUpdate)) {
            addStatusToJson(statusDoc, statusUpdate);
            
            // Check if we're approaching size limit
            String tempString;
            size_t tempSize = serializeJson(statusDoc, tempString);
            if (tempSize >= MAX_BLE_PACKET_SIZE) {
                Serial.printf("Warning: Status update approaching size limit (%d bytes), sending now\n", tempSize);
                break;
            }
        }
        
        // Send the batched updates
        sendStatusUpdate(statusDoc);
    }
}

void BLEManager::addStatusToJson(JsonDocument& doc, const StatusUpdateData& statusUpdate) {
    switch (statusUpdate.type) {
        case StatusUpdateType::SPEED_UPDATE:
            doc["currentSpeed"] = statusUpdate.floatValue;  // Actual speed for display
            break;
        case StatusUpdateType::SPEED_SETPOINT_CHANGED:
            doc["speed"] = statusUpdate.floatValue;         // Setpoint for UI controls
            break;
        case StatusUpdateType::DIRECTION_CHANGED:
            doc["direction"] = statusUpdate.boolValue ? "cw" : "ccw";
            break;
        case StatusUpdateType::ENABLED_CHANGED:
            doc["enabled"] = statusUpdate.boolValue;
            break;
        case StatusUpdateType::CURRENT_CHANGED:
            doc["current"] = statusUpdate.intValue;
            break;
        case StatusUpdateType::ACCELERATION_CHANGED:
            doc["acceleration"] = statusUpdate.uint32Value;
            break;
        case StatusUpdateType::SPEED_VARIATION_ENABLED_CHANGED:
            doc["speedVariationEnabled"] = statusUpdate.boolValue;
            break;
        case StatusUpdateType::SPEED_VARIATION_STRENGTH_CHANGED:
            doc["speedVariationStrength"] = statusUpdate.floatValue;
            break;
        case StatusUpdateType::SPEED_VARIATION_PHASE_CHANGED:
            doc["speedVariationPhase"] = statusUpdate.floatValue;
            break;
        case StatusUpdateType::TOTAL_REVOLUTIONS_UPDATE:
            doc["totalRevolutions"] = statusUpdate.floatValue;
            break;
        case StatusUpdateType::RUNTIME_UPDATE:
            doc["runtime"] = statusUpdate.ulongValue;
            break;
        case StatusUpdateType::STALL_DETECTED_UPDATE:
            doc["stallDetected"] = statusUpdate.boolValue;
            break;
        case StatusUpdateType::STALL_COUNT_UPDATE:
            doc["stallCount"] = statusUpdate.intValue;
            break;
        case StatusUpdateType::TMC2209_STATUS_UPDATE:
            doc["tmc2209Status"] = statusUpdate.boolValue;
            break;
        case StatusUpdateType::TMC2209_TEMPERATURE_UPDATE:
            doc["tmc2209Temperature"] = statusUpdate.intValue;
            break;
        case StatusUpdateType::STALLGUARD_THRESHOLD_CHANGED:
            doc["stallguardThreshold"] = statusUpdate.intValue;
            break;
        case StatusUpdateType::STALLGUARD_RESULT_UPDATE:
            doc["stallguardResult"] = statusUpdate.intValue;
            break;
        case StatusUpdateType::PD_NEGOTIATION_STATUS:
            doc["pdNegotiationStatus"] = statusUpdate.intValue;
            break;
        case StatusUpdateType::PD_NEGOTIATED_VOLTAGE:
            doc["pdNegotiatedVoltage"] = statusUpdate.floatValue;
            break;
        case StatusUpdateType::PD_CURRENT_VOLTAGE:
            doc["pdCurrentVoltage"] = statusUpdate.floatValue;
            break;
        case StatusUpdateType::PD_POWER_GOOD_STATUS:
            doc["pdPowerGood"] = statusUpdate.boolValue;
            break;
    }
}

void BLEManager::sendStatusUpdate(JsonDocument& statusDoc) {
    String statusString;
    size_t result = serializeJson(statusDoc, statusString);
    if (result == 0) {
        Serial.println("ERROR: Failed to serialize status JSON");
        return;
    }
    
    Serial.printf("Sending status update (%d bytes): %s\n", 
                  statusString.length(), statusString.c_str());
    
    try {
        commandCharacteristic->setValue(statusString.c_str());
        commandCharacteristic->notify();
        
        // Small delay to prevent overwhelming BLE stack
        vTaskDelay(pdMS_TO_TICKS(10));
    } catch (...) {
        Serial.println("Failed to send status update notification");
    }
}

void BLEManager::sendNotification(const String& level, const String& message) {
    if (!commandCharacteristic || !deviceConnected) return;
    
    // Use fixed-size JSON document to prevent heap corruption (compatible with ArduinoJson v6)
    JsonDocument doc;
    doc["type"] = "notification";
    doc["level"] = level;
    if (message.length() > 0 && message.length() < 128) { // Prevent buffer overflow
        doc["message"] = message;
    }
    
    String response;
    size_t result = serializeJson(doc, response);
    if (result == 0) {
        Serial.println("ERROR: Failed to serialize notification JSON");
        return;
    }
    
    // Ensure response is not too long for BLE characteristic
    if (response.length() > 512) {
        Serial.printf("WARNING: Notification too long (%d chars), truncating\n", response.length());
        response = response.substring(0, 512);
    }
    
    // Send via BLE notification
    try {
        commandCharacteristic->setValue(response.c_str());
        commandCharacteristic->notify();
        
        // Small delay to prevent overwhelming BLE stack
        vTaskDelay(pdMS_TO_TICKS(5));
    } catch (...) {
        Serial.println("ERROR: Failed to send notification");
    }
}

void BLEManager::sendAllCurrentStatus() {
    if (!deviceConnected) {
        return;
    }
    
    Serial.println("Requesting all current status from StepperController and PowerDeliveryTask...");
    
    // Use the thread-safe SystemCommand to request all status information
    StepperCommandData stepperCmd(StepperCommand::REQUEST_ALL_STATUS);
    systemCommand.sendCommand(stepperCmd);
    
    // Also request power delivery status
    PowerDeliveryCommandData pdCmd(PowerDeliveryCommand::REQUEST_ALL_STATUS);
    systemCommand.sendPowerDeliveryCommand(pdCmd);
    
    Serial.println("Status requests sent");
}
