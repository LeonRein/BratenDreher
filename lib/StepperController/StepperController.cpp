// Always include the header first to ensure class/type declarations
#include "StepperController.h"
#include "PowerDeliveryTask.h"

void StepperController::applyStepperSetpointSpeed(float rpm) {
    if (!stepper) {
        Serial.println("WARNING: Cannot apply setpoint speed - stepper not initialized");
        return;
    }

    stepperSetSpeed(rpm);

    setpointRPM = rpm;

    systemStatus.publishStatusUpdate(StatusUpdateType::SPEED_SETPOINT_CHANGED, rpm);
}

void StepperController::applyStepperAcceleration(uint32_t accelerationStepsPerSec2) {
    if (!stepper) {
        Serial.println("WARNING: Cannot apply acceleration - stepper not initialized");
        return;
    }
    stepperSetAcceleration(accelerationStepsPerSec2);

    setpointAcceleration = accelerationStepsPerSec2;

    systemStatus.publishStatusUpdate(StatusUpdateType::ACCELERATION_CHANGED, accelerationStepsPerSec2);
}

void StepperController::applyRunClockwise() {
    if (!stepper) {
        Serial.println("WARNING: Cannot set direction - stepper not initialized");
        return;
    }
    
    // Check power delivery and warn if not optimal, but still allow operation
    PowerDeliveryTask& pdTask = PowerDeliveryTask::getInstance();
    if (pdTask.isNegotiationComplete() && pdTask.getNegotiationState() == PDNegotiationState::SUCCESS && !pdTask.isPowerGood()) {
        Serial.println("WARNING: Power delivery indicates power not good, but enabling motor anyway");
    } else if (!pdTask.isNegotiationComplete()) {
        Serial.println("INFO: Motor enabled without power delivery negotiation (no PD adapter or still negotiating)");
    }

    if(!motorEnabled) {
        publishTMC2209Communication();
        stepperDriver.enable();
        motorEnabled = true;
        systemStatus.publishStatusUpdate(StatusUpdateType::ENABLED_CHANGED, true);
    }
    
    stepper->runForward(); // In FastAccelStepper, backward means clockwise
    clockwise = true;

    systemStatus.publishStatusUpdate(StatusUpdateType::DIRECTION_CHANGED, clockwise);
}

void StepperController::applyRunCounterClockwise() {
    if (!stepper) {
        Serial.println("WARNING: Cannot set direction - stepper not initialized");
        return;
    }
    
    // Check power delivery and warn if not optimal, but still allow operation
    PowerDeliveryTask& pdTask = PowerDeliveryTask::getInstance();
    if (pdTask.isNegotiationComplete() && pdTask.getNegotiationState() == PDNegotiationState::SUCCESS && !pdTask.isPowerGood()) {
        Serial.println("WARNING: Power delivery indicates power not good, but enabling motor anyway");
    } else if (!pdTask.isNegotiationComplete()) {
        Serial.println("INFO: Motor enabled without power delivery negotiation (no PD adapter or still negotiating)");
    }

    if(!motorEnabled) {
        stepperDriver.enable();
        publishTMC2209Communication();
        motorEnabled = true;
        systemStatus.publishStatusUpdate(StatusUpdateType::ENABLED_CHANGED, true);
    }
    
    stepper->runBackward(); // In FastAccelStepper, backward means counter-clockwise
    clockwise = false;

    systemStatus.publishStatusUpdate(StatusUpdateType::DIRECTION_CHANGED, clockwise);
}

void StepperController::applyStop() {
    if (!stepper) {
        Serial.println("WARNING: Cannot stop stepper - not initialized");
        return;
    }

    stepper->stopMove();
    motorEnabled = false;
    publishTMC2209Communication();

    systemStatus.publishStatusUpdate(StatusUpdateType::ENABLED_CHANGED, false);
}

void StepperController::applyCurrent(uint8_t current) {
    if (!stepper) {
        Serial.println("WARNING: Cannot set current - stepper not initialized");
        return;
    }

    publishTMC2209Communication();
    if (!tmc2209Initialized) {
        Serial.println("ERROR: TMC2209 driver not initialized - cannot set current");
        return;
    }

    runCurrent = current;
    stepperDriver.setRunCurrent(current);

    systemStatus.publishStatusUpdate(StatusUpdateType::CURRENT_CHANGED, current);
}

void StepperController::publishTMC2209Communication() {
    bool isCommunicating = stepperDriver.isSetupAndCommunicating();
    
    if (!isCommunicating) {
        tmc2209Initialized = false;
        systemStatus.publishStatusUpdate(StatusUpdateType::TMC2209_STATUS_UPDATE, false);
        systemStatus.sendNotification(NotificationType::ERROR, "TMC2209 driver not initialized or not communicating");
    } else {
        tmc2209Initialized = true;
        systemStatus.publishStatusUpdate(StatusUpdateType::TMC2209_STATUS_UPDATE, true);
    }
}

void StepperController::publishTMC2209Temperature() {
    if (!tmc2209Initialized) {
        Serial.println("WARNING: Cannot read temperature - TMC2209 not initialized");
        return;
    }

    // Read TMC2209 status which includes temperature information
    TMC2209::Status status = stepperDriver.getStatus();
    
    // Determine temperature status based on warning flags
    // Temperature ranges: normal < 120°C < warning < 143°C < critical < 150°C < shutdown < 157°C
    int temperatureStatus = 0; // 0=normal, 1=120°C+, 2=143°C+, 3=150°C+, 4=157°C+
    
    if (status.over_temperature_157c) {
        temperatureStatus = 4; // Critical - 157°C+
    } else if (status.over_temperature_150c) {
        temperatureStatus = 3; // High - 150°C+
    } else if (status.over_temperature_143c) {
        temperatureStatus = 2; // Elevated - 143°C+
    } else if (status.over_temperature_120c) {
        temperatureStatus = 1; // Warm - 120°C+
    }
    
    // Publish temperature status update
    systemStatus.publishStatusUpdate(StatusUpdateType::TMC2209_TEMPERATURE_UPDATE, temperatureStatus);
    
    // Send notifications for temperature warnings
    if (status.over_temperature_shutdown) {
        systemStatus.sendNotification(NotificationType::ERROR, "TMC2209 over-temperature shutdown! Driver disabled for safety.");
    } else if (status.over_temperature_warning || temperatureStatus >= 2) {
        if (temperatureStatus == 4) {
            systemStatus.sendNotification(NotificationType::ERROR, "TMC2209 critical temperature (>157°C)! Reduce current or improve cooling.");
        } else if (temperatureStatus == 3) {
            systemStatus.sendNotification(NotificationType::WARNING, "TMC2209 high temperature (>150°C). Consider reducing current.");
        } else if (temperatureStatus == 2) {
            systemStatus.sendNotification(NotificationType::WARNING, "TMC2209 elevated temperature (>143°C). Monitor thermal conditions.");
        }
    }
    
    // Log temperature status for debugging
    if (temperatureStatus > 0) {
        const char* tempLabels[] = {"Normal", "Warm (>120°C)", "Elevated (>143°C)", "High (>150°C)", "Critical (>157°C)"};
        Serial.printf("TMC2209 Temperature: %s\n", tempLabels[temperatureStatus]);
    }
}

