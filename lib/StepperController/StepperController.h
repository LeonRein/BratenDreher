#ifndef STEPPER_CONTROLLER_H
#define STEPPER_CONTROLLER_H

#include <Arduino.h>
#include "FastAccelStepper.h"
#include <TMC2209.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "Task.h"

// Hardware pin definitions (from PD-Stepper example)
#define TMC_EN_PIN          21
#define STEP_PIN            5
#define DIR_PIN             6
#define MS1_PIN             1
#define MS2_PIN             2
#define TMC_TX_PIN          17
#define TMC_RX_PIN          18
#define DIAG_PIN            16

// Motor specifications
#define STEPS_PER_REVOLUTION        200    // NEMA 17
#define GEAR_RATIO                  10     // 1:10 reduction
#define TOTAL_STEPS_PER_REVOLUTION  (STEPS_PER_REVOLUTION * GEAR_RATIO)
#define MICRO_STEPS                 16     // Fixed at 16 microsteps

// Speed settings (in RPM)
#define MIN_SPEED_RPM               0.1f   // Minimum speed
#define MAX_SPEED_RPM               30.0f  // Maximum speed (0.5 RPS after gear reduction)

// Timing configuration
#define STATUS_UPDATE_INTERVAL      100    // Status update every 500ms
#define MOTOR_SPEED_UPDATE_INTERVAL 10     // Speed update every 50ms for smooth variation

// Queue size configuration
#define COMMAND_QUEUE_SIZE          20     // Command queue size
#define NOTIFICATION_QUEUE_SIZE     10     // Notification queue size (smaller since only warnings/errors)  
#define STATUS_UPDATE_QUEUE_SIZE    30     // Status update queue size

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

// Notification types (for warnings and errors only)
enum class NotificationType {
    WARNING,    // Success with warning message
    ERROR       // Error occurred
};

