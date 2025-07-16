#ifndef STEPPER_CONTROLLER_H
#define STEPPER_CONTROLLER_H

#include <Arduino.h>
#include "FastAccelStepper.h"
#include <TMC2209.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "Task.h"
#include "SystemStatus.h"
#include "SystemCommand.h"

// Forward declarations
class PowerDeliveryTask;

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
#define MICRO_STEPS                 16     // Fixed at 16 microsteps
#define TOTAL_MICRO_STEPS_PER_REVOLUTION  (STEPS_PER_REVOLUTION * GEAR_RATIO * MICRO_STEPS) // Total microsteps per output revolution

// Speed settings (in RPM)
#define MIN_SPEED_RPM               0.1f   // Minimum speed
#define MAX_SPEED_RPM               30.0f  // Maximum speed (0.5 RPS after gear reduction)

// Timing configuration
#define STATUS_UPDATE_INTERVAL      100    // Status update every 500ms
#define MOTOR_SPEED_UPDATE_INTERVAL 10     // Speed update every 50ms for smooth variation

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
    int runCurrent;
    
    // State tracking
    bool motorEnabled;
    bool clockwise;
    unsigned long startTime;
    unsigned long totalMicroSteps;
    bool isFirstStart;
    bool tmc2209Initialized;  // Track TMC2209 driver initialization status
    bool powerDeliveryReady;  // Track if power delivery negotiation is complete
    
    // Stall detection
    bool stallDetected;
    unsigned long lastStallTime;
    uint16_t stallCount;
    
    // Acceleration tracking
    uint32_t setpointAcceleration;   // Target acceleration in steps/s²
    
    // Speed variation settings
    bool speedVariationEnabled;
    float speedVariationStrength;    // 0.0 to 1.0 (0% to 100% variation)
    float speedVariationPhase;       // Phase offset in radians (0 to 2*PI)
    int32_t speedVariationStartPosition; // Position where variation was enabled
    float speedVariationK;           // Internal k parameter (derived from strength)
    float speedVariationK0;          // Compensation factor k0 = sqrt(1 - k²)
    
    // Cached references to system singletons
    SystemStatus& systemStatus;
    SystemCommand& systemCommand;
    
    // Helper methods
    bool initPreferences();
    void saveSettings();
    void loadSettings();
    void configureDriver();
    void configureStallDetection(bool enableStealthChop = true);
    bool checkPowerDeliveryReady();    // Check if power delivery is ready for stepper initialization
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
    
    // Centralized stepper hardware control methods (always publish status when hardware is changed)
    void applyStepperSetpointSpeed(float rpm);            // Set target speed in RPM and publish setpoint update
    void applyStepperAcceleration(uint32_t accelerationStepsPerSec2); // Set target acceleration
    void applyRunClockwise();                        // Set direction to clockwise
    void applyRunCounterClockwise();                   // Set direction to counter-clockwise
    void applyStop();
    void applyCurrent(uint8_t current);                     // Set run current in mA

    void publishTMC2209Communication(); // Check TMC2209 driver communication status
    void publishStallDetection();        // Check stall detection status and update stallDetected, stallCount, lastStallTime
    void publishCurrentRPM();                                      // Update actual/measured RPM
    void publishTotalRevolutions();                              // Update total revolutions based on current position   
    void publishRuntime();

    void stepperSetSpeed(float rpm);                            // Set target speed
    void stepperSetAcceleration(uint32_t accelerationStepsPerSec2); // Set target acceleration

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
    };

#endif // STEPPER_CONTROLLER_H