void StepperController::publishStallDetection() {
    publishTMC2209Communication();

    if (!tmc2209Initialized) {
        Serial.println("WARNING: Cannot check stall detection - TMC2209 not initialized");
        return;
    }

    // Update stall detection state efficiently
    const bool diagPinHigh = digitalRead(DIAG_PIN);
    
    if (motorEnabled) {
        if (diagPinHigh && !stallDetected) {
            // New stall detected
            stallDetected = true;
            stallCount++;
            systemStatus.sendNotification(NotificationType::WARNING, "Stall detected! Check motor load or settings.");
            Serial.printf("STALL DETECTED! Count: %d, Time: %lu\n", stallCount, millis());
            Serial.println("Consider: reducing speed, increasing current, or checking load");
        } else if (!diagPinHigh && stallDetected) {
            // Stall cleared
            stallDetected = false;
            Serial.println("Stall condition cleared");
        }
    } else if (stallDetected) {
        // Clear stall status when motor is stopped
        stallDetected = false;
        Serial.println("Stall status cleared (motor stopped)");
    }

    // Publish stall status update
    systemStatus.publishStatusUpdate(StatusUpdateType::STALL_DETECTED_UPDATE, stallDetected);
    systemStatus.publishStatusUpdate(StatusUpdateType::STALL_COUNT_UPDATE, static_cast<int>(stallCount));
}


void StepperController::publishCurrentRPM() {
    if (!stepper) {
        systemStatus.publishStatusUpdate(StatusUpdateType::SPEED_UPDATE, 0.0f);
        return;
    }

    float currentStepsPerSecond = static_cast<float>(abs(stepper->getCurrentSpeedInMilliHz() / 1000)) ;

    float rpm = (currentStepsPerSecond * 60.0f) /
        (static_cast<float>(GEAR_RATIO) * static_cast<float>(STEPS_PER_REVOLUTION) * static_cast<float>(MICRO_STEPS));

    systemStatus.publishStatusUpdate(StatusUpdateType::SPEED_UPDATE, rpm);
}

// Centralized stepper hardware control methods
void StepperController::stepperSetSpeed(float rpm) {
    if (!stepper) {
        Serial.println("WARNING: Cannot apply speed - stepper not initialized");
        return;
    }
    const uint32_t stepsPerSecond = rpmToStepsPerSecond(rpm);

    // Set the actual speed on the hardware
    stepper->setSpeedInHz(stepsPerSecond);
    stepper->applySpeedAcceleration(); 
}

void StepperController::stepperSetAcceleration(uint32_t accelerationStepsPerSec2) {
    if (!stepper) {
        Serial.println("WARNING: Cannot apply acceleration - stepper not initialized");
        return;
    }

    stepper->setAcceleration(accelerationStepsPerSec2);
    stepper->applySpeedAcceleration();
}

StepperController::StepperController() 
    : Task("Stepper_Task", 4096, 1, 1), // Task name, 4KB stack, priority 1, core 1
      isInitializing(true), // Start in initialization mode
      stepper(nullptr), serialStream(Serial2), setpointRPM(1.0f),
      runCurrent(30), motorEnabled(false), clockwise(true),
      startTime(0), totalMicroSteps(0), isFirstStart(true), tmc2209Initialized(false), powerDeliveryReady(false),
      stallDetected(false), stallCount(0),
      stallGuardThreshold(10),  // Initialize StallGuard with default threshold
      setpointAcceleration(0),  // Will be set during initialization
      speedVariationEnabled(false), speedVariationStrength(0.0f), speedVariationPhase(0.0f), speedVariationStartPosition(0),
      speedVariationK(0.0f), speedVariationK0(1.0f),  // Initialize with default values
      systemStatus(SystemStatus::getInstance()), systemCommand(SystemCommand::getInstance())
{
    // No need to create command queue - using SystemCommand singleton
}

StepperController::~StepperController() {
    // No cleanup needed for SystemCommand (singleton manages itself)
}

bool StepperController::begin() {
    Serial.println("Initializing FastAccelStepper with TMC2209...");
    
    // Initialize preferences storage
    initPreferences();
    
    // Configure pins
    pinMode(TMC_EN_PIN, OUTPUT);
    pinMode(MS1_PIN, OUTPUT);
    pinMode(MS2_PIN, OUTPUT);
    pinMode(DIAG_PIN, INPUT);
    
    // Set serial address pins
    digitalWrite(MS1_PIN, LOW);
    digitalWrite(MS2_PIN, LOW);
    
    
    // Initialize TMC2209
    stepperDriver.setup(serialStream, 115200, TMC2209::SERIAL_ADDRESS_0, TMC_RX_PIN, TMC_TX_PIN);
    
    // Load saved settings
    loadSettings();
    
    // Configure driver with loaded settings
    configureDriver();
    
    // Check TMC2209 initialization status after configuration
    tmc2209Initialized = stepperDriver.isSetupAndCommunicating();
    
    // Publish initial TMC2209 status using the communication manager
    systemStatus.publishStatusUpdate(StatusUpdateType::TMC2209_STATUS_UPDATE, tmc2209Initialized);
    
    if (tmc2209Initialized) {
        Serial.println("TMC2209 driver initialized and communicating successfully");
    } else {
        Serial.println("WARNING: TMC2209 driver initialization failed or not responding");
    }
    
    // Initialize FastAccelStepper engine
    engine.init();
    
    // Connect stepper to pin
    stepper = engine.stepperConnectToPin(STEP_PIN);
    
    if (!stepper) {
        Serial.println("Failed to initialize FastAccelStepper");
        return false;
    }
    
    // Configure FastAccelStepper
    stepper->setDirectionPin(DIR_PIN);
    stepper->setEnablePin(TMC_EN_PIN);
    stepper->setAutoEnable(true);
    
    // Set delays for enable/disable
    stepper->setDelayToEnable(50);
    stepper->setDelayToDisable(1000);
    
    // Set acceleration (steps/s²) - optimized for variable speed operation
    // For variable speed to work smoothly, we need fast acceleration
    // Target: reach MAX_SPEED_RPM in 2 seconds for responsive speed changes
    // Calculate: 30 RPM * 10 gear ratio * 200 steps * 16 microsteps / 60 seconds = 16000 steps/s
    // Acceleration for 2 seconds: 16000 / 2 = 8000 steps/s²
    uint32_t defaultAcceleration = rpmToStepsPerSecond(MAX_SPEED_RPM) / 2;  // 2 seconds to reach max speed
    applyStepperAcceleration(defaultAcceleration);
    
    // Note: Acceleration status update will be sent in batch with other initial updates
    
    // Set initial speed using internal method (avoid command queue during initialization)
    setSpeedInternal(setpointRPM);
    
    // Publish initial status updates for loaded settings using the communication manager
    systemStatus.publishStatusUpdate(StatusUpdateType::DIRECTION_CHANGED, clockwise);
    systemStatus.publishStatusUpdate(StatusUpdateType::CURRENT_CHANGED, runCurrent);
    systemStatus.publishStatusUpdate(StatusUpdateType::ENABLED_CHANGED, false); // Initially disabled
    systemStatus.publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_ENABLED_CHANGED, speedVariationEnabled);
    systemStatus.publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_STRENGTH_CHANGED, speedVariationStrength);
    systemStatus.publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_PHASE_CHANGED, speedVariationPhase);
    systemStatus.publishStatusUpdate(StatusUpdateType::ACCELERATION_CHANGED, defaultAcceleration);
    systemStatus.publishStatusUpdate(StatusUpdateType::STALLGUARD_THRESHOLD_CHANGED, static_cast<int>(stallGuardThreshold));
    
    // Initialize speed variation parameters (k and k0)
    updateSpeedVariationParameters();
    
    // Initially disabled
    stepperDriver.disable();
    
    // Initialization complete
    isInitializing = false;
    
    return true;
}