// Status update types for inter-task communication
enum class StatusUpdateType {
    SPEED_CHANGED,              // Current/actual speed changed (for display only)
    SPEED_SETPOINT_CHANGED,     // User setpoint changed (for UI controls)
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
    TMC2209_STATUS_UPDATE
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

// Notification structure (for warnings and errors only)
struct NotificationData {
    NotificationType type;
    char message[128];  // fixed-size buffer to prevent heap fragmentation
    NotificationData() : type(NotificationType::WARNING) { message[0] = '\0'; }
    void setMessage(const char* msg) {
        if (msg) {
            strncpy(message, msg, sizeof(message) - 1);
            message[sizeof(message) - 1] = '\0';
        } else {
            message[0] = '\0';
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
    // Helper constructors
    StatusUpdateData() : type(StatusUpdateType::SPEED_CHANGED) {
        floatValue = 0.0f;
    }
    StatusUpdateData(StatusUpdateType t, float value) : type(t) {
        floatValue = value;
    }
    StatusUpdateData(StatusUpdateType t, bool value) : type(t) {
        boolValue = value;
    }
    StatusUpdateData(StatusUpdateType t, int value) : type(t) {
        intValue = value;
    }
    StatusUpdateData(StatusUpdateType t, uint32_t value) : type(t) {
        uint32Value = value;
    }
    StatusUpdateData(StatusUpdateType t, unsigned long value) : type(t) {
        ulongValue = value;
    }
};

class StepperController : public Task {
private:
    bool isInitializing; // True during construction/initialization, false otherwise
    // FastAccelStepper engine and stepper
    FastAccelStepperEngine engine;
    FastAccelStepper* stepper;
    
    // TMC2209 stepper driver
    TMC2209 stepperDriver;
    HardwareSerial& serialStream;
    Preferences preferences;
    
    // Speed settings (in RPM)
    float setpointRPM;         // Target RPM set by user/command
    float currentRPM;          // Actual/measured RPM (calculated from stepper)
    int runCurrent;
    
    // Acceleration tracking
    uint32_t setpointAcceleration;   // Target acceleration in steps/s²
    
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
    
    // Notification queue for warnings and errors only
    QueueHandle_t notificationQueue;
    
    // Status update queue for thread-safe status communication
    QueueHandle_t statusUpdateQueue;
    
    // Helper methods
    bool initPreferences();
    void saveSettings();
    void loadSettings();
    void configureDriver();
    void configureStallDetection(bool enableStealthChop = true);
    inline uint32_t rpmToStepsPerSecond(float rpm) const;  // Inline hint for frequent calls
    
    // Internal methods (called from command processing)
    void setSpeedInternal(float rpm);
    void setDirectionInternal(bool clockwise);
    void enableInternal();
    void disableInternal();
    void emergencyStopInternal();
    void setRunCurrentInternal(int current);
    void setAccelerationInternal(uint32_t accelerationStepsPerSec2);
    void resetCountersInternal();
    void resetStallCountInternal();
    void setSpeedVariationInternal(float strength);
    void setSpeedVariationPhaseInternal(float phase);
    void enableSpeedVariationInternal();
    void disableSpeedVariationInternal();
    void requestAllStatusInternal();
    
    // Speed variation helper methods
    inline float calculateVariableSpeed() const;  // Inline hint for frequent calls
    inline float getPositionAngle() const;       // Inline hint for frequent calls
    uint32_t calculateRequiredAccelerationForVariableSpeed() const;
    void updateAccelerationForVariableSpeed();
    void updateSpeedForVariableSpeed();    // Update base speed for variable speed constraints
    void updateSpeedVariationParameters(); // Helper to calculate k and k0
    float calculateMaxAllowedBaseSpeed() const; // Calculate max base speed to not exceed MAX_SPEED_RPM
    
    // Helper methods for status reporting
    void sendNotification(NotificationType type, const String& message = "");
    void publishStatusUpdate(StatusUpdateType type, float value);
    void publishStatusUpdate(StatusUpdateType type, bool value);
    void publishStatusUpdate(StatusUpdateType type, int value);
    void publishStatusUpdate(StatusUpdateType type, uint32_t value);
    void publishStatusUpdate(StatusUpdateType type, unsigned long value);
    
    // Centralized stepper hardware control methods (always publish status when hardware is changed)
    void applyStepperSpeed(uint32_t stepsPerSecond);      // Set target speed
    void applyStepperSetpointSpeed(float rpm);            // Set target speed in RPM and publish setpoint update
    void updateCurrentRPM();                                      // Update actual/measured RPM
    void applyStepperAcceleration(uint32_t accelerationStepsPerSec2); // Set target acceleration
    
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
    bool setSpeed(float rpm);
    bool setDirection(bool clockwise);
    bool enable();
    bool disable();
    bool emergencyStop();
    bool setRunCurrent(int current);
    bool setAcceleration(uint32_t accelerationStepsPerSec2);
    bool resetCounters();
    bool resetStallCount();
    
    // Speed variation control (thread-safe via command queue)
    uint32_t setSpeedVariation(float strength);           // Set variation strength (0.0 to 1.0)
    uint32_t setSpeedVariationPhase(float phase);         // Set phase offset (0.0 to 2*PI)
    uint32_t enableSpeedVariation();                      // Enable variable speed (current position becomes slowest)
    uint32_t disableSpeedVariation();                     // Disable variable speed
    
    // Status request (thread-safe via command queue)
    uint32_t requestAllStatus();                           // Request all current status to be published
    
    // Speed variation information getters
    float getMaxAllowedBaseSpeed() const;                  // Get maximum base speed that doesn't exceed MAX_SPEED_RPM with current variation
    
    // Notification retrieval (thread-safe)
    bool getNotification(NotificationData& notification); // non-blocking, returns false if no notification available
    
    // Status update retrieval (thread-safe)
    bool getStatusUpdate(StatusUpdateData& status); // non-blocking, returns false if no update available
    
    // Stall detection configuration
    void setStallGuardThreshold(uint8_t threshold); // 0 = most sensitive, 255 = least sensitive
};

#endif // STEPPER_CONTROLLER_H
