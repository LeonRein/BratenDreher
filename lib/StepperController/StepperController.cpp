// Always include the header first to ensure class/type declarations
#include "StepperController.h"

void StepperController::applyStepperSetpointSpeed(float rpm) {
    if (!stepper) {
        Serial.println("WARNING: Cannot apply setpoint speed - stepper not initialized");
        return;
    }

    stepperSetSpeed(rpm);

    setpointRPM = rpm;

    publishStatusUpdate(StatusUpdateType::SPEED_SETPOINT_CHANGED, rpm);
}

void StepperController::applyStepperAcceleration(uint32_t accelerationStepsPerSec2) {
    if (!stepper) {
        Serial.println("WARNING: Cannot apply acceleration - stepper not initialized");
        return;
    }
    stepperSetAcceleration(accelerationStepsPerSec2);

    setpointAcceleration = accelerationStepsPerSec2;

    publishStatusUpdate(StatusUpdateType::ACCELERATION_CHANGED, accelerationStepsPerSec2);
}

void StepperController::applyRunClockwise() {
    if (!stepper) {
        Serial.println("WARNING: Cannot set direction - stepper not initialized");
        return;
    }

    if(!motorEnabled) {
        publishTMC2209Communication();
        stepperDriver.enable();
        motorEnabled = true;
        publishStatusUpdate(StatusUpdateType::ENABLED_CHANGED, true);
    }
    
    stepper->runForward(); // In FastAccelStepper, backward means clockwise
    clockwise = true;

    publishStatusUpdate(StatusUpdateType::DIRECTION_CHANGED, clockwise);
}

void StepperController::applyRunCounterClockwise() {
    if (!stepper) {
        Serial.println("WARNING: Cannot set direction - stepper not initialized");
        return;
    }

    if(!motorEnabled) {
        stepperDriver.enable();
        publishTMC2209Communication();
        motorEnabled = true;
        publishStatusUpdate(StatusUpdateType::ENABLED_CHANGED, true);
    }
    
    stepper->runBackward(); // In FastAccelStepper, backward means counter-clockwise
    clockwise = false;

    publishStatusUpdate(StatusUpdateType::DIRECTION_CHANGED, clockwise);
}

void StepperController::applyStop() {
    if (!stepper) {
        Serial.println("WARNING: Cannot stop stepper - not initialized");
        return;
    }

    stepper->stopMove();
    motorEnabled = false;
    publishTMC2209Communication();
    stepperDriver.disable();

    publishStatusUpdate(StatusUpdateType::ENABLED_CHANGED, false);
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

    publishStatusUpdate(StatusUpdateType::CURRENT_CHANGED, current);
}

