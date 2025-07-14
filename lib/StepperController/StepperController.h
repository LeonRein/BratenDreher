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
    SET_CURRENT,
    SET_ACCELERATION,
    RESET_COUNTERS,
    RESET_STALL_COUNT,
    SET_SPEED_VARIATION,
    SET_SPEED_VARIATION_PHASE,
    ENABLE_SPEED_VARIATION,
    DISABLE_SPEED_VARIATION,
    REQUEST_ALL_STATUS  // Request all current status values
};

// Command result status
enum class CommandResult {
    SUCCESS,
    HARDWARE_ERROR,
    INVALID_PARAMETER,
    DRIVER_NOT_RESPONDING,
    COMMUNICATION_ERROR
};

// Status update types for inter-task communication
enum class StatusUpdateType {
    SPEED_CHANGED,
    DIRECTION_CHANGED,
    ENABLED_CHANGED,
    CURRENT_CHANGED,
    ACCELERATION_CHANGED,
    SPEED_VARIATION_ENABLED_CHANGED,
    SPEED_VARIATION_STRENGTH_CHANGED,
    SPEED_VARIATION_PHASE_CHANGED,
    // Periodic updates - now separate for each value
    TOTAL_REVOLUTIONS_UPDATE,
    RUNTIME_UPDATE,
    IS_RUNNING_UPDATE,
    STALL_DETECTED_UPDATE,
    STALL_COUNT_UPDATE,
    TMC2209_STATUS_UPDATE,
    CURRENT_VARIABLE_SPEED_UPDATE
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
    char errorMessage[128];  // fixed-size buffer to prevent heap fragmentation
    
    // Helper constructor to safely set error message
    CommandResultData() : commandId(0), result(CommandResult::SUCCESS) {
        errorMessage[0] = '\0';
    }
    
    void setErrorMessage(const char* msg) {
        if (msg) {
            strncpy(errorMessage, msg, sizeof(errorMessage) - 1);
            errorMessage[sizeof(errorMessage) - 1] = '\0';
        } else {
            errorMessage[0] = '\0';
        }
    }
};

// Status update structure
struct StatusUpdateData {
    StatusUpdateType type;
    union {
        float floatValue;    // for speed, acceleration, revolutions, etc.
        bool boolValue;      // for direction, enabled, etc.
        int intValue;        // for current, counts
        uint32_t uint32Value; // for acceleration
        unsigned long ulongValue; // for runtime, timestamps
    };
    uint32_t timestamp;      // millis() when status was updated
    
    // Helper constructors
    StatusUpdateData() : type(StatusUpdateType::SPEED_CHANGED), timestamp(0) {
        floatValue = 0.0f;
    }
    
    StatusUpdateData(StatusUpdateType t, float value) : type(t), timestamp(millis()) {
        floatValue = value;
    }
    
    StatusUpdateData(StatusUpdateType t, bool value) : type(t), timestamp(millis()) {
        boolValue = value;
    }
    
    StatusUpdateData(StatusUpdateType t, int value) : type(t), timestamp(millis()) {
        intValue = value;
    }
    
    StatusUpdateData(StatusUpdateType t, uint32_t value) : type(t), timestamp(millis()) {
        uint32Value = value;
    }
    
    StatusUpdateData(StatusUpdateType t, unsigned long value) : type(t), timestamp(millis()) {
        ulongValue = value;
    }
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
    static const unsigned long STATUS_UPDATE_INTERVAL = 500;        // Status update every 500ms
    static const unsigned long MOTOR_SPEED_UPDATE_INTERVAL = 50;    // Speed update every 50ms for smooth variation
    
    // Speed settings (in RPM) - max 30 RPM for the final output (0.5 RPS)
    float currentSpeedRPM;
    float minSpeedRPM;
    float maxSpeedRPM;
    int microSteps;          // Fixed at 16 - no longer user configurable
    int runCurrent;
    
    // Acceleration tracking
    uint32_t currentAcceleration;    // Current acceleration in steps/s²
    
    // Speed variation settings
    bool speedVariationEnabled;
    float speedVariationStrength;    // 0.0 to 1.0 (0% to 100% variation)
    float speedVariationPhase;       // Phase offset in radians (0 to 2*PI)
    int32_t speedVariationStartPosition; // Position where variation was enabled
    float speedVariationK;           // Internal k parameter (derived from strength)
    float speedVariationK0;          // Compensation factor k0 = sqrt(1 - k²)
    
