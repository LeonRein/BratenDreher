#ifndef SYSTEM_COMMAND_H
#define SYSTEM_COMMAND_H

/**
 * @file SystemCommand.h
 * @brief Unified command manager for thread-safe communication between BLEManager and StepperController
 * 
 * This class provides thread-safe command management using FreeRTOS queues.
 * It handles all command forwarding and eliminates the need for separate command queues
 * in individual components.
 */

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "CommandTypes.h"

// Queue size configuration
#define COMMAND_QUEUE_SIZE          20     // Command queue size

class SystemCommand {
private:
    QueueHandle_t commandQueue;
    
    // Singleton implementation
    SystemCommand();
    ~SystemCommand();
    
    // Delete copy constructor and assignment operator
    SystemCommand(const SystemCommand&) = delete;
    SystemCommand& operator=(const SystemCommand&) = delete;
    
public:
    // Singleton access method
    static SystemCommand& getInstance();
    
    // Initialization
    bool begin();
    
    // Command management (thread-safe)
    bool sendCommand(const StepperCommandData& command, TickType_t timeout = pdMS_TO_TICKS(10));
    bool sendCommand(StepperCommand cmd, TickType_t timeout = pdMS_TO_TICKS(10));
    bool sendCommand(StepperCommand cmd, float value, TickType_t timeout = pdMS_TO_TICKS(10));
    bool sendCommand(StepperCommand cmd, bool value, TickType_t timeout = pdMS_TO_TICKS(10));
    bool sendCommand(StepperCommand cmd, int value, TickType_t timeout = pdMS_TO_TICKS(10));
    bool sendCommand(StepperCommand cmd, uint32_t value, TickType_t timeout = pdMS_TO_TICKS(10));
    
    // Emergency stop with no timeout (highest priority)
    bool emergencyStop();
    
    // Command retrieval (thread-safe)
    bool getCommand(StepperCommandData& command, TickType_t timeout = portMAX_DELAY);
    bool hasCommands() const;
    UBaseType_t getPendingCommandCount() const;
    void clearCommands();
};

#endif // SYSTEM_COMMAND_H
