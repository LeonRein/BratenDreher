// Always include the header first to ensure class/type declarations
#include "StepperController.h"

// Centralized stepper hardware control methods
void StepperController::applyStepperSpeed(uint32_t stepsPerSecond) {
    if (!stepper) {
        Serial.println("WARNING: Cannot apply speed - stepper not initialized");
        return;
    }
    // Calculate and set canonical RPM value
    currentSpeedRPM = (static_cast<float>(stepsPerSecond) * 60.0f) /
        (static_cast<float>(GEAR_RATIO) * static_cast<float>(STEPS_PER_REVOLUTION) * static_cast<float>(MICRO_STEPS));
    stepper->setSpeedInHz(stepsPerSecond);
    stepper->applySpeedAcceleration();
    publishStatusUpdate(StatusUpdateType::SPEED_CHANGED, currentSpeedRPM);
}

void StepperController::applyStepperAcceleration(uint32_t accelerationStepsPerSec2) {
    if (!stepper) {
        Serial.println("WARNING: Cannot apply acceleration - stepper not initialized");
        return;
    }
    stepper->setAcceleration(accelerationStepsPerSec2);
    currentAcceleration = accelerationStepsPerSec2;
    stepper->applySpeedAcceleration();
    publishStatusUpdate(StatusUpdateType::ACCELERATION_CHANGED, accelerationStepsPerSec2);
}
#include "StepperController.h"

StepperController::StepperController() 
    : Task("Stepper_Task", 4096, 1, 1), // Task name, 4KB stack, priority 1, core 1
      stepper(nullptr), serialStream(Serial2), currentSpeedRPM(1.0f),
      runCurrent(30), motorEnabled(false), clockwise(true),
      startTime(0), totalSteps(0), isFirstStart(true), tmc2209Initialized(false),
      stallDetected(false), lastStallTime(0), stallCount(0),
      currentAcceleration(0),  // Will be set during initialization
      speedVariationEnabled(false), speedVariationStrength(0.0f), speedVariationPhase(0.0f), speedVariationStartPosition(0),
      speedVariationK(0.0f), speedVariationK0(1.0f),  // Initialize with default values
      /* nextCommandId removed */ {
    
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
    statusData.timestamp = millis();
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
    setSpeedInternal(currentSpeedRPM, 0); // Use commandId 0 for internal calls
    
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
    
    // Check if TMC2209 driver is properly communicating
    if (tmc2209Initialized) {
        Serial.println("FastAccelStepper with TMC2209 initialized successfully");
        Serial.printf("Steps per output revolution: %d\n", TOTAL_STEPS_PER_REVOLUTION);
        Serial.printf("Speed range: %.1f - %.1f RPM\n", MIN_SPEED_RPM, MAX_SPEED_RPM);
        Serial.printf("Microsteps: %d, Run current: %d%%\n", MICRO_STEPS, runCurrent);
    } else {
        Serial.println("WARNING: FastAccelStepper initialized but TMC2209 driver not responding");
        Serial.println("Check TMC2209 wiring and power supply");
    }
    
    return true;
}