void StepperController::publishTMC2209Communication() {
    bool isCommunicating = stepperDriver.isSetupAndCommunicating();
    
    if (!isCommunicating) {
        tmc2209Initialized = false;
        publishStatusUpdate(StatusUpdateType::TMC2209_STATUS_UPDATE, false);
        sendNotification(NotificationType::ERROR, "TMC2209 driver not initialized or not communicating");
    } else {
        tmc2209Initialized = true;
        publishStatusUpdate(StatusUpdateType::TMC2209_STATUS_UPDATE, true);
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
            lastStallTime = millis();
            stallCount++;
            Serial.printf("STALL DETECTED! Count: %d, Time: %lu\n", stallCount, lastStallTime);
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
    publishStatusUpdate(StatusUpdateType::STALL_DETECTED_UPDATE, stallDetected);
    publishStatusUpdate(StatusUpdateType::STALL_COUNT_UPDATE, static_cast<int>(stallCount));
}


void StepperController::publishCurrentRPM() {
    if (!stepper) {
        publishStatusUpdate(StatusUpdateType::SPEED_UPDATE, 0.0f);
        return;
    }

    float currentStepsPerSecond = static_cast<float>(abs(stepper->getCurrentSpeedInMilliHz() / 1000)) ;

    float rpm = (currentStepsPerSecond * 60.0f) /
        (static_cast<float>(GEAR_RATIO) * static_cast<float>(STEPS_PER_REVOLUTION) * static_cast<float>(MICRO_STEPS));

    publishStatusUpdate(StatusUpdateType::SPEED_UPDATE, rpm);
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
      stepper(nullptr), serialStream(Serial2), setpointRPM(1.0f),
      runCurrent(30), motorEnabled(false), clockwise(true),
      startTime(0), totalMicroSteps(0), isFirstStart(true), tmc2209Initialized(false),
      stallDetected(false), lastStallTime(0), stallCount(0),
      setpointAcceleration(0),  // Will be set during initialization
      speedVariationEnabled(false), speedVariationStrength(0.0f), speedVariationPhase(0.0f), speedVariationStartPosition(0),
      speedVariationK(0.0f), speedVariationK0(1.0f)  // Initialize with default values
{
    // Create command queue for thread-safe operation
    commandQueue = xQueueCreate(COMMAND_QUEUE_SIZE, sizeof(StepperCommandData));
    if (commandQueue == nullptr) {
        Serial.println("ERROR: Failed to create stepper command queue!");
    }
    
    // Create notification queue for warnings and errors only
    notificationQueue = xQueueCreate(NOTIFICATION_QUEUE_SIZE, sizeof(NotificationData));
    if (notificationQueue == nullptr) {
        Serial.println("ERROR: Failed to create stepper notification queue!");
    }
    
    // Create status update queue for thread-safe status communication
    statusUpdateQueue = xQueueCreate(STATUS_UPDATE_QUEUE_SIZE, sizeof(StatusUpdateData));
    if (statusUpdateQueue == nullptr) {
        Serial.println("ERROR: Failed to create stepper status update queue!");
    }
}

StepperController::~StepperController() {
    if (commandQueue != nullptr) {
        vQueueDelete(commandQueue);
    }
    if (notificationQueue != nullptr) {
        vQueueDelete(notificationQueue);
    }
    if (statusUpdateQueue != nullptr) {
        vQueueDelete(statusUpdateQueue);
    }
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
    
    // Enable driver (will be controlled via FastAccelStepper)
    digitalWrite(TMC_EN_PIN, LOW);
    
    // Initialize TMC2209
    stepperDriver.setup(serialStream, 115200, TMC2209::SERIAL_ADDRESS_0, TMC_RX_PIN, TMC_TX_PIN);
    
    // Load saved settings
    loadSettings();
    
    // Configure driver with loaded settings
    configureDriver();
    
    // Check TMC2209 initialization status after configuration
    tmc2209Initialized = stepperDriver.isSetupAndCommunicating();
    
    // Publish initial TMC2209 status
    StatusUpdateData statusData;
    statusData.type = StatusUpdateType::TMC2209_STATUS_UPDATE;
    // timestamp field removed
    statusData.boolValue = tmc2209Initialized;
    xQueueSend(statusUpdateQueue, &statusData, 0);
    
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
    
    // Batch publish initial status updates for loaded settings
    StatusUpdateData initialUpdates[7];  // Pre-allocate array
    initialUpdates[0] = StatusUpdateData(StatusUpdateType::DIRECTION_CHANGED, clockwise);
    initialUpdates[1] = StatusUpdateData(StatusUpdateType::CURRENT_CHANGED, runCurrent);
    initialUpdates[2] = StatusUpdateData(StatusUpdateType::ENABLED_CHANGED, false); // Initially disabled
    initialUpdates[3] = StatusUpdateData(StatusUpdateType::SPEED_VARIATION_ENABLED_CHANGED, speedVariationEnabled);
    initialUpdates[4] = StatusUpdateData(StatusUpdateType::SPEED_VARIATION_STRENGTH_CHANGED, speedVariationStrength);
    initialUpdates[5] = StatusUpdateData(StatusUpdateType::SPEED_VARIATION_PHASE_CHANGED, speedVariationPhase);
    initialUpdates[6] = StatusUpdateData(StatusUpdateType::ACCELERATION_CHANGED, defaultAcceleration);
    
    // Batch send all initial updates
    for (uint8_t i = 0; i < 7; i++) {
        xQueueSend(statusUpdateQueue, &initialUpdates[i], 0);
    }
    
    // Initialize speed variation parameters (k and k0)
    updateSpeedVariationParameters();
    
    // Initially disabled
    stepperDriver.disable();
    
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
    stepperDriver.enableAutomaticCurrentScaling();
    
    // Note: StallGuard typically requires SpreadCycle mode (not StealthChop)
    // We'll enable StealthChop for quiet operation but may need to disable it for stall detection
    stepperDriver.enableStealthChop();
    stepperDriver.setCoolStepDurationThreshold(5000);
    
    // Configure StallGuard for stall detection
    // Setting threshold automatically enables StallGuard functionality
    stepperDriver.setStallGuardThreshold(10); // Sensitivity: 0 (most sensitive) to 255 (least sensitive)
    
    // Update communication status after configuration
    bool newTmc2209Status = stepperDriver.isSetupAndCommunicating();
    
    // Publish TMC2209 status update if communication status changed
    if (newTmc2209Status != tmc2209Initialized) {
        publishStatusUpdate(StatusUpdateType::TMC2209_STATUS_UPDATE, newTmc2209Status);
    }
    tmc2209Initialized = newTmc2209Status;
    
    // Check if driver is communicating properly
    if (tmc2209Initialized) {
        Serial.printf("TMC2209 configured: %d microsteps, %d%% current, StallGuard threshold: 10\n", MICRO_STEPS, runCurrent);
        Serial.println("Note: StallGuard may require disabling StealthChop for optimal detection");
    } else {
        Serial.println("WARNING: TMC2209 driver not responding during configuration");
    }
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
    
    // Initialize stepper controller
    if (!begin()) {
        Serial.println("Failed to initialize stepper controller!");
        return;
    }
    
    Serial.println("Stepper Controller initialized successfully!");
    
    // Initialize timing variables with cached millis() value
    StepperCommandData cmd;
    uint32_t currentTime = millis();
    unsigned long nextSpeedUpdate = currentTime + MOTOR_SPEED_UPDATE_INTERVAL;
    unsigned long nextPeriodicUpdate = currentTime + STATUS_UPDATE_INTERVAL;
    
    // Main event loop
    while (true) {
        // Calculate smart timeout for queue operations (use the earliest upcoming event)
        const unsigned long nextEvent = min(nextPeriodicUpdate, nextSpeedUpdate);
        const TickType_t timeout = calculateQueueTimeout(nextEvent);
        
        // Block on queue until command arrives or any update is due
        if (xQueueReceive(commandQueue, &cmd, timeout) == pdTRUE) {
            processCommand(cmd);
        }
        
        // Cache millis() value to avoid multiple system calls
        currentTime = millis();
        
        // Perform periodic status updates if due
        if (isUpdateDue(nextPeriodicUpdate)) {
            publishPeriodicStatusUpdates();
            nextPeriodicUpdate = currentTime + STATUS_UPDATE_INTERVAL;
        }
        
        // Update motor speed if due (more frequent for smooth variation)
        if (isUpdateDue(nextSpeedUpdate)) {
            updateMotorSpeed();
            nextSpeedUpdate = currentTime + MOTOR_SPEED_UPDATE_INTERVAL;
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
    publishStatusUpdate(StatusUpdateType::TOTAL_REVOLUTIONS_UPDATE, totalRevolutions);
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

    publishStatusUpdate(StatusUpdateType::RUNTIME_UPDATE, runtime);
}

void StepperController::publishPeriodicStatusUpdates() {
    if (!stepper) {
        Serial.println("WARNING: Cannot publish status updates - stepper not initialized");
        return;
    }
    
    publishCurrentRPM();
    publishTotalRevolutions();
    publishRuntime();
}

void StepperController::processCommand(const StepperCommandData& cmd) {
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
    lastStallTime = 0;
    
    Serial.println("Stall count reset");
    publishStatusUpdate(StatusUpdateType::STALL_COUNT_UPDATE, stallCount);
    // Success is indicated by the status update - no notification needed
}

void StepperController::setSpeedInternal(float rpm) {
    // Store original requested speed for potential warning message
    const float originalRequestedSpeed = rpm;
    bool speedWasAdjusted = false;
    
    if (!stepper) {
        publishStatusUpdate(StatusUpdateType::SPEED_SETPOINT_CHANGED, setpointRPM);
        sendNotification(NotificationType::ERROR, "Stepper not initialized");
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
        sendNotification(NotificationType::WARNING, String(warningMsg));
    }
    // Success is indicated by the status update - no notification needed for normal success
}

void StepperController::setDirectionInternal(bool clockwise) {
    if (!stepper) {
        publishStatusUpdate(StatusUpdateType::DIRECTION_CHANGED, clockwise);
        sendNotification(NotificationType::ERROR, "Stepper not initialized");
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
        publishStatusUpdate(StatusUpdateType::ENABLED_CHANGED, motorEnabled);
        sendNotification(NotificationType::ERROR, "Stepper not initialized");
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
        publishStatusUpdate(StatusUpdateType::ENABLED_CHANGED, false);
        sendNotification(NotificationType::ERROR, "Stepper not initialized");
        return;
    }

    stepper->forceStopAndNewPosition(stepper->getCurrentPosition());
    
    stepperDriver.disable();
    motorEnabled = false;
    
    Serial.println("EMERGENCY STOP executed");
    
    publishStatusUpdate(StatusUpdateType::ENABLED_CHANGED, false);
}

void StepperController::setRunCurrentInternal(int current) {
    // Validate current (10-100%)
    if (current < 10 || current > 100) {
        publishStatusUpdate(StatusUpdateType::CURRENT_CHANGED, runCurrent);
        sendNotification(NotificationType::ERROR, "Current out of range (10-100%)");
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
        publishStatusUpdate(StatusUpdateType::ACCELERATION_CHANGED, setpointAcceleration);
        sendNotification(NotificationType::ERROR, "Stepper not initialized");
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
    publishStatusUpdate(StatusUpdateType::ACCELERATION_CHANGED, accelerationStepsPerSec2);

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
        sendNotification(NotificationType::WARNING, String(warningMsg));
    }
    // Success is indicated by the status update - no notification needed for normal success
}

void StepperController::sendNotification(NotificationType type, const String& message) {
    if (notificationQueue == nullptr) return;
    
    NotificationData notificationData;
    notificationData.type = type;
    
    // Use efficient string copying without temporary String objects
    const char* msgPtr = message.c_str();
    const size_t msgLen = message.length();
    const size_t maxLen = sizeof(notificationData.message) - 1;
    
    if (msgLen <= maxLen) {
        memcpy(notificationData.message, msgPtr, msgLen);
        notificationData.message[msgLen] = '\0';
    } else {
        memcpy(notificationData.message, msgPtr, maxLen);
        notificationData.message[maxLen] = '\0';
    }
    
    // Non-blocking send to avoid task delays
    xQueueSend(notificationQueue, &notificationData, 0);
}

bool StepperController::getNotification(NotificationData& notification) {
    if (notificationQueue == nullptr) return false;
    
    return xQueueReceive(notificationQueue, &notification, 0) == pdTRUE; // Non-blocking
}

bool StepperController::getStatusUpdate(StatusUpdateData& status) {
    if (statusUpdateQueue == nullptr) return false;
    
    return xQueueReceive(statusUpdateQueue, &status, 0) == pdTRUE; // Non-blocking
}

void StepperController::publishStatusUpdate(StatusUpdateType type, float value) {
    if (statusUpdateQueue == nullptr) return;
    
    StatusUpdateData statusData(type, value);
    // Don't block if queue is full - just drop the update
    xQueueSend(statusUpdateQueue, &statusData, 0);
}

void StepperController::publishStatusUpdate(StatusUpdateType type, bool value) {
    if (statusUpdateQueue == nullptr) return;
    
    StatusUpdateData statusData(type, value);
    xQueueSend(statusUpdateQueue, &statusData, 0);
}

void StepperController::publishStatusUpdate(StatusUpdateType type, int value) {
    if (statusUpdateQueue == nullptr) return;
    
    StatusUpdateData statusData(type, value);
    xQueueSend(statusUpdateQueue, &statusData, 0);
}

void StepperController::publishStatusUpdate(StatusUpdateType type, uint32_t value) {
    if (statusUpdateQueue == nullptr) return;
    
    StatusUpdateData statusData(type, value);
    xQueueSend(statusUpdateQueue, &statusData, 0);
}

void StepperController::publishStatusUpdate(StatusUpdateType type, unsigned long value) {
    if (statusUpdateQueue == nullptr) return;
    
    StatusUpdateData statusData(type, value);
    xQueueSend(statusUpdateQueue, &statusData, 0);
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
bool StepperController::setSpeed(float rpm) {
    if (commandQueue == nullptr) return false;
    StepperCommandData cmd;
    cmd.command = StepperCommand::SET_SPEED;
    cmd.floatValue = rpm;
    return xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE;
}

bool StepperController::setDirection(bool clockwise) {
    if (commandQueue == nullptr) return false;
    StepperCommandData cmd;
    cmd.command = StepperCommand::SET_DIRECTION;
    cmd.boolValue = clockwise;
    return xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE;
}

bool StepperController::enable() {
    if (commandQueue == nullptr) return false;
    StepperCommandData cmd;
    cmd.command = StepperCommand::ENABLE;
    return xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE;
}

bool StepperController::disable() {
    if (commandQueue == nullptr) return false;
    StepperCommandData cmd;
    cmd.command = StepperCommand::DISABLE;
    return xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE;
}

bool StepperController::emergencyStop() {
    if (commandQueue == nullptr) return false;
    StepperCommandData cmd;
    cmd.command = StepperCommand::EMERGENCY_STOP;
    return xQueueSend(commandQueue, &cmd, 0) == pdTRUE;
}

bool StepperController::setRunCurrent(int current) {
    if (commandQueue == nullptr) return false;
    StepperCommandData cmd;
    cmd.command = StepperCommand::SET_CURRENT;
    cmd.intValue = current;
    return xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE;
}

bool StepperController::setAcceleration(uint32_t accelerationStepsPerSec2) {
    if (commandQueue == nullptr) return false;
    StepperCommandData cmd;
    cmd.command = StepperCommand::SET_ACCELERATION;
    cmd.intValue = accelerationStepsPerSec2;
    return xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE;
}

bool StepperController::resetCounters() {
    if (commandQueue == nullptr) return false;
    StepperCommandData cmd;
    cmd.command = StepperCommand::RESET_COUNTERS;
    return xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE;
}

bool StepperController::resetStallCount() {
    if (commandQueue == nullptr) return false;
    StepperCommandData cmd;
    cmd.command = StepperCommand::RESET_STALL_COUNT;
    return xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE;
}

void StepperController::setSpeedVariationInternal(float strength) {
    // Validate strength (0.0 to 1.0)
    if (strength < 0.0f || strength > 1.0f) {
        publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_STRENGTH_CHANGED, speedVariationStrength);
        sendNotification(NotificationType::ERROR, 
                    "Speed variation strength out of range (0.0-1.0)");
        return;
    }
    
    if (!stepper) {
        publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_STRENGTH_CHANGED, speedVariationStrength);
        sendNotification(NotificationType::ERROR, "Stepper not initialized");
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
    publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_STRENGTH_CHANGED, strength);
    // Success is indicated by the status update - no notification needed
}

void StepperController::setSpeedVariationPhaseInternal(float phase) {
    // Optimize phase normalization using fmod
    speedVariationPhase = fmodf(phase + (phase < 0.0f ? 6.28318530718f : 0.0f), 6.28318530718f);
    
    Serial.printf("Speed variation phase set to %.2f radians (%.0f degrees)\n", 
                  speedVariationPhase, speedVariationPhase * 180.0f / PI);
    
    // Publish status update for speed variation phase change
    publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_PHASE_CHANGED, speedVariationPhase);
    // Success is indicated by the status update - no notification needed
}

void StepperController::enableSpeedVariationInternal() {
    if (!stepper) {
        publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_ENABLED_CHANGED, speedVariationEnabled);
        sendNotification(NotificationType::ERROR, "Stepper not initialized");
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
    
    Serial.printf("Speed variation enabled at position %ld (strength: %.0f%%, phase: 0°)\n", 
                  speedVariationStartPosition, speedVariationStrength * 100.0f);
    Serial.printf("Max allowed base speed: %.2f RPM (setpoint: %.2f RPM)\n", 
                  calculateMaxAllowedBaseSpeed(), setpointRPM);
    Serial.println("Current position will be the fastest point in the cycle (new algorithm)");
    
    // Publish status update for speed variation enabled change
    publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_ENABLED_CHANGED, true);
    publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_PHASE_CHANGED, speedVariationPhase);
    publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_STRENGTH_CHANGED, speedVariationStrength);
    // Success is indicated by the status updates - no notification needed
}