bool StepperController::initPreferences() {
    // Try to open the namespace with write permissions to ensure it exists
    if (preferences.begin("stepper", false)) {
        // Check if this is a fresh namespace by looking for a key
        if (!preferences.isKey("speed")) {
            // Fresh namespace - write default values
            Serial.println("Fresh preferences namespace, writing defaults");
            preferences.putFloat("speed", setpointRPM);
            preferences.putBool("clockwise", clockwise);
            preferences.putInt("microsteps", MICRO_STEPS);
            preferences.putInt("current", runCurrent);
        }
        preferences.end();
        Serial.println("Preferences namespace initialized");
        return true;
    } else {
        Serial.println("Failed to initialize preferences namespace");
        return false;
    }
}

void StepperController::configureDriver() {
    stepperDriver.setRunCurrent(runCurrent);
    stepperDriver.setMicrostepsPerStep(MICRO_STEPS);
    stepperDriver.enableAutomaticCurrentScaling(); //current control mode
    //  stepperDriver.enableCoolStep();
    stepperDriver.enableStealthChop(); //stealth chop needs to be enabled for stall detect
    stepperDriver.setCoolStepDurationThreshold(5000); //TCOOLTHRS (DIAG only enabled when TSTEP smaller than this)
    
    // Configure StallGuard
    stepperDriver.setStallGuardThreshold(stallGuardThreshold);
    
    // Update communication status after configuration
    bool newTmc2209Status = stepperDriver.isSetupAndCommunicating();


    
    // Publish TMC2209 status update if communication status changed
    if (newTmc2209Status != tmc2209Initialized) {
        systemStatus.publishStatusUpdate(StatusUpdateType::TMC2209_STATUS_UPDATE, newTmc2209Status);
    }
    tmc2209Initialized = newTmc2209Status;
    
    // Check if driver is communicating properly
    if (tmc2209Initialized) {
        Serial.printf("TMC2209 configured: %d microsteps, %d%% current, StallGuard threshold: %d\n", 
                     MICRO_STEPS, runCurrent, stallGuardThreshold);
        Serial.println("Note: StallGuard may require disabling StealthChop for optimal detection");
    } else {
        Serial.println("WARNING: TMC2209 driver not responding during configuration");
    }
}

bool StepperController::checkPowerDeliveryReady() {
    PowerDeliveryTask& pdTask = PowerDeliveryTask::getInstance();
    
    // Check if power delivery is ready (successful negotiation AND power good)
    if (pdTask.isNegotiationComplete() && pdTask.isPowerGood()) {
        if (!powerDeliveryReady) {
            powerDeliveryReady = true;
            Serial.printf("StepperController: Power delivery ready - %dV negotiated, %0.1fV measured\n", 
                         pdTask.getNegotiatedVoltage(), pdTask.getCurrentVoltage());
        }
        return true;
    }
    
    // If negotiation is complete but failed, check if it's because no PD adapter is connected
    if (pdTask.isNegotiationComplete() && !pdTask.isPowerGood()) {
        PDNegotiationState state = pdTask.getNegotiationState();
        
        // If negotiation timed out or failed, assume no PD adapter and allow operation
        if (state == PDNegotiationState::FAILED) {
            if (!powerDeliveryReady) {
                powerDeliveryReady = true;
                Serial.println("StepperController: No PD adapter detected, allowing operation without PD safety");
            }
            return true;
        }
    }
    
    // If we get here, either negotiation is still in progress or power was lost
    if (powerDeliveryReady) {
        powerDeliveryReady = false;
        Serial.println("StepperController: Power delivery lost or negotiation in progress");
    }
    return false;
}

uint32_t StepperController::rpmToStepsPerSecond(float rpm) const {
    // Optimized calculation
    // Formula: (rpm * GEAR_RATIO * STEPS_PER_REVOLUTION * MICRO_STEPS) / 60.0f
    float gearRatio = static_cast<float>(GEAR_RATIO);
    float stepsPerRev = static_cast<float>(STEPS_PER_REVOLUTION);
    float microSteps = static_cast<float>(MICRO_STEPS);
    
    const float motorStepsPerSecond = (rpm * gearRatio * stepsPerRev * microSteps) / 60.0f;
    return static_cast<uint32_t>(motorStepsPerSecond);
}