bool StepperController::initPreferences() {
    // Try to open the namespace with write permissions to ensure it exists
    if (preferences.begin("stepper", false)) {
        // Check if this is a fresh namespace by looking for a key
        if (!preferences.isKey("speed")) {
            // Fresh namespace - write default values
            Serial.println("Fresh preferences namespace, writing defaults");
            preferences.putFloat("speed", currentSpeedRPM);
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
        StatusUpdateData statusData;
        statusData.type = StatusUpdateType::TMC2209_STATUS_UPDATE;
        statusData.timestamp = millis();
        statusData.boolValue = newTmc2209Status;
        xQueueSend(statusUpdateQueue, &statusData, 0);
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

void StepperController::configureStallDetection(bool enableStealthChop) {
    if (!tmc2209Initialized) {
        Serial.println("Cannot configure stall detection: TMC2209 not initialized");
        return;
    }
    
    if (enableStealthChop) {
        // Quiet operation mode - StallGuard may be less sensitive
        stepperDriver.enableStealthChop();
        Serial.println("StealthChop enabled - quiet operation, reduced stall sensitivity");
    } else {
        // SpreadCycle mode - better for stall detection
        stepperDriver.disableStealthChop();
        Serial.println("StealthChop disabled - improved stall detection, may be noisier");
    }
    
    // Configure StallGuard threshold (can be adjusted based on load)
    stepperDriver.setStallGuardThreshold(10);
    
    // Check communication status after configuration
    bool newTmc2209Status = stepperDriver.isSetupAndCommunicating();
    
    // Publish TMC2209 status update if communication status changed
    if (newTmc2209Status != tmc2209Initialized) {
        StatusUpdateData statusData;
        statusData.type = StatusUpdateType::TMC2209_STATUS_UPDATE;
        statusData.timestamp = millis();
        statusData.boolValue = newTmc2209Status;
        xQueueSend(statusUpdateQueue, &statusData, 0);
        tmc2209Initialized = newTmc2209Status;
    }
    
    Serial.println("StallGuard configured for stall detection");
}

void StepperController::setStallGuardThreshold(uint8_t threshold) {
    if (!tmc2209Initialized) {
        Serial.println("Cannot set StallGuard threshold: TMC2209 not initialized");
        return;
    }
    
    stepperDriver.setStallGuardThreshold(threshold);
    
    // Check communication status after setting threshold
    bool newTmc2209Status = stepperDriver.isSetupAndCommunicating();
    
    // Publish TMC2209 status update if communication status changed
    if (newTmc2209Status != tmc2209Initialized) {
        StatusUpdateData statusData;
        statusData.type = StatusUpdateType::TMC2209_STATUS_UPDATE;
        statusData.timestamp = millis();
        statusData.boolValue = newTmc2209Status;
        xQueueSend(statusUpdateQueue, &statusData, 0);
        tmc2209Initialized = newTmc2209Status;
    }
    
    Serial.printf("StallGuard threshold set to %d (0=most sensitive, 255=least sensitive)\n", threshold);
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

void StepperController::publishPeriodicStatusUpdates() {
    if (statusUpdateQueue == nullptr) return;
    
    // Pre-allocate status update array for batch processing
    StatusUpdateData updates[6];  // Maximum 6 updates per cycle
    uint8_t updateCount = 0;
    
    // Collect all status data in one pass to minimize function calls
    const uint32_t currentTime = millis();
    bool motorIsRunning = false;
    float totalRevolutions = 0.0f;
    
    if (stepper) {
        // Single position read for multiple calculations
        const int32_t currentPosition = stepper->getCurrentPosition();
        // Optimized revolution calculation
        float stepsPerOutputRev = static_cast<float>(STEPS_PER_REVOLUTION * MICRO_STEPS * GEAR_RATIO);
        totalRevolutions = static_cast<float>(currentPosition) / stepsPerOutputRev;
        motorIsRunning = stepper->isRunning();
    }
    
    // Calculate runtime once
    const unsigned long runtime = (!isFirstStart && startTime > 0) ? 
        (currentTime - startTime) / 1000 : 0;
    
    // Update stall detection state efficiently
    const bool diagPinHigh = digitalRead(DIAG_PIN);
    const bool shouldCheckStall = motorEnabled && motorIsRunning;
    bool stallStateChanged = false;
    
    if (shouldCheckStall) {
        if (diagPinHigh && !stallDetected) {
            // New stall detected
            stallDetected = true;
            lastStallTime = currentTime;
            stallCount++;
            stallStateChanged = true;
            Serial.printf("STALL DETECTED! Count: %d, Time: %lu\n", stallCount, lastStallTime);
            Serial.println("Consider: reducing speed, increasing current, or checking load");
        } else if (!diagPinHigh && stallDetected) {
            // Stall cleared
            stallDetected = false;
            stallStateChanged = true;
            Serial.println("Stall condition cleared");
        }
    } else if (stallDetected) {
        // Clear stall status when motor is stopped
        stallDetected = false;
        stallStateChanged = true;
        Serial.println("Stall status cleared (motor stopped)");
    }
    
    // Build status updates array (avoid redundant constructor calls)
    updates[updateCount++] = StatusUpdateData(StatusUpdateType::TOTAL_REVOLUTIONS_UPDATE, totalRevolutions);
    updates[updateCount++] = StatusUpdateData(StatusUpdateType::IS_RUNNING_UPDATE, motorIsRunning && motorEnabled);
    updates[updateCount++] = StatusUpdateData(StatusUpdateType::RUNTIME_UPDATE, runtime);
    updates[updateCount++] = StatusUpdateData(StatusUpdateType::STALL_DETECTED_UPDATE, stallDetected);
    updates[updateCount++] = StatusUpdateData(StatusUpdateType::STALL_COUNT_UPDATE, static_cast<int>(stallCount));
    
    // Only add variable speed update if enabled (avoid unnecessary calculation)
    if (speedVariationEnabled) {
        const float currentVariableSpeed = calculateVariableSpeed();
        updates[updateCount++] = StatusUpdateData(StatusUpdateType::CURRENT_VARIABLE_SPEED_UPDATE, currentVariableSpeed);
    }
    
    // Batch send all updates (reduces queue operation overhead)
    for (uint8_t i = 0; i < updateCount; i++) {
        xQueueSend(statusUpdateQueue, &updates[i], 0);
    }
}

void StepperController::processCommand(const StepperCommandData& cmd) {
    switch (cmd.command) {
        case StepperCommand::SET_SPEED:
            setSpeedInternal(cmd.floatValue, cmd.commandId);
            break;
            
        case StepperCommand::SET_DIRECTION:
            setDirectionInternal(cmd.boolValue, cmd.commandId);
            break;
            
        case StepperCommand::ENABLE:
            enableInternal(cmd.commandId);
            break;
            
        case StepperCommand::DISABLE:
            disableInternal(cmd.commandId);
            break;
            
        case StepperCommand::EMERGENCY_STOP:
            emergencyStopInternal(cmd.commandId);
            break;
            
        case StepperCommand::SET_CURRENT:
            setRunCurrentInternal(cmd.intValue, cmd.commandId);
            break;
            
        case StepperCommand::SET_ACCELERATION:
            setAccelerationInternal(cmd.intValue, cmd.commandId);
            break;
            
        case StepperCommand::RESET_COUNTERS:
            resetCountersInternal(cmd.commandId);
            break;
            
        case StepperCommand::RESET_STALL_COUNT:
            resetStallCountInternal(cmd.commandId);
            break;
            
        case StepperCommand::SET_SPEED_VARIATION:
            setSpeedVariationInternal(cmd.floatValue, cmd.commandId);
            break;
            
        case StepperCommand::SET_SPEED_VARIATION_PHASE:
            setSpeedVariationPhaseInternal(cmd.floatValue, cmd.commandId);
            break;
            
        case StepperCommand::ENABLE_SPEED_VARIATION:
            enableSpeedVariationInternal(cmd.commandId);
            break;
            
        case StepperCommand::DISABLE_SPEED_VARIATION:
            disableSpeedVariationInternal(cmd.commandId);
            break;
            
        case StepperCommand::REQUEST_ALL_STATUS:
            requestAllStatusInternal(cmd.commandId);
            break;
    }
}

void StepperController::resetCountersInternal(uint32_t commandId) {
    if (stepper) {
        // FastAccelStepper setCurrentPosition returns void
        stepper->setCurrentPosition(0);
    }
    totalSteps = 0;
    startTime = millis();
    isFirstStart = false;
    
    Serial.println("Counters reset");
    // Success is indicated by the status update - no notification needed
}

void StepperController::resetStallCountInternal(uint32_t commandId) {
    stallCount = 0;
    stallDetected = false;
    lastStallTime = 0;
    
    Serial.println("Stall count reset");
    publishStatusUpdate(StatusUpdateType::STALL_COUNT_UPDATE, stallCount);
    // Success is indicated by the status update - no notification needed
}

void StepperController::setSpeedInternal(float rpm, uint32_t commandId) {
    // Store original requested speed for potential warning message
    const float originalRequestedSpeed = rpm;
    bool speedWasAdjusted = false;
    
    // Validate input against global limits
    if (rpm < MIN_SPEED_RPM || rpm > MAX_SPEED_RPM) {
        publishStatusUpdate(StatusUpdateType::SPEED_CHANGED, currentSpeedRPM);
        // Use efficient string building for error message
        char errorMsg[80];
        snprintf(errorMsg, sizeof(errorMsg), "Speed out of range (%.1f-%.1f RPM)", MIN_SPEED_RPM, MAX_SPEED_RPM);
        sendNotification(commandId, NotificationType::ERROR, String(errorMsg));
        return;
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
    
    if (!stepper) {
        publishStatusUpdate(StatusUpdateType::SPEED_CHANGED, currentSpeedRPM);
        sendNotification(commandId, NotificationType::ERROR, "Stepper not initialized");
        return;
    }
    
    const uint32_t stepsPerSecond = rpmToStepsPerSecond(rpm);
    applyStepperSpeed(stepsPerSecond);
    
    // If variable speed is enabled, recalculate acceleration for new base speed
    if (speedVariationEnabled) {
        updateAccelerationForVariableSpeed();
    }
    
    // Save settings only if not during initialization (commandId != 0)
    if (commandId != 0) {
        saveSettings();
    }
    
    Serial.printf("Speed set to %.2f RPM (%u steps/sec)\n", rpm, stepsPerSecond);
    
    // Publish status update for speed change
    publishStatusUpdate(StatusUpdateType::SPEED_CHANGED, rpm);
    
    // Report warning if speed was adjusted (use efficient string building)
    if (speedWasAdjusted && commandId != 0) {
        char warningMsg[120];
        snprintf(warningMsg, sizeof(warningMsg), 
                "Speed auto-adjusted from %.2f to %.2f RPM due to variable speed modulation limits", 
                originalRequestedSpeed, rpm);
        sendNotification(commandId, NotificationType::WARNING, String(warningMsg));
    }
    // Success is indicated by the status update - no notification needed for normal success
}

void StepperController::setDirectionInternal(bool clockwise, uint32_t commandId) {
    this->clockwise = clockwise;
    
    // Save settings only if not during initialization (commandId != 0)
    if (commandId != 0) {
        saveSettings();
    }
    
    Serial.printf("Direction set to %s\n", clockwise ? "clockwise" : "counter-clockwise");
    
    // Publish status update for direction change
    publishStatusUpdate(StatusUpdateType::DIRECTION_CHANGED, clockwise);
    // Success is indicated by the status update - no notification needed
}

void StepperController::enableInternal(uint32_t commandId) {
    if (!stepper) {
        publishStatusUpdate(StatusUpdateType::ENABLED_CHANGED, motorEnabled);
        sendNotification(commandId, NotificationType::ERROR, "Stepper not initialized");
        return;
    }
    
    motorEnabled = true;
    
    // Enable the TMC2209 driver
    stepperDriver.enable();
    
    // Start continuous movement in the correct direction
    // FastAccelStepper run methods return MoveResultCode
    MoveResultCode result;
    if (clockwise) {
        result = stepper->runForward();
    } else {
        result = stepper->runBackward();
    }
    
    if (result != MOVE_OK) {
        motorEnabled = false;
        stepperDriver.disable();
        publishStatusUpdate(StatusUpdateType::ENABLED_CHANGED, motorEnabled);
        sendNotification(commandId, NotificationType::ERROR, 
                    "Failed to start stepper movement (result code: " + String((int)result) + ")");
        return;
    }
    
    if (isFirstStart) {
        startTime = millis();
        isFirstStart = false;
    }
    
    Serial.println("Motor enabled and started");
    
    // Publish status update for enabled state change
    publishStatusUpdate(StatusUpdateType::ENABLED_CHANGED, motorEnabled);
    // Success is indicated by the status update - no notification needed
}

void StepperController::disableInternal(uint32_t commandId) {
    motorEnabled = false;
    
    if (stepper) {
        // FastAccelStepper stopMove() returns void - just call it
        stepper->stopMove();
    }
    
    stepperDriver.disable();
    
    Serial.println("Motor disabled and stopped");
    
    // Publish status update for enabled state change
    publishStatusUpdate(StatusUpdateType::ENABLED_CHANGED, false);
    // Success is indicated by the status update - no notification needed
}

void StepperController::emergencyStopInternal(uint32_t commandId) {
    motorEnabled = false;
    
    if (stepper) {
        // FastAccelStepper forceStopAndNewPosition returns void
        stepper->forceStopAndNewPosition(stepper->getCurrentPosition());
    }
    
    stepperDriver.disable();
    
    Serial.println("EMERGENCY STOP executed");
    
    // Publish status update for enabled state change
    publishStatusUpdate(StatusUpdateType::ENABLED_CHANGED, false);
    // Success is indicated by the status update - no notification needed
}

void StepperController::setRunCurrentInternal(int current, uint32_t commandId) {
    // Validate current (10-100%)
    if (current < 10 || current > 100) {
        publishStatusUpdate(StatusUpdateType::CURRENT_CHANGED, runCurrent);
        sendNotification(commandId, NotificationType::ERROR, "Current out of range (10-100%)");
        return;
    }
    
    runCurrent = current;
    
    // Set the run current on TMC2209
    stepperDriver.setRunCurrent(current);
    
    // Update TMC2209 communication status after setting current
    const bool newTmc2209Status = stepperDriver.isSetupAndCommunicating();
    
    // Publish TMC2209 status update if communication status changed
    if (newTmc2209Status != tmc2209Initialized) {
        publishStatusUpdate(StatusUpdateType::TMC2209_STATUS_UPDATE, newTmc2209Status);
        tmc2209Initialized = newTmc2209Status;
    }
    
    // Verify driver communication by checking if it's responding
    if (!tmc2209Initialized) {
        publishStatusUpdate(StatusUpdateType::CURRENT_CHANGED, current);
        sendNotification(commandId, NotificationType::ERROR, 
                    "TMC2209 driver not responding after setting current");
        return;
    }
    
    saveSettings();
    
    Serial.printf("Run current set to %d%%\n", current);
    
    // Publish status update for current change
    publishStatusUpdate(StatusUpdateType::CURRENT_CHANGED, current);
    // Success is indicated by the status update - no notification needed
}

void StepperController::setAccelerationInternal(uint32_t accelerationStepsPerSec2, uint32_t commandId) {
    // Store original requested acceleration for potential warning message
    const uint32_t originalRequestedAccel = accelerationStepsPerSec2;
    bool accelWasAdjusted = false;

    if (!stepper) {
        publishStatusUpdate(StatusUpdateType::ACCELERATION_CHANGED, currentAcceleration);
        sendNotification(commandId, NotificationType::ERROR, "Stepper not initialized");
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

    applyStepperAcceleration(accelerationStepsPerSec2);
    Serial.printf("Acceleration set to %u steps/s²\n", accelerationStepsPerSec2);

    // Save settings only if not during initialization (commandId != 0)
    if (commandId != 0) {
        saveSettings();
    }

    // Publish status update for acceleration change
    publishStatusUpdate(StatusUpdateType::ACCELERATION_CHANGED, accelerationStepsPerSec2);

    // Report warning if acceleration was adjusted (use efficient string building)
    if (accelWasAdjusted && commandId != 0) {
        char warningMsg[120];
        snprintf(warningMsg, sizeof(warningMsg),
                "Acceleration auto-adjusted from %u to %u steps/s² (allowed range: 100-100000)",
                originalRequestedAccel, accelerationStepsPerSec2);
        sendNotification(commandId, NotificationType::WARNING, String(warningMsg));
    }
    // Success is indicated by the status update - no notification needed for normal success
}

void StepperController::sendNotification(uint32_t commandId, NotificationType type, const String& message) {
    if (notificationQueue == nullptr) return;
    
    NotificationData notificationData;
    notificationData.commandId = commandId;
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

float StepperController::getMaxAllowedBaseSpeed() const {
    return calculateMaxAllowedBaseSpeed();
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
        preferences.putFloat("speed", currentSpeedRPM);
        preferences.putBool("clockwise", clockwise);
        preferences.putInt("microsteps", MICRO_STEPS);
        preferences.putInt("current", runCurrent);
        preferences.putUInt("acceleration", currentAcceleration);
        preferences.end();
        Serial.println("Settings saved to flash");
    } else {
        Serial.println("Failed to open preferences for saving");
    }
}

void StepperController::loadSettings() {
    if (preferences.begin("stepper", true)) {
        currentSpeedRPM = preferences.getFloat("speed", currentSpeedRPM);
        clockwise = preferences.getBool("clockwise", clockwise);
        // microSteps is now a define, just read the value for compatibility but don't use it
        preferences.getInt("microsteps", MICRO_STEPS);
        runCurrent = preferences.getInt("current", runCurrent);
        currentAcceleration = preferences.getUInt("acceleration", currentAcceleration);
        preferences.end();
        Serial.printf("Settings loaded from flash: %.2f RPM, %s, %d microsteps, %d%% current, %u accel\n",
                      currentSpeedRPM, clockwise ? "CW" : "CCW", MICRO_STEPS, runCurrent, currentAcceleration);
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

void StepperController::setSpeedVariationInternal(float strength, uint32_t commandId) {
    // Validate strength (0.0 to 1.0)
    if (strength < 0.0f || strength > 1.0f) {
        publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_STRENGTH_CHANGED, speedVariationStrength);
        sendNotification(commandId, NotificationType::ERROR, 
                    "Speed variation strength out of range (0.0-1.0)");
        return;
    }
    
    if (!stepper) {
        publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_STRENGTH_CHANGED, speedVariationStrength);
        sendNotification(commandId, NotificationType::ERROR, "Stepper not initialized");
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
    Serial.printf("Max allowed base speed: %.2f RPM (current: %.2f RPM)\n", 
                  calculateMaxAllowedBaseSpeed(), currentSpeedRPM);
    
    // Publish status update for speed variation strength change
    publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_STRENGTH_CHANGED, strength);
    // Success is indicated by the status update - no notification needed
}

void StepperController::setSpeedVariationPhaseInternal(float phase, uint32_t commandId) {
    // Optimize phase normalization using fmod
    speedVariationPhase = fmodf(phase + (phase < 0.0f ? 6.28318530718f : 0.0f), 6.28318530718f);
    
    Serial.printf("Speed variation phase set to %.2f radians (%.0f degrees)\n", 
                  speedVariationPhase, speedVariationPhase * 180.0f / PI);
    
    // Publish status update for speed variation phase change
    publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_PHASE_CHANGED, speedVariationPhase);
    // Success is indicated by the status update - no notification needed
}

void StepperController::enableSpeedVariationInternal(uint32_t commandId) {
    if (!stepper) {
        publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_ENABLED_CHANGED, speedVariationEnabled);
        sendNotification(commandId, NotificationType::ERROR, "Stepper not initialized");
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
    Serial.printf("Max allowed base speed: %.2f RPM (current: %.2f RPM)\n", 
                  calculateMaxAllowedBaseSpeed(), currentSpeedRPM);
    Serial.println("Current position will be the fastest point in the cycle (new algorithm)");
    
    // Publish status update for speed variation enabled change
    publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_ENABLED_CHANGED, true);
    publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_PHASE_CHANGED, speedVariationPhase);
    publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_STRENGTH_CHANGED, speedVariationStrength);
    // Success is indicated by the status updates - no notification needed
}

void StepperController::disableSpeedVariationInternal(uint32_t commandId) {
    if (!stepper) {
        publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_ENABLED_CHANGED, speedVariationEnabled);
        sendNotification(commandId, NotificationType::ERROR, "Stepper not initialized");
        return;
    }
    
    speedVariationEnabled = false;
    
    // Always reset to base speed when disabling variation, regardless of motor state
    // The stepper library will handle the case when motor is disabled
    uint32_t baseStepsPerSecond = rpmToStepsPerSecond(currentSpeedRPM);
    applyStepperSpeed(baseStepsPerSecond);
    
    Serial.println("Speed variation disabled, returned to constant speed");
    Serial.println("Note: Acceleration remains at current setting for normal operation");
    
    // Publish status update for speed variation enabled change
    publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_ENABLED_CHANGED, false);
    // Success is indicated by the status update - no notification needed
}