void StepperController::disableSpeedVariationInternal() {
    if (!stepper) {
        publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_ENABLED_CHANGED, speedVariationEnabled);
        sendNotification(NotificationType::ERROR, "Stepper not initialized");
        return;
    }
    
    speedVariationEnabled = false;
    
    // Always reset to base speed when disabling variation, regardless of motor state
    // The stepper library will handle the case when motor is disabled
    applyStepperSetpointSpeed(setpointRPM);
    
    Serial.println("Speed variation disabled, returned to constant speed");
    Serial.println("Note: Acceleration remains at current setting for normal operation");
    
    // Publish status update for speed variation enabled change
    publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_ENABLED_CHANGED, false);
    // Success is indicated by the status update - no notification needed
}

void StepperController::requestAllStatusInternal() {
    Serial.println("Publishing all current status values...");
    
    // Publish all current status values through the status update queue
    publishStatusUpdate(StatusUpdateType::SPEED_SETPOINT_CHANGED, setpointRPM);
    publishStatusUpdate(StatusUpdateType::DIRECTION_CHANGED, clockwise);
    publishStatusUpdate(StatusUpdateType::ENABLED_CHANGED, motorEnabled);
    publishStatusUpdate(StatusUpdateType::CURRENT_CHANGED, runCurrent);
    publishStatusUpdate(StatusUpdateType::ACCELERATION_CHANGED, setpointAcceleration);
    
    // Speed variation status
    publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_ENABLED_CHANGED, speedVariationEnabled);
    publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_STRENGTH_CHANGED, speedVariationStrength);
    publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_PHASE_CHANGED, speedVariationPhase);
    
    publishTotalRevolutions();
    publishRuntime();
    publishTMC2209Communication();
    publishStallDetection();
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
uint32_t StepperController::setSpeedVariation(float strength) {
    if (commandQueue == nullptr) return 0;
    
    StepperCommandData cmd;
    cmd.command = StepperCommand::SET_SPEED_VARIATION;
    cmd.floatValue = strength;
    
    
    return xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE;
}

uint32_t StepperController::setSpeedVariationPhase(float phase) {
    if (commandQueue == nullptr) return 0;
    
    StepperCommandData cmd;
    cmd.command = StepperCommand::SET_SPEED_VARIATION_PHASE;
    cmd.floatValue = phase;
    
    return xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE;
}

uint32_t StepperController::enableSpeedVariation() {
    if (commandQueue == nullptr) return 0;

    StepperCommandData cmd;
    cmd.command = StepperCommand::ENABLE_SPEED_VARIATION;
    
    return xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE;
}

uint32_t StepperController::disableSpeedVariation() {
    if (commandQueue == nullptr) return 0;
    
    StepperCommandData cmd;
    cmd.command = StepperCommand::DISABLE_SPEED_VARIATION;
    
    return xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE;
}

uint32_t StepperController::requestAllStatus() {
    if (commandQueue == nullptr) return 0;
    
    StepperCommandData cmd;
    cmd.command = StepperCommand::REQUEST_ALL_STATUS;
    
    return xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE;
}

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
