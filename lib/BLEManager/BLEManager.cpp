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
        
        if (value.length() > 0 && value.length() <= BLEManager::MAX_COMMAND_LENGTH) {
            // Queue the command for processing in the BLE task instead of processing here
            // This avoids stack issues in the BLE callback context
            // No Serial prints in time-critical callback context
            bleManager->queueCommand(value);
        }
    }
};

BLEManager::BLEManager() 
    : Task("BLE_Task", 8192, 2, 0), // Task name, 8KB stack (reduced from 12KB), priority 2, core 0
      server(nullptr), service(nullptr), commandCharacteristic(nullptr),
      serverCallbacks(nullptr), commandCallbacks(nullptr),
      deviceConnected(false), oldDeviceConnected(false),
      stepperController(nullptr) {
    
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

void BLEManager::setStepperController(StepperController* controller) {
    stepperController = controller;
}

void BLEManager::handleCommand(const std::string& command) {
    if (!stepperController) return;
    
    Serial.printf("Processing command: %s (length: %d)\n", command.c_str(), command.length());
    
    // Prevent buffer overflow attacks
    if (command.length() > MAX_COMMAND_LENGTH || command.length() == 0) {
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
        uint32_t commandId = stepperController->setSpeed(speed);
        if (commandId > 0) {
            Serial.printf("Speed command queued: %.2f RPM (ID: %u)\n", speed, commandId);
        } else {
            Serial.println("Failed to queue speed command");
            sendCommandResult(0, "error", "Failed to queue speed command");
        }
    }
    else if (strcmp(type, "direction") == 0) {
        bool clockwise = doc["value"];
        uint32_t commandId = stepperController->setDirection(clockwise);
        if (commandId > 0) {
            Serial.printf("Direction command queued: %s (ID: %u)\n", clockwise ? "clockwise" : "counter-clockwise", commandId);
        } else {
            Serial.println("Failed to queue direction command");
            sendCommandResult(0, "error", "Failed to queue direction command");
        }
    }
    else if (strcmp(type, "enable") == 0) {
        bool enable = doc["value"];
        uint32_t commandId = 0;
        if (enable) {
            commandId = stepperController->enable();
        } else {
            commandId = stepperController->disable();
        }
        if (commandId > 0) {
            Serial.printf("Motor %s command queued (ID: %u)\n", enable ? "enable" : "disable", commandId);
        } else {
            Serial.printf("Failed to queue motor %s command\n", enable ? "enable" : "disable");
            sendCommandResult(0, "error", String("Failed to queue motor ") + (enable ? "enable" : "disable") + " command");
        }
    }
    else if (strcmp(type, "current") == 0) {
        int current = doc["value"];
        if (current >= 10 && current <= 100) {
            uint32_t commandId = stepperController->setRunCurrent(current);
            if (commandId > 0) {
                Serial.printf("Current command queued: %d%% (ID: %u)\n", current, commandId);
            } else {
                Serial.println("Failed to queue current command");
                sendCommandResult(0, "error", "Failed to queue current command");
            }
        }
    }
    else if (strcmp(type, "reset") == 0) {
        uint32_t commandId = stepperController->resetCounters();
        if (commandId > 0) {
            Serial.printf("Reset counters command queued (ID: %u)\n", commandId);
        } else {
            Serial.println("Failed to queue reset counters command");
            sendCommandResult(0, "error", "Failed to queue reset counters command");
        }
    }
    else if (strcmp(type, "reset_stall") == 0) {
        uint32_t commandId = stepperController->resetStallCount();
        if (commandId > 0) {
            Serial.printf("Reset stall count command queued (ID: %u)\n", commandId);
        } else {
            Serial.println("Failed to queue reset stall count command");
            sendCommandResult(0, "error", "Failed to queue reset stall count command");
        }
    }
    else if (strcmp(type, "status_request") == 0) {
        // Status requests are no longer needed - real-time updates are provided automatically
        Serial.println("Info: Status request received, but real-time updates are already active");
    }
    else if (strcmp(type, "acceleration") == 0) {
        // Set acceleration directly in steps/s²
        uint32_t accelerationStepsPerSec2 = doc["value"];  // Acceleration in steps/s²
        
        if (accelerationStepsPerSec2 >= 100 && accelerationStepsPerSec2 <= 100000) {
            uint32_t commandId = stepperController->setAcceleration(accelerationStepsPerSec2);
            Serial.printf("Acceleration set to %u steps/s²\n", accelerationStepsPerSec2);
            
            // Send success response
            sendCommandResult(commandId, "success", String("Acceleration set to ") + accelerationStepsPerSec2 + " steps/s²");
        } else {
            Serial.println("Invalid acceleration parameters");
            sendCommandResult(0, "invalid_parameter", "Acceleration must be 100-100000 steps/s²");
        }
    }
    else if (strcmp(type, "speed_variation_strength") == 0) {
        float strength = doc["value"];
        if (strength >= 0.0f && strength <= 1.0f) {
            uint32_t commandId = stepperController->setSpeedVariation(strength);
            if (commandId > 0) {
                Serial.printf("Speed variation strength command queued: %.2f (ID: %u)\n", strength, commandId);
            } else {
                Serial.println("Failed to queue speed variation strength command");
                sendCommandResult(0, "error", "Failed to queue speed variation strength command");
            }
        } else {
            Serial.println("Invalid speed variation strength");
            sendCommandResult(0, "invalid_parameter", "Speed variation strength must be 0.0-1.0");
        }
    }
    else if (strcmp(type, "speed_variation_phase") == 0) {
        float phase = doc["value"];
        uint32_t commandId = stepperController->setSpeedVariationPhase(phase);
        if (commandId > 0) {
            Serial.printf("Speed variation phase command queued: %.2f radians (ID: %u)\n", phase, commandId);
        } else {
            Serial.println("Failed to queue speed variation phase command");
            sendCommandResult(0, "error", "Failed to queue speed variation phase command");
        }
    }
    else if (strcmp(type, "enable_speed_variation") == 0) {
        uint32_t commandId = stepperController->enableSpeedVariation();
        if (commandId > 0) {
            Serial.printf("Enable speed variation command queued (ID: %u)\n", commandId);
        } else {
            Serial.println("Failed to queue enable speed variation command");
            sendCommandResult(0, "error", "Failed to queue enable speed variation command");
        }
    }
    else if (strcmp(type, "disable_speed_variation") == 0) {
        uint32_t commandId = stepperController->disableSpeedVariation();
        if (commandId > 0) {
            Serial.printf("Disable speed variation command queued (ID: %u)\n", commandId);
        } else {
            Serial.println("Failed to queue disable speed variation command");
            sendCommandResult(0, "error", "Failed to queue disable speed variation command");
        }
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
    // Process any queued commands with timeout (non-blocking)
    processQueuedCommands();
    
    // Process command results from StepperController
    processCommandResults();
    
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
    // Limit processing to prevent overwhelming the system
    int commandsProcessed = 0;
    const int maxCommandsPerCycle = 5;
    
    while (commandsProcessed < maxCommandsPerCycle && 
           xQueueReceive(commandQueue, commandBuffer, pdMS_TO_TICKS(1)) == pdTRUE) {
        std::string command(commandBuffer);
        Serial.printf("Processing queued command: %s\n", command.c_str());
        handleCommand(command);
        commandsProcessed++;
        
        // Small delay between commands to prevent system overload
        if (commandsProcessed < maxCommandsPerCycle) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
    
    // If queue is getting full, warn about potential command loss
    if (uxQueueMessagesWaiting(commandQueue) > (MAX_QUEUE_SIZE * 0.8)) {
        Serial.printf("WARNING: Command queue nearly full (%d/%d)\n", 
                     uxQueueMessagesWaiting(commandQueue), MAX_QUEUE_SIZE);
    }
}

void BLEManager::processCommandResults() {
    if (!stepperController) return;
    
    CommandResultData result;
    // Process all available command results
    while (stepperController->getCommandResult(result)) {
        String status;
        switch (result.result) {
            case CommandResult::SUCCESS:
                status = "success";
                break;
            case CommandResult::HARDWARE_ERROR:
                status = "hardware_error";
                break;
            case CommandResult::INVALID_PARAMETER:
                status = "invalid_parameter";
                break;
            case CommandResult::DRIVER_NOT_RESPONDING:
                status = "driver_not_responding";
                break;
            case CommandResult::COMMUNICATION_ERROR:
                status = "communication_error";
                break;
            default:
                status = "unknown_error";
                break;
        }
        
        Serial.printf("Command %u result: %s", result.commandId, status.c_str());
        if (strlen(result.errorMessage) > 0) {
            Serial.printf(" - %s", result.errorMessage);
        }
        Serial.println();
        
        // Send result to client
        sendCommandResult(result.commandId, status, String(result.errorMessage));
    }
}

void BLEManager::processStatusUpdates() {
    if (!stepperController || !commandCharacteristic || !deviceConnected) return;
    
    StatusUpdateData statusUpdate;
    
    // Check if there are any status updates available
    if (stepperController->getStatusUpdate(statusUpdate)) {
        // Create JSON document and add the first update
        JsonDocument statusDoc;
        statusDoc["type"] = "status_update";
        statusDoc["timestamp"] = millis();
        
        // Add the first status update
        addStatusToJson(statusDoc, statusUpdate);
        
        // Process all remaining status updates in the queue
        while (stepperController->getStatusUpdate(statusUpdate)) {
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
        case StatusUpdateType::SPEED_CHANGED:
            doc["speed"] = statusUpdate.floatValue;
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
        case StatusUpdateType::IS_RUNNING_UPDATE:
            doc["running"] = statusUpdate.boolValue;
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
        case StatusUpdateType::CURRENT_VARIABLE_SPEED_UPDATE:
            doc["currentVariableSpeed"] = statusUpdate.floatValue;
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

void BLEManager::sendCommandResult(uint32_t commandId, const String& status, const String& message) {
    if (!commandCharacteristic || !deviceConnected) return;
    
    // Use fixed-size JSON document to prevent heap corruption (compatible with ArduinoJson v6)
    JsonDocument doc;
    doc["type"] = "command_result";
    doc["command_id"] = commandId;
    doc["status"] = status;
    if (message.length() > 0 && message.length() < 128) { // Prevent buffer overflow
        doc["message"] = message;
    }
    
    String response;
    size_t result = serializeJson(doc, response);
    if (result == 0) {
        Serial.println("ERROR: Failed to serialize command result JSON");
        return;
    }
    
    // Ensure response is not too long for BLE characteristic
    if (response.length() > 512) {
        Serial.printf("WARNING: Command result too long (%d chars), truncating\n", response.length());
        response = response.substring(0, 512);
    }
    
    // Send via BLE notification
    try {
        commandCharacteristic->setValue(response.c_str());
        commandCharacteristic->notify();
        
        // Small delay to prevent overwhelming BLE stack
        vTaskDelay(pdMS_TO_TICKS(5));
    } catch (...) {
        Serial.println("ERROR: Failed to send command result notification");
    }
}

// ...existing code...