void StepperController::requestAllStatusInternal(uint32_t commandId) {
    Serial.println("Publishing all current status values...");
    
    // Publish all current status values through the status update queue
    publishStatusUpdate(StatusUpdateType::SPEED_CHANGED, currentSpeedRPM);
    publishStatusUpdate(StatusUpdateType::DIRECTION_CHANGED, clockwise);
    publishStatusUpdate(StatusUpdateType::ENABLED_CHANGED, motorEnabled);
    publishStatusUpdate(StatusUpdateType::CURRENT_CHANGED, runCurrent);
    publishStatusUpdate(StatusUpdateType::ACCELERATION_CHANGED, currentAcceleration);
    
    // Speed variation status
    publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_ENABLED_CHANGED, speedVariationEnabled);
    publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_STRENGTH_CHANGED, speedVariationStrength);
    publishStatusUpdate(StatusUpdateType::SPEED_VARIATION_PHASE_CHANGED, speedVariationPhase);
    
    // Current variable speed (if variation is enabled)
    if (speedVariationEnabled) {
        float currentVariableSpeed = calculateVariableSpeed();
        publishStatusUpdate(StatusUpdateType::CURRENT_VARIABLE_SPEED_UPDATE, currentVariableSpeed);
    }
    
    // Statistics and runtime info
    unsigned long totalRevolutions = totalSteps / TOTAL_STEPS_PER_REVOLUTION;
    publishStatusUpdate(StatusUpdateType::TOTAL_REVOLUTIONS_UPDATE, (float)totalRevolutions);
    
    unsigned long runtimeSeconds = isFirstStart ? 0 : (millis() - startTime) / 1000;
    publishStatusUpdate(StatusUpdateType::RUNTIME_UPDATE, runtimeSeconds);
    
    bool isCurrentlyRunning = stepper ? stepper->isRunning() : false;
    publishStatusUpdate(StatusUpdateType::IS_RUNNING_UPDATE, isCurrentlyRunning);
    
    // TMC2209 and stall detection
    publishStatusUpdate(StatusUpdateType::TMC2209_STATUS_UPDATE, tmc2209Initialized);
    publishStatusUpdate(StatusUpdateType::STALL_DETECTED_UPDATE, stallDetected);
    publishStatusUpdate(StatusUpdateType::STALL_COUNT_UPDATE, (int)stallCount);
    // Success is indicated by all the status updates - no notification needed
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
        return currentSpeedRPM;
    }
    
    // Calculate current angle in the rotation cycle
    const float angle = getPositionAngle() + speedVariationPhase;
    
    // Normalize angle to 0-2π (optimized - single modulo operation)
    const float normalizedAngle = fmodf(angle + (angle < 0.0f ? 6.28318530718f : 0.0f), 6.28318530718f);
    
    // Use precomputed k and k0 values for efficiency
    // Apply the formula: w(a) = w0 * k0 * 1/(1 + k*cos(a))
    const float denominator = 1.0f + speedVariationK * cosf(normalizedAngle);
    const float variableSpeed = currentSpeedRPM * speedVariationK0 / denominator;
    
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
    
    const float minSpeed = constrain(currentSpeedRPM * k0_over_1_plus_k, MIN_SPEED_RPM, MAX_SPEED_RPM);
    const float maxSpeed = constrain(currentSpeedRPM * k0_over_1_minus_k, MIN_SPEED_RPM, MAX_SPEED_RPM);
    
    // Calculate the maximum speed change and required acceleration
    const float maxSpeedChange = maxSpeed - minSpeed;
    const uint32_t maxSpeedChangeSteps = rpmToStepsPerSecond(maxSpeedChange);
    
    // Calculate time for half a rotation at current RPM: 30/currentSpeedRPM seconds
    const float halfRotationTime = 30.0f / currentSpeedRPM;
    
    // The maximum speed change occurs over half a rotation with 50% safety margin
    const uint32_t requiredAcceleration = static_cast<uint32_t>((maxSpeedChangeSteps / halfRotationTime) * 1.5f);
    
    Serial.printf("Variable speed acceleration calculation (optimized):\n");
    Serial.printf("  External strength: %.2f (%.0f%%), Internal k: %.3f, k0: %.3f\n", 
                  speedVariationStrength, speedVariationStrength * 100.0f, speedVariationK, speedVariationK0);
    Serial.printf("  Base RPM: %.2f, Speed range: %.2f - %.2f RPM (Δ%.2f RPM)\n", 
                  currentSpeedRPM, minSpeed, maxSpeed, maxSpeedChange);
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
    if (requiredAcceleration > currentAcceleration) {
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
    
    // Apply the new speed
    uint32_t stepsPerSecond = rpmToStepsPerSecond(currentVariableSpeed);
    applyStepperSpeed(stepsPerSecond);
}