void StepperController::run() {
    Serial.println("Stepper Task started");
    
    // Wait for power delivery negotiation with timeout
    Serial.println("Waiting for power delivery negotiation...");
    unsigned long pdWaitStart = millis();
    const unsigned long PD_WAIT_TIMEOUT = 10000; // 10 second timeout
    bool pdTimedOut = false;
    
    while (!checkPowerDeliveryReady() && !pdTimedOut) {
        if (millis() - pdWaitStart >= PD_WAIT_TIMEOUT) {
            pdTimedOut = true;
            Serial.println("StepperController: Power delivery negotiation timed out");
            Serial.println("StepperController: Proceeding with stepper initialization (no PD adapter or negotiation failed)");
            Serial.println("StepperController: Motor control will be available but without PD safety features");
        } else {
            vTaskDelay(pdMS_TO_TICKS(500)); // Check every 500ms
        }
    }
    
    if (!pdTimedOut) {
        Serial.println("StepperController: Power delivery negotiation successful, proceeding with full safety features");
    }
    
    // Initialize stepper controller
    if (!begin()) {
        Serial.println("Failed to initialize stepper controller!");
        return;
    }
    
    Serial.println("Stepper Controller initialized successfully!");
    
    // Initialize timing variables with cached millis() value
    StepperCommandData cmd;
    uint32_t currentTime = millis();
    unsigned long nextMotorSpeedUpdate = currentTime + MOTOR_SPEED_UPDATE_INTERVAL; // 10ms
    unsigned long nextFastStatusUpdate = currentTime + FAST_UPDATE_INTERVAL; // 100ms
    unsigned long nextStallUpdate = currentTime + 1000; // 1s
    unsigned long nextTMCUpdate = currentTime + 2000; // 2s

    while (true) {
        // Find the next event to wait for
        unsigned long nextEvent = min(nextMotorSpeedUpdate, min(nextFastStatusUpdate, min(nextStallUpdate, nextTMCUpdate)));
        TickType_t timeout = calculateQueueTimeout(nextEvent);

        if (systemCommand.getCommand(cmd, timeout)) {
            processCommand(cmd);
        }

        currentTime = millis();

        // Motor speed updates (every 10ms for smooth variation)
        if (isUpdateDue(nextMotorSpeedUpdate)) {
            updateMotorSpeed();
            nextMotorSpeedUpdate = currentTime + MOTOR_SPEED_UPDATE_INTERVAL;
        }

        // Fast status updates (every 100ms)
        if (isUpdateDue(nextFastStatusUpdate)) {
            publishFastStatusUpdates();
            nextFastStatusUpdate = currentTime + FAST_UPDATE_INTERVAL;
        }

        // Stall status updates (every 1s)
        if (isUpdateDue(nextStallUpdate)) {
            publishStallStatusUpdates();
            
            nextStallUpdate = currentTime + STALL_UPDATE_INTERVAL;
        }

        // TMC2209 status/temperature updates (every 2s)
        if (isUpdateDue(nextTMCUpdate)) {
            publishTMCStatusUpdates();
            nextTMCUpdate = currentTime + TMC_UPDATE_INTERVAL;
        }
    }
}

TickType_t StepperController::calculateQueueTimeout(unsigned long nextUpdate) {
    const unsigned long currentTime = millis();
    
    // Handle millis() overflow gracefully
    if (nextUpdate < currentTime) {
        return 0; // Update overdue
    }
    
    unsigned long timeUntilUpdate = nextUpdate - currentTime;
    
    // Clamp timeout to reasonable bounds (max 100ms for responsiveness)
    timeUntilUpdate = min(timeUntilUpdate, 100UL);
    
    return timeUntilUpdate > 0 ? pdMS_TO_TICKS(timeUntilUpdate) : 0;
}

bool StepperController::isUpdateDue(unsigned long nextUpdate) {
    const unsigned long currentTime = millis();
    
    // Handle millis() overflow case
    if (nextUpdate > currentTime && (nextUpdate - currentTime) > 0x80000000UL) {
        return true; // Overflow detected
    }
    
    return currentTime >= nextUpdate;
}

void StepperController::publishTotalRevolutions() {
    if (!stepper) {
        Serial.println("WARNING: Cannot check total revolutions - stepper not initialized");
        return;
    }

    static int32_t lastPosition = 0;
    
    const int32_t currentPosition = stepper->getCurrentPosition();
    int32_t diffMicrosteps = abs(currentPosition - lastPosition);
    lastPosition = currentPosition;

    totalMicroSteps += diffMicrosteps;

    float totalRevolutions = static_cast<float>(totalMicroSteps) / TOTAL_MICRO_STEPS_PER_REVOLUTION;
    systemStatus.publishStatusUpdate(StatusUpdateType::TOTAL_REVOLUTIONS_UPDATE, totalRevolutions);
}

void StepperController::publishRuntime() {
    if (!stepper) {
        Serial.println("WARNING: Cannot check runtime - stepper not initialized");
        return;
    }

    // Calculate runtime only if the motor has been started at least once
    if (isFirstStart || startTime == 0) {
        return; // No runtime to report yet
    }

    const unsigned long currentTime = millis();
    unsigned long runtime = currentTime - startTime; // Keep in milliseconds

    systemStatus.publishStatusUpdate(StatusUpdateType::RUNTIME_UPDATE, runtime);
}

void StepperController::publishPeriodicStatusUpdates() {
    // Deprecated: now split into separate functions for each timing group
    // See publishFastStatusUpdates, publishStallStatusUpdates, publishTMCStatusUpdates
    // This function can be removed if not used elsewhere
    return;

// New functions for split status updates
}

void StepperController::publishFastStatusUpdates() {
    if (!stepper) return;
    publishCurrentRPM();
    publishTotalRevolutions();
    publishRuntime();
    publishStallGuardResult();
}

void StepperController::publishStallStatusUpdates() {
    publishStallDetection();
}

void StepperController::publishTMCStatusUpdates() {
    publishTMC2209Communication();
    publishTMC2209Temperature();
}

void StepperController::processCommand(const StepperCommandData& cmd) {
    Serial.printf("StepperController: Processing command type %d\n", (int)cmd.command);
    
    switch (cmd.command) {
        case StepperCommand::SET_SPEED:
            setSpeedInternal(cmd.floatValue);
            break;
            
        case StepperCommand::SET_DIRECTION:
            setDirectionInternal(cmd.boolValue);
            break;
            
        case StepperCommand::ENABLE:
            enableInternal();
            break;
            
        case StepperCommand::DISABLE:
            disableInternal();
            break;
            
        case StepperCommand::EMERGENCY_STOP:
            emergencyStopInternal();
            break;
            
        case StepperCommand::SET_CURRENT:
            setRunCurrentInternal(cmd.intValue);
            break;
            
        case StepperCommand::SET_ACCELERATION:
            setAccelerationInternal(cmd.intValue);
            break;
            
        case StepperCommand::RESET_COUNTERS:
            resetCountersInternal();
            break;
            
        case StepperCommand::RESET_STALL_COUNT:
            resetStallCountInternal();
            break;
            
        case StepperCommand::SET_SPEED_VARIATION:
            setSpeedVariationInternal(cmd.floatValue);
            break;
            
        case StepperCommand::SET_SPEED_VARIATION_PHASE:
            setSpeedVariationPhaseInternal(cmd.floatValue);
            break;
            
        case StepperCommand::ENABLE_SPEED_VARIATION:
            enableSpeedVariationInternal();
            break;
            
        case StepperCommand::DISABLE_SPEED_VARIATION:
            disableSpeedVariationInternal();
            break;
            
        case StepperCommand::SET_STALLGUARD_THRESHOLD:
            setStallGuardThresholdInternal(cmd.intValue);
            break;
            
        case StepperCommand::REQUEST_ALL_STATUS:
            requestAllStatusInternal();
            break;
    }
}

