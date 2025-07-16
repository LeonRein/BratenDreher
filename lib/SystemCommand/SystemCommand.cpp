#include "SystemCommand.h"

// Singleton implementation
SystemCommand& SystemCommand::getInstance() {
    static SystemCommand instance;
    return instance;
}

SystemCommand::SystemCommand() : commandQueue(nullptr), pdCommandQueue(nullptr) {
}

SystemCommand::~SystemCommand() {
    if (commandQueue != nullptr) {
        vQueueDelete(commandQueue);
        commandQueue = nullptr;
    }
    if (pdCommandQueue != nullptr) {
        vQueueDelete(pdCommandQueue);
        pdCommandQueue = nullptr;
    }
}

bool SystemCommand::begin() {
    // Create command queue
    commandQueue = xQueueCreate(COMMAND_QUEUE_SIZE, sizeof(StepperCommandData));
    if (commandQueue == nullptr) {
        Serial.println("ERROR: Failed to create command queue");
        return false;
    }
    
    // Create power delivery command queue
    pdCommandQueue = xQueueCreate(PD_COMMAND_QUEUE_SIZE, sizeof(PowerDeliveryCommandData));
    if (pdCommandQueue == nullptr) {
        Serial.println("ERROR: Failed to create power delivery command queue");
        vQueueDelete(commandQueue);
        commandQueue = nullptr;
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

// Power delivery command management methods
bool SystemCommand::sendPowerDeliveryCommand(const PowerDeliveryCommandData& command, TickType_t timeout) {
    if (pdCommandQueue == nullptr) {
        Serial.println("ERROR: SystemCommand PD queue not initialized!");
        return false;
    }
    
    Serial.printf("SystemCommand: Sending PD command type %d\n", (int)command.command);
    
    BaseType_t result = xQueueSend(pdCommandQueue, &command, timeout);
    
    if (result == pdTRUE) {
        Serial.printf("SystemCommand: PD Command queued successfully. Queue depth: %d\n", 
                     uxQueueMessagesWaiting(pdCommandQueue));
    } else {
        Serial.println("ERROR: SystemCommand failed to queue PD command!");
    }
    
    return result == pdTRUE;
}

bool SystemCommand::sendPowerDeliveryCommand(PowerDeliveryCommand cmd, TickType_t timeout) {
    PowerDeliveryCommandData command(cmd);
    return sendPowerDeliveryCommand(command, timeout);
}

bool SystemCommand::sendPowerDeliveryCommand(PowerDeliveryCommand cmd, float value, TickType_t timeout) {
    PowerDeliveryCommandData command(cmd, value);
    return sendPowerDeliveryCommand(command, timeout);
}

bool SystemCommand::sendPowerDeliveryCommand(PowerDeliveryCommand cmd, bool value, TickType_t timeout) {
    PowerDeliveryCommandData command(cmd, value);
    return sendPowerDeliveryCommand(command, timeout);
}

bool SystemCommand::sendPowerDeliveryCommand(PowerDeliveryCommand cmd, int value, TickType_t timeout) {
    PowerDeliveryCommandData command(cmd, value);
    return sendPowerDeliveryCommand(command, timeout);
}

bool SystemCommand::getPowerDeliveryCommand(PowerDeliveryCommandData& command, TickType_t timeout) {
    if (pdCommandQueue == nullptr) {
        Serial.println("ERROR: SystemCommand PD queue not initialized!");
        return false;
    }
    
    BaseType_t result = xQueueReceive(pdCommandQueue, &command, timeout);
    
    if (result == pdTRUE) {
        Serial.printf("SystemCommand: Retrieved PD command type %d. Remaining queue depth: %d\n", 
                     (int)command.command, uxQueueMessagesWaiting(pdCommandQueue));
    }
    
    return result == pdTRUE;
}

bool SystemCommand::hasPowerDeliveryCommands() const {
    if (pdCommandQueue == nullptr) return false;
    
    return uxQueueMessagesWaiting(pdCommandQueue) > 0;
}

UBaseType_t SystemCommand::getPendingPowerDeliveryCommandCount() const {
    if (pdCommandQueue == nullptr) return 0;
    
    return uxQueueMessagesWaiting(pdCommandQueue);
}

void SystemCommand::clearPowerDeliveryCommands() {
    if (pdCommandQueue == nullptr) return;
    
    xQueueReset(pdCommandQueue);
}
