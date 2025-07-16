#include "SystemCommand.h"

// Singleton implementation
SystemCommand& SystemCommand::getInstance() {
    static SystemCommand instance;
    return instance;
}

SystemCommand::SystemCommand() : commandQueue(nullptr) {
}

SystemCommand::~SystemCommand() {
    if (commandQueue != nullptr) {
        vQueueDelete(commandQueue);
        commandQueue = nullptr;
    }
}

bool SystemCommand::begin() {
    // Create command queue
    commandQueue = xQueueCreate(COMMAND_QUEUE_SIZE, sizeof(StepperCommandData));
    if (commandQueue == nullptr) {
        Serial.println("ERROR: Failed to create command queue");
        return false;
    }
    
    return true;
}

// Command management methods
bool SystemCommand::sendCommand(const StepperCommandData& command, TickType_t timeout) {
    if (commandQueue == nullptr) {
        Serial.println("ERROR: SystemCommand queue not initialized!");
        return false;
    }
    
    Serial.printf("SystemCommand: Sending command type %d\n", (int)command.command);
    
    BaseType_t result = xQueueSend(commandQueue, &command, timeout);
    
    if (result == pdTRUE) {
        Serial.printf("SystemCommand: Command queued successfully. Queue depth: %d\n", 
                     uxQueueMessagesWaiting(commandQueue));
    } else {
        Serial.println("ERROR: SystemCommand failed to queue command!");
    }
    
    return result == pdTRUE;
}

bool SystemCommand::sendCommand(StepperCommand cmd, TickType_t timeout) {
    StepperCommandData commandData(cmd);
    return sendCommand(commandData, timeout);
}

bool SystemCommand::sendCommand(StepperCommand cmd, float value, TickType_t timeout) {
    StepperCommandData commandData(cmd, value);
    return sendCommand(commandData, timeout);
}

bool SystemCommand::sendCommand(StepperCommand cmd, bool value, TickType_t timeout) {
    StepperCommandData commandData(cmd, value);
    return sendCommand(commandData, timeout);
}

bool SystemCommand::sendCommand(StepperCommand cmd, int value, TickType_t timeout) {
    StepperCommandData commandData(cmd, value);
    return sendCommand(commandData, timeout);
}

bool SystemCommand::sendCommand(StepperCommand cmd, uint32_t value, TickType_t timeout) {
    StepperCommandData commandData(cmd, value);
    return sendCommand(commandData, timeout);
}

bool SystemCommand::emergencyStop() {
    if (commandQueue == nullptr) return false;
    
    StepperCommandData emergencyCmd(StepperCommand::EMERGENCY_STOP);
    // Emergency stop has no timeout - must be processed immediately
    return xQueueSend(commandQueue, &emergencyCmd, 0) == pdTRUE;
}

bool SystemCommand::getCommand(StepperCommandData& command, TickType_t timeout) {
    if (commandQueue == nullptr) {
        Serial.println("ERROR: SystemCommand queue not initialized for getCommand!");
        return false;
    }
    
    BaseType_t result = xQueueReceive(commandQueue, &command, timeout);
    
    if (result == pdTRUE) {
        Serial.printf("SystemCommand: Retrieved command type %d. Remaining queue depth: %d\n", 
                     (int)command.command, uxQueueMessagesWaiting(commandQueue));
    }
    
    return result == pdTRUE;
}

bool SystemCommand::hasCommands() const {
    if (commandQueue == nullptr) return false;
    
    return uxQueueMessagesWaiting(commandQueue) > 0;
}

UBaseType_t SystemCommand::getPendingCommandCount() const {
    if (commandQueue == nullptr) return 0;
    
    return uxQueueMessagesWaiting(commandQueue);
}

void SystemCommand::clearCommands() {
    if (commandQueue == nullptr) return;
    
    xQueueReset(commandQueue);
}