void StepperController::resetCountersInternal() {
    totalMicroSteps = 0;
    startTime = millis();
    isFirstStart = false;
    
    Serial.println("Counters reset");
    // Success is indicated by the status update - no notification needed
}

void StepperController::resetStallCountInternal() {
    stallCount = 0;
    stallDetected = false;
    
    Serial.println("Stall count reset");
    systemStatus.publishStatusUpdate(StatusUpdateType::STALL_COUNT_UPDATE, stallCount);
    // Success is indicated by the status update - no notification needed
}

void StepperController::setSpeedInternal(float rpm) {
    // Store original requested speed for potential warning message
    const float originalRequestedSpeed = rpm;
    bool speedWasAdjusted = false;
    
    if (!stepper) {
        systemStatus.publishStatusUpdate(StatusUpdateType::SPEED_SETPOINT_CHANGED, setpointRPM);
        systemStatus.sendNotification(NotificationType::ERROR, "Stepper not initialized");
        return;
    }
    
    // Clamp speed to allowed range
    if (rpm < MIN_SPEED_RPM) {
        rpm = MIN_SPEED_RPM;
        speedWasAdjusted = true;
    } else if (rpm > MAX_SPEED_RPM) {
        rpm = MAX_SPEED_RPM;
        speedWasAdjusted = true;
    }
    
    // If speed variation is enabled, check against maximum allowed base speed
    if (speedVariationEnabled && speedVariationStrength > 0.0f) {
        const float maxAllowedBaseSpeed = calculateMaxAllowedBaseSpeed();
        if (rpm > maxAllowedBaseSpeed) {
            Serial.printf("Requested speed %.2f RPM exceeds max allowed %.2f RPM with current variation. Auto-adjusting to max allowed speed.\n", 
                          rpm, maxAllowedBaseSpeed);
            rpm = maxAllowedBaseSpeed; // Auto-adjust to maximum allowed speed
            speedWasAdjusted = true;
        }
    }

    // Update the setpoint
    setpointRPM = rpm;
    
    applyStepperSetpointSpeed(rpm);
    
    // If variable speed is enabled, recalculate acceleration for new base speed
    if (speedVariationEnabled) {
        updateAccelerationForVariableSpeed();
    }
    
    // Save settings only if not during initialization
    if (!isInitializing) {
        saveSettings();
    }
    
    const uint32_t stepsPerSecond = rpmToStepsPerSecond(rpm);  // For logging only
    Serial.printf("Speed setpoint set to %.2f RPM (%u steps/sec)\n", rpm, stepsPerSecond);
    
    // Report warning if speed was adjusted (use efficient string building)
    if (speedWasAdjusted && !isInitializing) {
        char warningMsg[150];
        if (speedVariationEnabled && speedVariationStrength > 0.0f) {
            const float maxAllowedBaseSpeed = calculateMaxAllowedBaseSpeed();
            if (rpm == maxAllowedBaseSpeed && originalRequestedSpeed > maxAllowedBaseSpeed) {
                // Adjusted due to variable speed requirements
                snprintf(warningMsg, sizeof(warningMsg),
                        "Speed auto-adjusted from %.2f to %.2f RPM due to variable speed modulation limits",
                        originalRequestedSpeed, rpm);
            } else {
                // Adjusted due to range limits
                snprintf(warningMsg, sizeof(warningMsg),
                        "Speed auto-adjusted from %.2f to %.2f RPM (allowed range: %.1f-%.1f RPM)",
                        originalRequestedSpeed, rpm, MIN_SPEED_RPM, MAX_SPEED_RPM);
            }
        } else {
            // Adjusted due to range limits
            snprintf(warningMsg, sizeof(warningMsg),
                    "Speed auto-adjusted from %.2f to %.2f RPM (allowed range: %.1f-%.1f RPM)",
                    originalRequestedSpeed, rpm, MIN_SPEED_RPM, MAX_SPEED_RPM);
        }
        systemStatus.sendNotification(NotificationType::WARNING, String(warningMsg));
    }
    // Success is indicated by the status update - no notification needed for normal success
}

void StepperController::setDirectionInternal(bool clockwise) {
    if (!stepper) {
        systemStatus.publishStatusUpdate(StatusUpdateType::DIRECTION_CHANGED, clockwise);
        systemStatus.sendNotification(NotificationType::ERROR, "Stepper not initialized");
        return;
    }

    if(clockwise) {
        applyRunClockwise();
    } else {
        applyRunCounterClockwise();
    }
    
    if (!isInitializing) {
        saveSettings();
    }
}

void StepperController::enableInternal() {
    if (!stepper) {
        systemStatus.publishStatusUpdate(StatusUpdateType::ENABLED_CHANGED, motorEnabled);
        systemStatus.sendNotification(NotificationType::ERROR, "Stepper not initialized");
        return;
    }

    if(clockwise) {
        applyRunClockwise();
    } else {
        applyRunCounterClockwise();
    }
    
    if (isFirstStart) {
        startTime = millis();
        isFirstStart = false;
    }
    
    Serial.println("Motor enabled and started");
}

void StepperController::disableInternal() {
    applyStop();
}

void StepperController::emergencyStopInternal() {
    if (!stepper) {
        systemStatus.publishStatusUpdate(StatusUpdateType::ENABLED_CHANGED, false);
        systemStatus.sendNotification(NotificationType::ERROR, "Stepper not initialized");
        return;
    }

    stepper->forceStopAndNewPosition(stepper->getCurrentPosition());
    
    stepperDriver.disable();
    motorEnabled = false;
    
    Serial.println("EMERGENCY STOP executed");
    
    systemStatus.publishStatusUpdate(StatusUpdateType::ENABLED_CHANGED, false);
}

void StepperController::setRunCurrentInternal(int current) {
    // Validate current (10-100%)
    if (current < 10 || current > 100) {
        systemStatus.publishStatusUpdate(StatusUpdateType::CURRENT_CHANGED, runCurrent);
        systemStatus.sendNotification(NotificationType::ERROR, "Current out of range (10-100%)");
        return;
    }
    
    applyCurrent(current);
    
    if(!isInitializing) {
        saveSettings();
    }
    
    Serial.printf("Run current set to %d%%\n", current);
}

