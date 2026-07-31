#ifndef STEPPER_CONTROLLER_H
#define STEPPER_CONTROLLER_H

#include "FastAccelStepper.h"
#include <TMC2209.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "Task.h"
#include "BoardPins.h"
#include "SystemStatus.h"
#include "SystemCommand.h"
#include "PowerDeliveryTask.h"
#include "dbg_print.h"

// Forward declarations
class PowerDeliveryTask;

// Motor specifications
#define STEPS_PER_REVOLUTION 200                                                           // NEMA 17
#define GEAR_RATIO 10                                                                      // 1:10 reduction
#define MICRO_STEPS 16                                                                     // Fixed at 16 microsteps
#define TOTAL_MICRO_STEPS_PER_REVOLUTION (STEPS_PER_REVOLUTION * GEAR_RATIO * MICRO_STEPS) // Total microsteps per output revolution

// Speed settings (in RPM)
#define MIN_SPEED_RPM 0.1f  // Minimum speed
#define MAX_SPEED_RPM 30.0f // Maximum speed (0.5 RPS after gear reduction)

// Acceleration limits (steps/s^2)
#define MIN_ACCELERATION 100
#define MAX_ACCELERATION 100000
#define EMERGENCY_STOP_ACCELERATION 16000 // Hard braking ramp for emergency stop

// Timing configuration
#define FAST_UPDATE_INTERVAL 100
#define STALL_UPDATE_INTERVAL 1000
#define TMC_UPDATE_INTERVAL 2000
#define MOTOR_SPEED_UPDATE_INTERVAL 100

class StepperController : public Task
{
private:
    bool isInitializing; // True during construction/initialization, false otherwise
    // FastAccelStepper engine and stepper
    FastAccelStepperEngine engine;
    FastAccelStepper *stepper;

    // TMC2209 stepper driver
    TMC2209 stepperDriver;
    HardwareSerial &serialStream;
    Preferences preferences;

    // Speed settings (in RPM)
    float setpointRPM; // Target RPM set by user/command
    int runCurrent;

    // State tracking
    bool motorEnabled;
    bool clockwise;
    unsigned long totalMicroSteps;
    float currentAngle;

    // Runtime accounting. Only time during which the motor is actually turning
    // counts, so the reported runtime (and the average speed derived from it)
    // is not inflated by idle periods between runs.
    unsigned long runtimeAccumulatedMs; // Completed running segments
    unsigned long runSegmentStartMs;    // Start of the segment in progress
    bool runtimeSegmentActive;          // Whether a segment is in progress.
                                        // An explicit flag, not a 0 sentinel on
                                        // runSegmentStartMs - millis() really is
                                        // 0 at boot and again at every wrap.
    bool tmc2209Initialized; // Track TMC2209 driver initialization status
    bool powerDeliveryReady; // Track if power delivery negotiation is complete

    // Stall detection
    bool stallDetected;
    uint16_t stallCount;
    unsigned long lastStallNotifyTime; // 0 = never notified; rate limits stall notifications

    // Tracks the last reported temperature state so notifications are only sent
    // on change rather than on every poll.
    int lastTemperatureStatus;
    bool lastOverTemperatureShutdown;

    // Last stepper position sampled by publishTotalRevolutions()
    int32_t lastRevolutionPosition;

    // StallGuard settings
    uint8_t stallGuardThreshold; // StallGuard threshold (0-255, 0=least sensitive, 255=most sensitive)

    // Acceleration tracking
    uint32_t setpointAcceleration; // Target acceleration in steps/s²

    // Speed variation settings
    bool speedVariationEnabled;
    float speedVariationStrength;        // 0.0 to 1.0 (0% to 100% variation)
    float speedVariationPhase;           // Phase offset in radians (0 to 2*PI)
    int32_t speedVariationStartPosition; // Position where variation was enabled
    float speedVariationK;               // Internal k parameter (derived from strength)
    float speedVariationK0;              // Compensation factor k0 = sqrt(1 - k²)

    // Cached references to system singletons
    SystemStatus &systemStatus;
    SystemCommand &systemCommand;

    StepperController();
    ~StepperController();
    StepperController(const StepperController &) = delete;
    StepperController &operator=(const StepperController &) = delete;

    // Helper methods
    void saveSettings();
    void loadSettings();
    void configureDriver();
    bool checkPowerDeliveryReady();                       // Check if power delivery is ready for stepper initialization
    inline uint32_t rpmToStepsPerSecond(float rpm) const; // Inline hint for frequent calls

    // Runtime accounting helpers
    void startRuntimeAccounting();          // Begin a running segment (no-op if already running)
    void stopRuntimeAccounting();           // Close the current segment (no-op if already stopped)
    unsigned long currentRuntimeMs() const; // Total running time, including the open segment

    // Internal methods (called from command processing)
    void setSpeedInternal(float rpm);
    void adjustSpeedInternal(float deltaRpm);
    void toggleEnabledInternal();
    void setDirectionInternal(bool runClockwise);
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
    void setStallGuardThresholdInternal(uint8_t threshold);
    void requestAllStatusInternal();

    // Speed variation helper methods
    inline float calculateVariableSpeed(); // Inline hint for frequent calls
    void getPositionAngle();       // Inline hint for frequent calls
    uint32_t calculateRequiredAccelerationForVariableSpeed() const;
    void updateAccelerationForVariableSpeed();
    void updateSpeedForVariableSpeed();         // Update base speed for variable speed constraints
    void updateSpeedVariationParameters();      // Helper to calculate k and k0
    float calculateMaxAllowedBaseSpeed() const; // Calculate max base speed to not exceed MAX_SPEED_RPM

    // Centralized stepper hardware control methods (always publish status when hardware is changed)
    void applyStepperSetpointSpeed(float rpm);                        // Set target speed in RPM and publish setpoint update
    void applyStepperAcceleration(uint32_t accelerationStepsPerSec2); // Set target acceleration
    void applyRun(bool runClockwise);                                 // Enable the driver and run in the given direction
    void applyStop();
    void applyCurrent(uint8_t current); // Set run current in mA

    void publishTMC2209Communication(); // Check TMC2209 driver communication status
    void publishTMC2209Temperature();   // Check TMC2209 temperature status
    void publishStallDetection();       // Check stall detection status and update stallDetected, stallCount, lastStallTime
    void publishStallGuardResult();     // Update StallGuard result (0-510)
    void publishCurrentRPM();           // Update actual/measured RPM
    void publishTotalRevolutions();     // Update total revolutions based on current position
    void publishRuntime();
    void publishCurrentAngle(); // Publish current angle in radians (0-2*PI)

    // Split status update helpers
    void publishFastStatusUpdates();  // Speed, runtime, revolutions (100ms)
    void publishStallStatusUpdates(); // Stall status/count (1s)
    void publishTMCStatusUpdates();   // TMC2209 status/temperature (2s)

    void stepperSetSpeed(float rpm);                                // Set target speed
    void stepperSetAcceleration(uint32_t accelerationStepsPerSec2); // Set target acceleration

    // Speed variation control
    void updateMotorSpeed();

protected:
    // Task implementation
    void run() override;

    // Internal methods
    void processCommand(const StepperCommandData &cmd);

    // Helper methods for run() timing
    TickType_t calculateQueueTimeout(unsigned long nextUpdate);
    bool isUpdateDue(unsigned long nextUpdate);

public:
    // Initialization
    bool begin();

    static StepperController &getInstance()
    {
        static StepperController instance;
        return instance;
    }
};

#endif // STEPPER_CONTROLLER_H