    // State tracking
    bool motorEnabled;
    bool clockwise;
    unsigned long startTime;
    unsigned long totalSteps;
    bool isFirstStart;
    bool tmc2209Initialized;  // Track TMC2209 driver initialization status
    
    // Stall detection
    bool stallDetected;
    unsigned long lastStallTime;
    uint16_t stallCount;
    
    // Command queue for thread-safe operation
    QueueHandle_t commandQueue;
    static const size_t COMMAND_QUEUE_SIZE = 20;
    
    // Result queue for command status reporting
    QueueHandle_t resultQueue;
    static const size_t RESULT_QUEUE_SIZE = 20;
    uint32_t nextCommandId;
    
    // Status update queue for thread-safe status communication
    QueueHandle_t statusUpdateQueue;
    static const size_t STATUS_UPDATE_QUEUE_SIZE = 30;
    
    // Helper methods
    bool initPreferences();
    void saveSettings();
    void loadSettings();
    void configureDriver();
    void configureStallDetection(bool enableStealthChop = true);
    uint32_t rpmToStepsPerSecond(float rpm) const;
    
    // Internal methods (called from command processing)
    void setSpeedInternal(float rpm, uint32_t commandId);
    void setDirectionInternal(bool clockwise, uint32_t commandId);
    void enableInternal(uint32_t commandId);
    void disableInternal(uint32_t commandId);
    void emergencyStopInternal(uint32_t commandId);
    void setRunCurrentInternal(int current, uint32_t commandId);
    void setAccelerationInternal(uint32_t accelerationStepsPerSec2, uint32_t commandId);
    void resetCountersInternal(uint32_t commandId);
    void resetStallCountInternal(uint32_t commandId);
    void setSpeedVariationInternal(float strength, uint32_t commandId);
    void setSpeedVariationPhaseInternal(float phase, uint32_t commandId);
    void enableSpeedVariationInternal(uint32_t commandId);
    void disableSpeedVariationInternal(uint32_t commandId);
    void requestAllStatusInternal(uint32_t commandId);
    
    // Speed variation helper methods
    float calculateVariableSpeed() const;
    float getPositionAngle() const;
    uint32_t calculateRequiredAccelerationForVariableSpeed() const;
    void updateAccelerationForVariableSpeed();
    void updateSpeedVariationParameters(); // Helper to calculate k and k0
    
    // Helper methods for status reporting
    void reportResult(uint32_t commandId, CommandResult result, const String& errorMessage = "");
    void publishStatusUpdate(StatusUpdateType type, float value);
    void publishStatusUpdate(StatusUpdateType type, bool value);
    void publishStatusUpdate(StatusUpdateType type, int value);
    void publishStatusUpdate(StatusUpdateType type, uint32_t value);
    void publishStatusUpdate(StatusUpdateType type, unsigned long value);
    
    // Speed variation control
    void updateMotorSpeed();

protected:
    // Task implementation
    void run() override;
    
    // Internal methods
    void processCommand(const StepperCommandData& cmd);
    
    // Helper methods for run() timing
    TickType_t calculateQueueTimeout(unsigned long nextUpdate);
    bool isUpdateDue(unsigned long nextUpdate);
    
    // Periodic status updates
    void publishPeriodicStatusUpdates();
    
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
    uint32_t setRunCurrent(int current);
    uint32_t setAcceleration(uint32_t accelerationStepsPerSec2);
    uint32_t resetCounters();
    uint32_t resetStallCount();
    
    // Speed variation control (thread-safe via command queue)
    uint32_t setSpeedVariation(float strength);           // Set variation strength (0.0 to 1.0)
    uint32_t setSpeedVariationPhase(float phase);         // Set phase offset (0.0 to 2*PI)
    uint32_t enableSpeedVariation();                      // Enable variable speed (current position becomes slowest)
    uint32_t disableSpeedVariation();                     // Disable variable speed
    
    // Status request (thread-safe via command queue)
    uint32_t requestAllStatus();                           // Request all current status to be published
    
    // Command result retrieval (thread-safe)
    bool getCommandResult(CommandResultData& result); // non-blocking, returns false if no result available
    
    // Status update retrieval (thread-safe)
    bool getStatusUpdate(StatusUpdateData& status); // non-blocking, returns false if no update available
    
    // Stall detection configuration
    void setStallGuardThreshold(uint8_t threshold); // 0 = most sensitive, 255 = least sensitive
};

#endif // STEPPER_CONTROLLER_H