void StepperController::setAccelerationInternal(uint32_t accelerationStepsPerSec2) {
    // Store original requested acceleration for potential warning message
    const uint32_t originalRequestedAccel = accelerationStepsPerSec2;
    bool accelWasAdjusted = false;

    if (!stepper) {
        systemStatus.publishStatusUpdate(StatusUpdateType::ACCELERATION_CHANGED, setpointAcceleration);
        systemStatus.sendNotification(NotificationType::ERROR, "Stepper not initialized");
        return;
    }

    // Clamp acceleration to allowed range
    if (accelerationStepsPerSec2 < 100) {
        accelerationStepsPerSec2 = 100;
        accelWasAdjusted = true;
    } else if (accelerationStepsPerSec2 > 100000) {
        accelerationStepsPerSec2 = 100000;
        accelWasAdjusted = true;
    }

    // If speed variation is enabled, check against minimum required acceleration
    if (speedVariationEnabled && speedVariationStrength > 0.0f) {
        const uint32_t minRequiredAcceleration = calculateRequiredAccelerationForVariableSpeed();
        if (minRequiredAcceleration > 0 && accelerationStepsPerSec2 < minRequiredAcceleration) {
            Serial.printf("Requested acceleration %u steps/s² is below minimum required %u steps/s² for current variable speed settings. Auto-adjusting to minimum required acceleration.\n", 
                          accelerationStepsPerSec2, minRequiredAcceleration);
            accelerationStepsPerSec2 = minRequiredAcceleration; // Auto-adjust to minimum required acceleration
            accelWasAdjusted = true;
        }
    }

    applyStepperAcceleration(accelerationStepsPerSec2);
    Serial.printf("Acceleration set to %u steps/s²\n", accelerationStepsPerSec2);

    // Save settings only if not during initialization
    if (!isInitializing) {
        saveSettings();
    }

    // Publish status update for acceleration change
    systemStatus.publishStatusUpdate(StatusUpdateType::ACCELERATION_CHANGED, accelerationStepsPerSec2);

    // Report warning if acceleration was adjusted (use efficient string building)
    if (accelWasAdjusted && !isInitializing) {
        char warningMsg[150];
        if (speedVariationEnabled && speedVariationStrength > 0.0f) {
            const uint32_t minRequiredAcceleration = calculateRequiredAccelerationForVariableSpeed();
            if (minRequiredAcceleration > 0 && accelerationStepsPerSec2 >= minRequiredAcceleration) {
                // Adjusted due to variable speed requirements
                snprintf(warningMsg, sizeof(warningMsg),
                        "Acceleration auto-adjusted from %u to %u steps/s² due to variable speed modulation requirements",
                        originalRequestedAccel, accelerationStepsPerSec2);
            } else {
                // Adjusted due to range limits
                snprintf(warningMsg, sizeof(warningMsg),
                        "Acceleration auto-adjusted from %u to %u steps/s² (allowed range: 100-100000)",
                        originalRequestedAccel, accelerationStepsPerSec2);
            }
        } else {
            // Adjusted due to range limits
            snprintf(warningMsg, sizeof(warningMsg),
                    "Acceleration auto-adjusted from %u to %u steps/s² (allowed range: 100-100000)",
                    originalRequestedAccel, accelerationStepsPerSec2);
        }
        systemStatus.sendNotification(NotificationType::WARNING, String(warningMsg));
    }
    // Success is indicated by the status update - no notification needed for normal success
}

void StepperController::saveSettings() {
    if (preferences.begin("stepper", false)) {
        preferences.putFloat("speed", setpointRPM);
        preferences.putBool("clockwise", clockwise);
        preferences.putInt("microsteps", MICRO_STEPS);
        preferences.putInt("current", runCurrent);
        preferences.putUInt("acceleration", setpointAcceleration);
        preferences.end();
        Serial.println("Settings saved to flash");
    } else {
        Serial.println("Failed to open preferences for saving");
    }
}

void StepperController::loadSettings() {
    if (preferences.begin("stepper", true)) {
        setpointRPM = preferences.getFloat("speed", setpointRPM);
        clockwise = preferences.getBool("clockwise", clockwise);
        // microSteps is now a define, just read the value for compatibility but don't use it
        preferences.getInt("microsteps", MICRO_STEPS);
        runCurrent = preferences.getInt("current", runCurrent);
        setpointAcceleration = preferences.getUInt("acceleration", setpointAcceleration);
        preferences.end();
        Serial.printf("Settings loaded from flash: %.2f RPM, %s, %d microsteps, %d%% current, %u accel\n",
                      setpointRPM, clockwise ? "CW" : "CCW", MICRO_STEPS, runCurrent, setpointAcceleration);
    } else {
        Serial.println("Failed to open preferences for loading, using defaults");
        // Default values are already set in constructor
    }
}

// Thread-safe public interface using command queue
// Public command methods removed - use SystemCommand singleton directly

void StepperController::setSpeedVariationInternal(float strength) {
    // Validate strength (0.0 to 1.0)
    if (strength < 0.0f || strength > 1.0f) {
        systemStatus.publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_STRENGTH_CHANGED, speedVariationStrength);
        systemStatus.sendNotification(NotificationType::ERROR, 
                    "Speed variation strength out of range (0.0-1.0)");
        return;
    }
    
    if (!stepper) {
        systemStatus.publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_STRENGTH_CHANGED, speedVariationStrength);
        systemStatus.sendNotification(NotificationType::ERROR, "Stepper not initialized");
        return;
    }
    
    speedVariationStrength = strength;
    
    // Update k and k0 parameters for new strength first
    updateSpeedVariationParameters();
    
    // Update speed constraints based on new variation strength
    updateSpeedForVariableSpeed();
    
    // Always update acceleration when variation strength changes, regardless of whether variation is currently enabled
    // This ensures consistent behavior and prepares for potential variation enabling
    updateAccelerationForVariableSpeed();
    
    Serial.printf("Speed variation strength set to %.2f (%.0f%%) - k=%.3f, k0=%.3f\n", 
                  strength, strength * 100.0f, speedVariationK, speedVariationK0);
    Serial.printf("Max allowed base speed: %.2f RPM (setpoint: %.2f RPM)\n", 
                  calculateMaxAllowedBaseSpeed(), setpointRPM);
    
    // Publish status update for speed variation strength change
    systemStatus.publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_STRENGTH_CHANGED, strength);
    // Success is indicated by the status update - no notification needed
}

void StepperController::setSpeedVariationPhaseInternal(float phase) {
    // Optimize phase normalization using fmod
    speedVariationPhase = fmodf(phase + (phase < 0.0f ? 6.28318530718f : 0.0f), 6.28318530718f);
    
    Serial.printf("Speed variation phase set to %.2f radians (%.0f degrees)\n", 
                  speedVariationPhase, speedVariationPhase * 180.0f / PI);
    
    // Publish status update for speed variation phase change
    systemStatus.publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_PHASE_CHANGED, speedVariationPhase);
    // Success is indicated by the status update - no notification needed
}

