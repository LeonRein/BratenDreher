#ifndef STEPPER_CONTROLLER_H
#define STEPPER_CONTROLLER_H

#include <Arduino.h>
#include "FastAccelStepper.h"
#include <TMC2209.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "Task.h"

// Command types for inter-task communication
enum class StepperCommand {
    SET_SPEED,
    SET_DIRECTION,
    ENABLE,
    DISABLE,
    EMERGENCY_STOP,
    SET_MICROSTEPS,
    SET_CURRENT,
    RESET_COUNTERS
};

// Command result status
enum class CommandResult {
    SUCCESS,
    HARDWARE_ERROR,
    INVALID_PARAMETER,
    DRIVER_NOT_RESPONDING,
    COMMUNICATION_ERROR
};

// Command data structure
struct StepperCommandData {
    StepperCommand command;
    union {
        float floatValue;    // for speed
        bool boolValue;      // for direction, enable/disable
        int intValue;        // for microsteps, current
    };
    uint32_t commandId;      // unique ID for tracking command results
};

// Command result structure
struct CommandResultData {
    uint32_t commandId;
    CommandResult result;
    String errorMessage;     // detailed error description
};

class StepperController : public Task {
private:
    // FastAccelStepper engine and stepper
    FastAccelStepperEngine engine;
    FastAccelStepper* stepper;
    
    // TMC2209 stepper driver
    TMC2209 stepperDriver;
    HardwareSerial& serialStream;
    Preferences preferences;
    
    // Hardware pins (from PD-Stepper example)
    static const uint8_t TMC_EN = 21;
    static const uint8_t STEP_PIN = 5;
    static const uint8_t DIR_PIN = 6;
    static const uint8_t MS1 = 1;
    static const uint8_t MS2 = 2;
    static const uint8_t TMC_TX = 17;
    static const uint8_t TMC_RX = 18;
    static const uint8_t DIAG = 16;
    
    // Motor specifications
    static const int STEPS_PER_REVOLUTION = 200;  // NEMA 17
    static const int GEAR_RATIO = 10;             // 1:10 reduction
    static const int TOTAL_STEPS_PER_REVOLUTION = STEPS_PER_REVOLUTION * GEAR_RATIO;
    
    // Timing configuration
    static const unsigned long CACHE_UPDATE_INTERVAL = 500;
    
    // Speed settings (in RPM) - max 30 RPM for the final output (0.5 RPS)
    float currentSpeedRPM;
    float minSpeedRPM;
    float maxSpeedRPM;
    int microSteps;
    int runCurrent;
    
    // State tracking
    bool motorEnabled;
    bool clockwise;
    unsigned long startTime;
    unsigned long totalSteps;
    bool isFirstStart;
    
    // Cached values for thread-safe reading
    mutable int32_t cachedCurrentPosition;
    mutable bool cachedIsRunning;
    
    // Command queue for thread-safe operation
    QueueHandle_t commandQueue;
    static const size_t COMMAND_QUEUE_SIZE = 20;
    
    // Result queue for command status reporting
    QueueHandle_t resultQueue;
    static const size_t RESULT_QUEUE_SIZE = 20;
    uint32_t nextCommandId;
    
    // Helper methods
    bool initPreferences();
    void saveSettings();
    void loadSettings();
    void configureDriver();
    uint32_t rpmToStepsPerSecond(float rpm);
    
    // Internal methods (called from command processing)
    void setSpeedInternal(float rpm, uint32_t commandId);
    void setDirectionInternal(bool clockwise, uint32_t commandId);
    void enableInternal(uint32_t commandId);
    void disableInternal(uint32_t commandId);
    void emergencyStopInternal(uint32_t commandId);
    void setMicroStepsInternal(int steps, uint32_t commandId);
    void setRunCurrentInternal(int current, uint32_t commandId);
    void resetCountersInternal(uint32_t commandId);
    
    // Helper methods for status reporting
    void reportResult(uint32_t commandId, CommandResult result, const String& errorMessage = "");
    bool isDriverResponding();

protected:
    // Task implementation
    void run() override;
    
    // Internal methods
    void processCommand(const StepperCommandData& cmd);
    void updateCache();
    
    // Helper methods for run() timing
    TickType_t calculateQueueTimeout(unsigned long nextCacheUpdate);
    bool isCacheUpdateDue(unsigned long nextCacheUpdate);
    
public:
    StepperController();
    ~StepperController();
    
    // Initialization
    bool begin();
    
    // Motor control (thread-safe via command queue)
    uint32_t setSpeed(float rpm);
    uint32_t setDirection(bool clockwise);
    uint32_t enable();
    uint32_t disable();
    uint32_t emergencyStop();
    uint32_t setMicroSteps(int steps);
    uint32_t setRunCurrent(int current);
    uint32_t resetCounters();
    
    // Command result retrieval (thread-safe)
    bool getCommandResult(CommandResultData& result); // non-blocking, returns false if no result available
    
    // Getters
    float getSpeed() const { return currentSpeedRPM; }
    bool isEnabled() const { return motorEnabled; }
    bool isClockwise() const { return clockwise; }
    float getMinSpeed() const { return minSpeedRPM; }
    float getMaxSpeed() const { return maxSpeedRPM; }
    
    // Statistics (thread-safe - read-only)
    float getTotalRevolutions() const;
    unsigned long getRunTime() const; // in seconds
    bool isRunning() const;
    
    // Settings (thread-safe - read-only)
    int getMicroSteps() const { return microSteps; }
    int getRunCurrent() const { return runCurrent; }
};

#endif // STEPPER_CONTROLLER_H
