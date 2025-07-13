#ifndef TASK_H
#define TASK_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class Task {
protected:
    TaskHandle_t taskHandle;
    const char* taskName;
    uint32_t stackSize;
    UBaseType_t priority;
    BaseType_t coreId;
    bool isRunning;
    
    // Static wrapper function for FreeRTOS
    static void taskWrapper(void* parameter) {
        Task* taskInstance = static_cast<Task*>(parameter);
        taskInstance->run();
        vTaskDelete(NULL);
    }
    
    // Pure virtual function to be implemented by subclasses
    virtual void run() = 0;
    
public:
    Task(const char* name, uint32_t stackSizeBytes, UBaseType_t taskPriority, BaseType_t core = tskNO_AFFINITY)
        : taskHandle(NULL), taskName(name), stackSize(stackSizeBytes), 
          priority(taskPriority), coreId(core), isRunning(false) {}
    
    virtual ~Task() {
        stop();
    }
    
    // Start the task
    bool start() {
        if (isRunning) {
            return false; // Already running
        }
        
        BaseType_t result;
        if (coreId == tskNO_AFFINITY) {
            result = xTaskCreate(taskWrapper, taskName, stackSize, this, priority, &taskHandle);
        } else {
            result = xTaskCreatePinnedToCore(taskWrapper, taskName, stackSize, this, priority, &taskHandle, coreId);
        }
        
        if (result == pdPASS) {
            isRunning = true;
            Serial.printf("Task '%s' started successfully\n", taskName);
            return true;
        } else {
            Serial.printf("Failed to create task '%s'\n", taskName);
            return false;
        }
    }
    
    // Stop the task
    void stop() {
        if (isRunning && taskHandle != NULL) {
            vTaskDelete(taskHandle);
            taskHandle = NULL;
            isRunning = false;
            Serial.printf("Task '%s' stopped\n", taskName);
        }
    }
    
    // Check if task is running
    bool getIsRunning() const { return isRunning; }
    
    // Get task handle
    TaskHandle_t getHandle() const { return taskHandle; }
    
    // Get task name
    const char* getName() const { return taskName; }
};

#endif // TASK_H