void StepperController::enableSpeedVariationInternal() {
    if (!stepper) {
        systemStatus.publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_ENABLED_CHANGED, speedVariationEnabled);
        systemStatus.sendNotification(NotificationType::ERROR, "Stepper not initialized");
        return;
    }
    
    // Update speed constraints based on current variation strength
    updateSpeedForVariableSpeed();
    
    // Set current position as the reference point for variation
    speedVariationStartPosition = stepper->getCurrentPosition();
    speedVariationEnabled = true;
    
    // Reset phase offset to 0 when re-enabling variable speed
    speedVariationPhase = 0.0f;
    
    // Dynamically calculate and apply required acceleration for variable speed
    updateAccelerationForVariableSpeed();
    
    Serial.printf("Speed variation enabled at position %d (strength: %.0f%%, phase: 0°)\n", 
                  static_cast<int>(speedVariationStartPosition), speedVariationStrength * 100.0f);
    Serial.printf("Max allowed base speed: %.2f RPM (setpoint: %.2f RPM)\n", 
                  calculateMaxAllowedBaseSpeed(), setpointRPM);
    Serial.println("Current position will be the fastest point in the cycle (new algorithm)");
    
    // Publish status update for speed variation enabled change
    systemStatus.publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_ENABLED_CHANGED, true);
    systemStatus.publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_PHASE_CHANGED, speedVariationPhase);
    systemStatus.publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_STRENGTH_CHANGED, speedVariationStrength);
    // Success is indicated by the status updates - no notification needed
}

void StepperController::disableSpeedVariationInternal() {
    if (!stepper) {
        systemStatus.publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_ENABLED_CHANGED, speedVariationEnabled);
        systemStatus.sendNotification(NotificationType::ERROR, "Stepper not initialized");
        return;
    }
    
    speedVariationEnabled = false;
    
    // Always reset to base speed when disabling variation, regardless of motor state
    // The stepper library will handle the case when motor is disabled
    applyStepperSetpointSpeed(setpointRPM);
    
    Serial.println("Speed variation disabled, returned to constant speed");
    Serial.println("Note: Acceleration remains at current setting for normal operation");
    
    // Publish status update for speed variation enabled change
    systemStatus.publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_ENABLED_CHANGED, false);
    // Success is indicated by the status update - no notification needed
}

void StepperController::requestAllStatusInternal() {
    Serial.println("Publishing all current status values...");
    
    // Publish all current status values through the status update queue
    systemStatus.publishStatusUpdate(StatusUpdateType::SPEED_SETPOINT_CHANGED, setpointRPM);
    systemStatus.publishStatusUpdate(StatusUpdateType::DIRECTION_CHANGED, clockwise);
    systemStatus.publishStatusUpdate(StatusUpdateType::ENABLED_CHANGED, motorEnabled);
    systemStatus.publishStatusUpdate(StatusUpdateType::CURRENT_CHANGED, runCurrent);
    systemStatus.publishStatusUpdate(StatusUpdateType::ACCELERATION_CHANGED, setpointAcceleration);
    
    // Speed variation status
    systemStatus.publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_ENABLED_CHANGED, speedVariationEnabled);
    systemStatus.publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_STRENGTH_CHANGED, speedVariationStrength);
    systemStatus.publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_PHASE_CHANGED, speedVariationPhase);
    
    // StallGuard status
    systemStatus.publishStatusUpdate(StatusUpdateType::STALLGUARD_THRESHOLD_CHANGED, static_cast<int>(stallGuardThreshold));
    publishStallGuardResult(); // Publish current StallGuard result
    
    publishTotalRevolutions();
    publishRuntime();
    publishTMC2209Communication();
    publishTMC2209Temperature(); // Add temperature status to full status request
    publishStallDetection();
    publishStallGuardResult();
}

void StepperController::publishStallGuardResult() {
    if (!tmc2209Initialized) {
        return; // Skip silently if TMC2209 not initialized
    }
    
    // Read StallGuard result from TMC2209 (0-510 per datasheet)
    uint16_t sgResult = stepperDriver.getStallGuardResult();
    
    // Publish StallGuard result update directly (no need to cache)
    systemStatus.publishStatusUpdate(StatusUpdateType::STALLGUARD_RESULT_UPDATE, static_cast<int>(sgResult));
}

// Speed variation helper methods
// Pattern: calculate*() methods perform calculations, update*() methods apply changes only when necessary
// - calculateVariableSpeed(): Calculate current variable speed based on position
// - calculateMaxAllowedBaseSpeed(): Calculate maximum base speed for current variation
// - calculateRequiredAccelerationForVariableSpeed(): Calculate required acceleration
// - updateSpeedForVariableSpeed(): Update speed only if constraints require reduction
// - updateAccelerationForVariableSpeed(): Update acceleration only if it differs from current setting
// - updateSpeedVariationParameters(): Update internal k and k0 parameters
// Note: Both update methods only apply changes when actually needed for optimal performance

float StepperController::calculateVariableSpeed() const {
    if (!speedVariationEnabled || speedVariationStrength == 0.0f) {
        return setpointRPM;
    }
    
    // Calculate current angle in the rotation cycle
    const float angle = getPositionAngle() + speedVariationPhase;
    
    // Normalize angle to 0-2π (optimized - single modulo operation)
    const float normalizedAngle = fmodf(angle + (angle < 0.0f ? 6.28318530718f : 0.0f), 6.28318530718f);
    
    // Use precomputed k and k0 values for efficiency
    // Apply the formula: w(a) = w0 * k0 * 1/(1 + k*cos(a))
    const float denominator = 1.0f + speedVariationK * cosf(normalizedAngle);
    const float variableSpeed = setpointRPM * speedVariationK0 / denominator;
    
    // Ensure we don't go below minimum or above maximum speed
    return constrain(variableSpeed, MIN_SPEED_RPM, MAX_SPEED_RPM);
}

float StepperController::getPositionAngle() const {
    if (!stepper) return 0.0f;
    
    // Calculate position relative to where speed variation was enabled
    const int32_t relativePosition = stepper->getCurrentPosition() - speedVariationStartPosition;
    
    // Convert position to angle using optimized calculation
    // (2π * relativePosition) / (STEPS_PER_REVOLUTION * MICRO_STEPS * GEAR_RATIO)
    float stepsPerOutputRev = static_cast<float>(STEPS_PER_REVOLUTION * MICRO_STEPS * GEAR_RATIO);
    
    return (6.28318530718f * static_cast<float>(relativePosition)) / stepsPerOutputRev;
}

