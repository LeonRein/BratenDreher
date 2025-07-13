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

// Command data structure
struct StepperCommandData {
    StepperCommand command;
    union {
        float floatValue;    // for speed
        bool boolValue;      // for direction, enable/disable
        int intValue;        // for microsteps, current
    };
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
    
    // Helper methods
    bool initPreferences();
    void saveSettings();
    void loadSettings();
    void configureDriver();
    uint32_t rpmToStepsPerSecond(float rpm);
    
    // Internal methods (called from command processing)
    void setSpeedInternal(float rpm);
    void setDirectionInternal(bool clockwise);
    void enableInternal();
    void disableInternal();
    void emergencyStopInternal();
    void setMicroStepsInternal(int steps);
    void setRunCurrentInternal(int current);
    void resetCountersInternal();

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
    bool setSpeed(float rpm);
    bool setDirection(bool clockwise);
    bool enable();
    bool disable();
    bool emergencyStop();
    bool setMicroSteps(int steps);
    bool setRunCurrent(int current);
    bool resetCounters();
    
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