void StepperController::updateSpeedForVariableSpeed() {
    if (!stepper) {
        return;
    }
    
    // Calculate maximum allowed base speed to ensure modulated speed doesn't exceed MAX_SPEED_RPM
    float maxAllowedBaseSpeed = calculateMaxAllowedBaseSpeed();
    
    // Only update if current speed exceeds the maximum allowed
    if (currentSpeedRPM > maxAllowedBaseSpeed) {
        float oldSpeed = currentSpeedRPM;
        uint32_t stepsPerSecond = rpmToStepsPerSecond(maxAllowedBaseSpeed);
        applyStepperSpeed(stepsPerSecond);
        Serial.printf("Base speed reduced from %.2f to %.2f RPM to prevent exceeding max speed with modulation\n", oldSpeed, currentSpeedRPM);
    }
}

// Speed variation control (thread-safe via command queue)
uint32_t StepperController::setSpeedVariation(float strength) {
    if (commandQueue == nullptr) return 0;
    
    uint32_t commandId = nextCommandId++;
    StepperCommandData cmd;
    cmd.command = StepperCommand::SET_SPEED_VARIATION;
    cmd.floatValue = strength;
    cmd.commandId = commandId;
    
    if (xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE) {
        return commandId;
    }
    return 0;
}

uint32_t StepperController::setSpeedVariationPhase(float phase) {
    if (commandQueue == nullptr) return 0;
    
    uint32_t commandId = nextCommandId++;
    StepperCommandData cmd;
    cmd.command = StepperCommand::SET_SPEED_VARIATION_PHASE;
    cmd.floatValue = phase;
    cmd.commandId = commandId;
    
    if (xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE) {
        return commandId;
    }
    return 0;
}

uint32_t StepperController::enableSpeedVariation() {
    if (commandQueue == nullptr) return 0;
    
    uint32_t commandId = nextCommandId++;
    StepperCommandData cmd;
    cmd.command = StepperCommand::ENABLE_SPEED_VARIATION;
    cmd.commandId = commandId;
    
    if (xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE) {
        return commandId;
    }
    return 0;
}

uint32_t StepperController::disableSpeedVariation() {
    if (commandQueue == nullptr) return 0;
    
    uint32_t commandId = nextCommandId++;
    StepperCommandData cmd;
    cmd.command = StepperCommand::DISABLE_SPEED_VARIATION;
    cmd.commandId = commandId;
    
    if (xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE) {
        return commandId;
    }
    return 0;
}

uint32_t StepperController::requestAllStatus() {
    if (commandQueue == nullptr) return 0;
    
    uint32_t commandId = nextCommandId++;
    StepperCommandData cmd;
    cmd.command = StepperCommand::REQUEST_ALL_STATUS;
    cmd.commandId = commandId;
    
    if (xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE) {
        return commandId;
    }
    return 0;
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