uint32_t StepperController::calculateRequiredAccelerationForVariableSpeed() const {
    if (!speedVariationEnabled || speedVariationStrength == 0.0f) {
        return 0; // No special acceleration needed
    }
    
    // Use precomputed k and k0 values for efficiency
    // Calculate the minimum and maximum speeds using the new formula
    // w(a) = w0 * k0 * 1/(1 + k*cos(a))
    // Minimum speed occurs when cos(a) = 1: w_min = w0 * k0 / (1 + k)
    // Maximum speed occurs when cos(a) = -1: w_max = w0 * k0 / (1 - k)
    const float k0_over_1_plus_k = speedVariationK0 / (1.0f + speedVariationK);
    const float k0_over_1_minus_k = speedVariationK0 / (1.0f - speedVariationK);
    
    const float minSpeed = constrain(setpointRPM * k0_over_1_plus_k, MIN_SPEED_RPM, MAX_SPEED_RPM);
    const float maxSpeed = constrain(setpointRPM * k0_over_1_minus_k, MIN_SPEED_RPM, MAX_SPEED_RPM);
    
    // Calculate the maximum speed change and required acceleration
    const float maxSpeedChange = maxSpeed - minSpeed;
    const uint32_t maxSpeedChangeSteps = rpmToStepsPerSecond(maxSpeedChange);
    
    // Calculate time for half a rotation at current RPM: 30/setpointRPM seconds
    const float halfRotationTime = 30.0f / setpointRPM;
    
    // The maximum speed change occurs over half a rotation with 50% safety margin
    const uint32_t requiredAcceleration = static_cast<uint32_t>((maxSpeedChangeSteps / halfRotationTime) * 1.5f);
    
    Serial.printf("Variable speed acceleration calculation (optimized):\n");
    Serial.printf("  External strength: %.2f (%.0f%%), Internal k: %.3f, k0: %.3f\n", 
                  speedVariationStrength, speedVariationStrength * 100.0f, speedVariationK, speedVariationK0);
    Serial.printf("  Base RPM: %.2f, Speed range: %.2f - %.2f RPM (Δ%.2f RPM)\n", 
                  setpointRPM, minSpeed, maxSpeed, maxSpeedChange);
    Serial.printf("  Half rotation time: %.3f seconds\n", halfRotationTime);
    Serial.printf("  Max speed change: %u steps/s over %.3fs\n", maxSpeedChangeSteps, halfRotationTime);
    Serial.printf("  Required acceleration: %u steps/s² (with 50%% safety margin)\n", requiredAcceleration);
    
    return requiredAcceleration;
}

void StepperController::updateAccelerationForVariableSpeed() {
    if (!stepper || !speedVariationEnabled) {
        return;
    }
    
    uint32_t requiredAcceleration = calculateRequiredAccelerationForVariableSpeed();
    
    if (requiredAcceleration == 0) {
        // No variable speed active, no need to change acceleration
        return;
    }
    
    // Only update acceleration if the current setting is too small for variable speed operation
    if (requiredAcceleration > setpointAcceleration) {
        applyStepperAcceleration(requiredAcceleration);
        Serial.printf("Acceleration increased to %u steps/s² for variable speed operation\n", requiredAcceleration);
    }
}

void StepperController::updateMotorSpeed() {
    if (!stepper || !motorEnabled || !speedVariationEnabled) {
        return;
    }
    
    // Calculate current variable speed
    float currentVariableSpeed = calculateVariableSpeed();
    
    stepperSetSpeed(currentVariableSpeed);
}

void StepperController::updateSpeedForVariableSpeed() {
    if (!stepper) {
        return;
    }
    
    // Calculate maximum allowed base speed to ensure modulated speed doesn't exceed MAX_SPEED_RPM
    float maxAllowedBaseSpeed = calculateMaxAllowedBaseSpeed();
    
    // Only update if current speed exceeds the maximum allowed
    if (setpointRPM > maxAllowedBaseSpeed) {
        float oldSpeed = setpointRPM;
        setpointRPM = maxAllowedBaseSpeed;
        applyStepperSetpointSpeed(maxAllowedBaseSpeed);
        Serial.printf("Base speed reduced from %.2f to %.2f RPM to prevent exceeding max speed with modulation\n", oldSpeed, setpointRPM);
    }
}

// Speed variation control (thread-safe via command queue)
// Additional public command methods removed - use SystemCommand singleton directly

void StepperController::updateSpeedVariationParameters() {
    // Scale external strength (0-1) to internal k parameter
    // External strength of 1.0 maps to internal k = 3/5 = 0.6
    speedVariationK = speedVariationStrength * 0.6f;
    
    // Calculate compensation factor k0 = sqrt(1 - k²)
    // This ensures the average speed remains w0
    speedVariationK0 = sqrtf(1.0f - speedVariationK * speedVariationK);
}

float StepperController::calculateMaxAllowedBaseSpeed() const {
    if (speedVariationStrength == 0.0f) {
        return MAX_SPEED_RPM; // No modulation, full speed allowed
    }
    
    // Calculate max allowed base speed so that the peak modulated speed doesn't exceed MAX_SPEED_RPM
    // Maximum speed occurs when cos(a) = -1: w_max = w0 * k0 / (1 - k)
    // Solving for w0: w0 = w_max * (1 - k) / k0
    // Where w_max should not exceed MAX_SPEED_RPM
    
    float maxAllowedBaseSpeed = MAX_SPEED_RPM * (1.0f - speedVariationK) / speedVariationK0;
    
    // Ensure it's at least the minimum speed
    maxAllowedBaseSpeed = max(maxAllowedBaseSpeed, MIN_SPEED_RPM);
    
    return maxAllowedBaseSpeed;
}

void StepperController::setStallGuardThresholdInternal(uint8_t threshold) {
    // Validate threshold (0-63 per TMC2209 datasheet)
    if (threshold > 63) {
        systemStatus.publishStatusUpdate(StatusUpdateType::STALLGUARD_THRESHOLD_CHANGED, stallGuardThreshold);
        systemStatus.sendNotification(NotificationType::ERROR, "StallGuard threshold out of range (0-63)");
        return;
    }
    
    if (!tmc2209Initialized) {
        systemStatus.publishStatusUpdate(StatusUpdateType::STALLGUARD_THRESHOLD_CHANGED, stallGuardThreshold);
        systemStatus.sendNotification(NotificationType::ERROR, "TMC2209 not initialized - cannot set StallGuard threshold");
        return;
    }
    
    stallGuardThreshold = threshold;
    stepperDriver.setStallGuardThreshold(threshold);
    
    Serial.printf("StallGuard threshold set to %d (0=most sensitive, 63=least sensitive)\n", threshold);
    
    // Publish status update for StallGuard threshold change
    systemStatus.publishStatusUpdate(StatusUpdateType::STALLGUARD_THRESHOLD_CHANGED, threshold);
}

bool StepperController::setStallGuardThreshold(uint8_t threshold) {
    StepperCommandData cmd(StepperCommand::SET_STALLGUARD_THRESHOLD, static_cast<int>(threshold));
    return systemCommand.sendCommand(cmd);
}
