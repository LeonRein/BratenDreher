#include "StepperController.h"

StepperController::StepperController() 
    : Task("Stepper_Task", 4096, 1, 1), // Task name, 4KB stack, priority 1, core 1
      stepper(nullptr), serialStream(Serial2), currentSpeedRPM(1.0f), minSpeedRPM(0.1f), maxSpeedRPM(30.0f),
      microSteps(16), runCurrent(30), motorEnabled(false), clockwise(true),  // Fixed to 16 microsteps
      startTime(0), totalSteps(0), isFirstStart(true), tmc2209Initialized(false),
      stallDetected(false), lastStallTime(0), stallCount(0),
      currentAcceleration(0),  // Will be set during initialization
      speedVariationEnabled(false), speedVariationStrength(0.0f), speedVariationPhase(0.0f), speedVariationStartPosition(0),
      cachedCurrentPosition(0), cachedIsRunning(false), cachedStallDetected(false), nextCommandId(1) {
    
    // Create command queue for thread-safe operation
    commandQueue = xQueueCreate(COMMAND_QUEUE_SIZE, sizeof(StepperCommandData));
    if (commandQueue == nullptr) {
        Serial.println("ERROR: Failed to create stepper command queue!");
    }
    
    // Create result queue for command status reporting
    resultQueue = xQueueCreate(RESULT_QUEUE_SIZE, sizeof(CommandResultData));
    if (resultQueue == nullptr) {
        Serial.println("ERROR: Failed to create stepper result queue!");
    }
}

StepperController::~StepperController() {
    if (commandQueue != nullptr) {
        vQueueDelete(commandQueue);
    }
    if (resultQueue != nullptr) {
        vQueueDelete(resultQueue);
    }
}

bool StepperController::begin() {
    Serial.println("Initializing FastAccelStepper with TMC2209...");
    
    // Initialize preferences storage
    initPreferences();
    
    // Configure pins
    pinMode(TMC_EN, OUTPUT);
    pinMode(MS1, OUTPUT);
    pinMode(MS2, OUTPUT);
    pinMode(DIAG, INPUT);
    
    // Set serial address pins
    digitalWrite(MS1, LOW);
    digitalWrite(MS2, LOW);
    
    // Enable driver (will be controlled via FastAccelStepper)
    digitalWrite(TMC_EN, LOW);
    
    // Initialize TMC2209
    stepperDriver.setup(serialStream, 115200, TMC2209::SERIAL_ADDRESS_0, TMC_RX, TMC_TX);
    
    // Load saved settings
    loadSettings();
    
    // Configure driver with loaded settings
    configureDriver();
    
    // Check TMC2209 initialization status after configuration
    tmc2209Initialized = stepperDriver.isSetupAndCommunicating();
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
    // Target: reach 30 RPM in 2 seconds for responsive speed changes
    // Calculate: 30 RPM * 10 gear ratio * 200 steps * 16 microsteps / 60 seconds = 16000 steps/s
    // Acceleration for 2 seconds: 16000 / 2 = 8000 steps/s²
    uint32_t defaultAcceleration = rpmToStepsPerSecond(30.0f) / 2;  // 2 seconds to reach max speed
    stepper->setAcceleration(defaultAcceleration);
    currentAcceleration = defaultAcceleration;  // Track the set acceleration
    
    // Set initial speed using internal method (avoid command queue during initialization)
    setSpeedInternal(currentSpeedRPM, 0); // Use commandId 0 for internal calls
    
    // Initially disabled
    stepperDriver.disable();
    
    // Check if TMC2209 driver is properly communicating
    if (tmc2209Initialized) {
        Serial.println("FastAccelStepper with TMC2209 initialized successfully");
        Serial.printf("Steps per output revolution: %d\n", TOTAL_STEPS_PER_REVOLUTION);
        Serial.printf("Speed range: %.1f - %.1f RPM\n", minSpeedRPM, maxSpeedRPM);
        Serial.printf("Microsteps: %d, Run current: %d%%\n", microSteps, runCurrent);
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
            preferences.putInt("microsteps", microSteps);
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
    stepperDriver.setMicrostepsPerStep(microSteps);
    stepperDriver.enableAutomaticCurrentScaling();
    
    // Note: StallGuard typically requires SpreadCycle mode (not StealthChop)
    // We'll enable StealthChop for quiet operation but may need to disable it for stall detection
    stepperDriver.enableStealthChop();
    stepperDriver.setCoolStepDurationThreshold(5000);
    
    // Configure StallGuard for stall detection
    // Setting threshold automatically enables StallGuard functionality
    stepperDriver.setStallGuardThreshold(10); // Sensitivity: 0 (most sensitive) to 255 (least sensitive)
    
    // Update communication status after configuration
    tmc2209Initialized = stepperDriver.isSetupAndCommunicating();
    
    // Check if driver is communicating properly
    if (tmc2209Initialized) {
        Serial.printf("TMC2209 configured: %d microsteps, %d%% current, StallGuard threshold: 10\n", microSteps, runCurrent);
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
    Serial.println("StallGuard configured for stall detection");
}

void StepperController::setStallGuardThreshold(uint8_t threshold) {
    if (!tmc2209Initialized) {
        Serial.println("Cannot set StallGuard threshold: TMC2209 not initialized");
        return;
    }
    
    stepperDriver.setStallGuardThreshold(threshold);
    Serial.printf("StallGuard threshold set to %d (0=most sensitive, 255=least sensitive)\n", threshold);
}

uint32_t StepperController::rpmToStepsPerSecond(float rpm) const {
    // Calculate steps per second for the motor (before gear reduction)
    // Final RPM * gear ratio = motor RPM
    float motorRPM = rpm * GEAR_RATIO;
    float motorStepsPerSecond = (motorRPM * STEPS_PER_REVOLUTION * microSteps) / 60.0f;
    return static_cast<uint32_t>(motorStepsPerSecond);
}

float StepperController::getTotalRevolutions() const {
    // Use cached position to avoid thread safety issues with FastAccelStepper
    float motorRevolutions = abs(cachedCurrentPosition) / (float)(STEPS_PER_REVOLUTION * microSteps);
    return motorRevolutions / GEAR_RATIO; // Convert to output shaft revolutions
}

unsigned long StepperController::getRunTime() const {
    if (isFirstStart) return 0;
    return (millis() - startTime) / 1000; // Convert to seconds
}

bool StepperController::isRunning() const {
    // Use cached running state to avoid thread safety issues
    return cachedIsRunning && motorEnabled;
}

void StepperController::run() {
    Serial.println("Stepper Task started");
    
    // Initialize stepper controller
    if (!begin()) {
        Serial.println("Failed to initialize stepper controller!");
        return;
    }
    
    Serial.println("Stepper Controller initialized successfully!");
    
    // Initialize timing variables
    StepperCommandData cmd;
    unsigned long nextCacheUpdate = millis() + CACHE_UPDATE_INTERVAL;
    unsigned long nextSpeedUpdate = millis() + MOTOR_SPEED_UPDATE_INTERVAL;
    
    // Main event loop
    while (true) {
        // Calculate smart timeout for queue operations (use the earliest upcoming event)
        unsigned long nextEvent = min(nextCacheUpdate, nextSpeedUpdate);
        TickType_t timeout = calculateQueueTimeout(nextEvent);
        
        // Block on queue until command arrives or any update is due
        if (xQueueReceive(commandQueue, &cmd, timeout) == pdTRUE) {
            processCommand(cmd);
        }
        
        unsigned long currentTime = millis();
        
        // Perform cache update if due
        if (isCacheUpdateDue(nextCacheUpdate)) {
            updateCache();
            nextCacheUpdate = currentTime + CACHE_UPDATE_INTERVAL;
        }
        
        // Update motor speed if due (more frequent for smooth variation)
        if (isUpdateDue(nextSpeedUpdate)) {
            updateMotorSpeed();
            nextSpeedUpdate = currentTime + MOTOR_SPEED_UPDATE_INTERVAL;
        }
    }
}

TickType_t StepperController::calculateQueueTimeout(unsigned long nextUpdate) {
    unsigned long currentTime = millis();
    
    // Handle millis() overflow gracefully
    if (nextUpdate < currentTime) {
        return 0; // Update overdue
    }
    
    unsigned long timeUntilUpdate = nextUpdate - currentTime;
    
    // Clamp timeout to reasonable bounds
    if (timeUntilUpdate > 100) {
        timeUntilUpdate = 100; // Max 100ms timeout for responsiveness
    }
    
    return timeUntilUpdate > 0 ? pdMS_TO_TICKS(timeUntilUpdate) : 0;
}

bool StepperController::isCacheUpdateDue(unsigned long nextCacheUpdate) {
    return isUpdateDue(nextCacheUpdate);
}

bool StepperController::isUpdateDue(unsigned long nextUpdate) {
    unsigned long currentTime = millis();
    
    // Handle millis() overflow case
    if (nextUpdate > currentTime && (nextUpdate - currentTime) > 0x80000000UL) {
        return true; // Overflow detected
    }
    
    return currentTime >= nextUpdate;
}

void StepperController::updateCache() {
    // Update cached values for thread-safe reading
    if (stepper) {
        cachedCurrentPosition = stepper->getCurrentPosition();
        cachedIsRunning = stepper->isRunning();
    } else {
        cachedCurrentPosition = 0;
        cachedIsRunning = false;
    }
    
    // Check for stall detection via DIAG pin
    bool diagPinHigh = digitalRead(DIAG);
    
    // DIAG pin goes high when StallGuard triggers (motor stalled)
    // Only check for stalls when motor is enabled and should be running
    if (motorEnabled && cachedIsRunning) {
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
    } else if (!motorEnabled || !cachedIsRunning) {
        // Clear stall status when motor is stopped
        if (stallDetected) {
            stallDetected = false;
            Serial.println("Stall status cleared (motor stopped)");
        }
    }
    
    cachedStallDetected = stallDetected;
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
    reportResult(commandId, CommandResult::SUCCESS);
}

void StepperController::resetStallCountInternal(uint32_t commandId) {
    stallCount = 0;
    stallDetected = false;
    lastStallTime = 0;
    
    Serial.println("Stall count reset");
    reportResult(commandId, CommandResult::SUCCESS);
}

void StepperController::setSpeedInternal(float rpm, uint32_t commandId) {
    // Validate input
    if (rpm < minSpeedRPM || rpm > maxSpeedRPM) {
        reportResult(commandId, CommandResult::INVALID_PARAMETER, 
                    "Speed out of range (" + String(minSpeedRPM) + "-" + String(maxSpeedRPM) + " RPM)");
        return;
    }
    
    if (!stepper) {
        reportResult(commandId, CommandResult::HARDWARE_ERROR, "Stepper not initialized");
        return;
    }
    
    currentSpeedRPM = rpm;
    uint32_t stepsPerSecond = rpmToStepsPerSecond(rpm);
    
    // Set speed and apply it with acceleration
    stepper->setSpeedInHz(stepsPerSecond);
    stepper->applySpeedAcceleration();
    
    // If variable speed is enabled, recalculate acceleration for new base speed
    if (speedVariationEnabled) {
        updateAccelerationForVariableSpeed();
    }
    
    // Save settings only if not during initialization (commandId != 0)
    if (commandId != 0) {
        saveSettings();
    }
    
    Serial.printf("Speed set to %.2f RPM (%u steps/sec)\n", rpm, stepsPerSecond);
    reportResult(commandId, CommandResult::SUCCESS);
}

void StepperController::setDirectionInternal(bool clockwise, uint32_t commandId) {
    this->clockwise = clockwise;
    
    // Save settings only if not during initialization (commandId != 0)
    if (commandId != 0) {
        saveSettings();
    }
    
    Serial.printf("Direction set to %s\n", clockwise ? "clockwise" : "counter-clockwise");
    reportResult(commandId, CommandResult::SUCCESS);
}

void StepperController::enableInternal(uint32_t commandId) {
    if (!stepper) {
        reportResult(commandId, CommandResult::HARDWARE_ERROR, "Stepper not initialized");
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
        reportResult(commandId, CommandResult::HARDWARE_ERROR, 
                    "Failed to start stepper movement (result code: " + String((int)result) + ")");
        return;
    }
    
    if (isFirstStart) {
        startTime = millis();
        isFirstStart = false;
    }
    
    Serial.println("Motor enabled and started");
    reportResult(commandId, CommandResult::SUCCESS);
}

void StepperController::disableInternal(uint32_t commandId) {
    motorEnabled = false;
    
    if (stepper) {
        // FastAccelStepper stopMove() returns void - just call it
        stepper->stopMove();
    }
    
    stepperDriver.disable();
    
    Serial.println("Motor disabled and stopped");
    reportResult(commandId, CommandResult::SUCCESS);
}

void StepperController::emergencyStopInternal(uint32_t commandId) {
    motorEnabled = false;
    
    if (stepper) {
        // FastAccelStepper forceStopAndNewPosition returns void
        stepper->forceStopAndNewPosition(stepper->getCurrentPosition());
    }
    
    stepperDriver.disable();
    
    Serial.println("EMERGENCY STOP executed");
    reportResult(commandId, CommandResult::SUCCESS);
}

void StepperController::setRunCurrentInternal(int current, uint32_t commandId) {
    // Validate current (10-100%)
    if (current < 10 || current > 100) {
        reportResult(commandId, CommandResult::INVALID_PARAMETER, 
                    "Current out of range (10-100%)");
        return;
    }
    
    runCurrent = current;
    
    // Set the run current on TMC2209
    stepperDriver.setRunCurrent(current);
    
    // Update TMC2209 communication status after setting current
    tmc2209Initialized = stepperDriver.isSetupAndCommunicating();
    
    // Verify driver communication by checking if it's responding
    if (!tmc2209Initialized) {
        reportResult(commandId, CommandResult::COMMUNICATION_ERROR, 
                    "TMC2209 driver not responding after setting current");
        return;
    }
    
    saveSettings();
    
    Serial.printf("Run current set to %d%%\n", current);
    reportResult(commandId, CommandResult::SUCCESS);
}

void StepperController::setAccelerationInternal(uint32_t accelerationStepsPerSec2, uint32_t commandId) {
    if (!stepper) {
        reportResult(commandId, CommandResult::HARDWARE_ERROR, 
                    "Stepper not initialized");
        return;
    }
    
    // Validate acceleration range (reasonable limits to prevent excessive values)
    if (accelerationStepsPerSec2 < 100 || accelerationStepsPerSec2 > 100000) {
        reportResult(commandId, CommandResult::INVALID_PARAMETER, 
                    "Acceleration out of range (100-100000 steps/s²)");
        return;
    }
    
    // Set the acceleration on FastAccelStepper
    stepper->setAcceleration(accelerationStepsPerSec2);
    currentAcceleration = accelerationStepsPerSec2;  // Track the set acceleration
    stepper->applySpeedAcceleration();
    
    Serial.printf("Acceleration set to %u steps/s²\n", accelerationStepsPerSec2);
    reportResult(commandId, CommandResult::SUCCESS);
}

void StepperController::reportResult(uint32_t commandId, CommandResult result, const String& errorMessage) {
    if (resultQueue == nullptr) return;
    
    CommandResultData resultData;
    resultData.commandId = commandId;
    resultData.result = result;
    resultData.setErrorMessage(errorMessage.c_str());
    
    // Try to send result, but don't block if queue is full
    xQueueSend(resultQueue, &resultData, 0);
}

bool StepperController::getCommandResult(CommandResultData& result) {
    if (resultQueue == nullptr) return false;
    
    return xQueueReceive(resultQueue, &result, 0) == pdTRUE; // Non-blocking
}

void StepperController::saveSettings() {
    if (preferences.begin("stepper", false)) {
        preferences.putFloat("speed", currentSpeedRPM);
        preferences.putBool("clockwise", clockwise);
        preferences.putInt("microsteps", microSteps);
        preferences.putInt("current", runCurrent);
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
        microSteps = preferences.getInt("microsteps", microSteps);
        runCurrent = preferences.getInt("current", runCurrent);
        preferences.end();
        
        Serial.printf("Settings loaded from flash: %.2f RPM, %s, %d microsteps, %d%% current\n",
                      currentSpeedRPM, clockwise ? "CW" : "CCW", microSteps, runCurrent);
    } else {
        Serial.println("Failed to open preferences for loading, using defaults");
        // Default values are already set in constructor
    }
}

// Thread-safe public interface using command queue
uint32_t StepperController::setSpeed(float rpm) {
    if (commandQueue == nullptr) return 0;
    
    uint32_t commandId = nextCommandId++;
    StepperCommandData cmd;
    cmd.command = StepperCommand::SET_SPEED;
    cmd.floatValue = rpm;
    cmd.commandId = commandId;
    
    if (xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE) {
        return commandId;
    }
    return 0;
}

uint32_t StepperController::setDirection(bool clockwise) {
    if (commandQueue == nullptr) return 0;
    
    uint32_t commandId = nextCommandId++;
    StepperCommandData cmd;
    cmd.command = StepperCommand::SET_DIRECTION;
    cmd.boolValue = clockwise;
    cmd.commandId = commandId;
    
    if (xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE) {
        return commandId;
    }
    return 0;
}

uint32_t StepperController::enable() {
    if (commandQueue == nullptr) return 0;
    
    uint32_t commandId = nextCommandId++;
    StepperCommandData cmd;
    cmd.command = StepperCommand::ENABLE;
    cmd.commandId = commandId;
    
    if (xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE) {
        return commandId;
    }
    return 0;
}

uint32_t StepperController::disable() {
    if (commandQueue == nullptr) return 0;
    
    uint32_t commandId = nextCommandId++;
    StepperCommandData cmd;
    cmd.command = StepperCommand::DISABLE;
    cmd.commandId = commandId;
    
    if (xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE) {
        return commandId;
    }
    return 0;
}

uint32_t StepperController::emergencyStop() {
    if (commandQueue == nullptr) return 0;
    
    uint32_t commandId = nextCommandId++;
    StepperCommandData cmd;
    cmd.command = StepperCommand::EMERGENCY_STOP;
    cmd.commandId = commandId;
    
    // Emergency stop has higher priority - try to send immediately
    if (xQueueSend(commandQueue, &cmd, 0) == pdTRUE) {
        return commandId;
    }
    return 0;
}

uint32_t StepperController::setRunCurrent(int current) {
    if (commandQueue == nullptr) return 0;
    
    uint32_t commandId = nextCommandId++;
    StepperCommandData cmd;
    cmd.command = StepperCommand::SET_CURRENT;
    cmd.intValue = current;
    cmd.commandId = commandId;
    
    if (xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE) {
        return commandId;
    }
    return 0;
}

uint32_t StepperController::setAcceleration(uint32_t accelerationStepsPerSec2) {
    if (commandQueue == nullptr) return 0;
    
    uint32_t commandId = nextCommandId++;
    StepperCommandData cmd;
    cmd.command = StepperCommand::SET_ACCELERATION;
    cmd.intValue = accelerationStepsPerSec2;
    cmd.commandId = commandId;
    
    if (xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE) {
        return commandId;
    }
    return 0;
}

uint32_t StepperController::resetCounters() {
    if (commandQueue == nullptr) return 0;
    
    uint32_t commandId = nextCommandId++;
    StepperCommandData cmd;
    cmd.command = StepperCommand::RESET_COUNTERS;
    cmd.commandId = commandId;
    
    if (xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE) {
        return commandId;
    }
    return 0;
}

uint32_t StepperController::resetStallCount() {
    if (commandQueue == nullptr) return 0;
    
    uint32_t commandId = nextCommandId++;
    StepperCommandData cmd;
    cmd.command = StepperCommand::RESET_STALL_COUNT;
    cmd.commandId = commandId;
    
    if (xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE) {
        return commandId;
    }
    return 0;
}

void StepperController::setSpeedVariationInternal(float strength, uint32_t commandId) {
    // Validate strength (0.0 to 1.0)
    if (strength < 0.0f || strength > 1.0f) {
        reportResult(commandId, CommandResult::INVALID_PARAMETER, 
                    "Speed variation strength out of range (0.0-1.0)");
        return;
    }
    
    speedVariationStrength = strength;
    
    // If variable speed is enabled, update acceleration for new strength
    if (speedVariationEnabled) {
        updateAccelerationForVariableSpeed();
    }
    
    Serial.printf("Speed variation strength set to %.2f (%.0f%%)\n", strength, strength * 100.0f);
    reportResult(commandId, CommandResult::SUCCESS);
}

void StepperController::setSpeedVariationPhaseInternal(float phase, uint32_t commandId) {
    // Normalize phase to 0-2π range
    while (phase < 0.0f) phase += 2.0f * PI;
    while (phase >= 2.0f * PI) phase -= 2.0f * PI;
    
    speedVariationPhase = phase;
    
    Serial.printf("Speed variation phase set to %.2f radians (%.0f degrees)\n", 
                  phase, phase * 180.0f / PI);
    reportResult(commandId, CommandResult::SUCCESS);
}

void StepperController::enableSpeedVariationInternal(uint32_t commandId) {
    if (!stepper) {
        reportResult(commandId, CommandResult::HARDWARE_ERROR, "Stepper not initialized");
        return;
    }
    
    // Set current position as the reference point for variation
    speedVariationStartPosition = stepper->getCurrentPosition();
    speedVariationEnabled = true;
    
    // Reset phase offset to 0 when re-enabling variable speed
    speedVariationPhase = 0.0f;
    
    // Dynamically calculate and apply required acceleration for variable speed
    updateAccelerationForVariableSpeed();
    
    Serial.printf("Speed variation enabled at position %ld (strength: %.0f%%, phase: 0°)\n", 
                  speedVariationStartPosition, speedVariationStrength * 100.0f);
    Serial.println("Current position will be the slowest point in the cycle");
    reportResult(commandId, CommandResult::SUCCESS);
}

void StepperController::disableSpeedVariationInternal(uint32_t commandId) {
    speedVariationEnabled = false;
    
    // Reset to base speed immediately
    if (stepper && motorEnabled) {
        uint32_t baseStepsPerSecond = rpmToStepsPerSecond(currentSpeedRPM);
        stepper->setSpeedInHz(baseStepsPerSecond);
        stepper->applySpeedAcceleration();
    }
    
    Serial.println("Speed variation disabled, returned to constant speed");
    Serial.println("Note: Acceleration remains at current setting for normal operation");
    reportResult(commandId, CommandResult::SUCCESS);
}

float StepperController::calculateVariableSpeed() const {
    if (!speedVariationEnabled || speedVariationStrength == 0.0f) {
        return currentSpeedRPM;
    }
    
    // Calculate current angle in the rotation cycle
    float angle = getPositionAngle();
    
    // Apply phase offset
    angle += speedVariationPhase;
    
    // Normalize angle to 0-2π
    while (angle >= 2.0f * PI) angle -= 2.0f * PI;
    while (angle < 0.0f) angle += 2.0f * PI;
    
    // Calculate speed variation using cosine wave to maintain mean RPM
    // cos(angle) ranges from -1 to +1, with mean value of 0 over a full cycle
    // We want minimum speed at angle = 0 (current position when enabled)
    // So we use -cos(angle) which gives -1 at angle = 0
    float variation = -cosf(angle);
    
    // Scale variation by strength, maintaining the same mean RPM
    // Since cos has mean 0, currentSpeedRPM * (1 + variation * strength) 
    // has mean currentSpeedRPM over a full rotation
    float speedMultiplier = 1.0f + (variation * speedVariationStrength);
    
    // Ensure we don't go below minimum or above maximum speed
    float variableSpeed = currentSpeedRPM * speedMultiplier;
    variableSpeed = constrain(variableSpeed, minSpeedRPM, maxSpeedRPM);
    
    return variableSpeed;
}

float StepperController::getPositionAngle() const {
    if (!stepper) return 0.0f;
    
    // Calculate position relative to where speed variation was enabled
    int32_t relativePosition = cachedCurrentPosition - speedVariationStartPosition;
    
    // Convert position to angle in one full output revolution
    // One full output revolution = TOTAL_STEPS_PER_REVOLUTION * microSteps
    int32_t stepsPerOutputRevolution = TOTAL_STEPS_PER_REVOLUTION * microSteps;
    
    // Calculate angle (0 to 2π for one complete revolution)
    float angle = (2.0f * PI * relativePosition) / stepsPerOutputRevolution;
    
    return angle;
}

float StepperController::getCurrentVariableSpeed() const {
    if (speedVariationEnabled) {
        return calculateVariableSpeed();
    }
    return currentSpeedRPM;
}

uint32_t StepperController::calculateRequiredAccelerationForVariableSpeed() const {
    if (!speedVariationEnabled || speedVariationStrength == 0.0f) {
        return 0; // No special acceleration needed
    }
    
    // Calculate the maximum speed change that can occur
    // Maximum variation is when going from minimum to maximum multiplier
    float minMultiplier = 1.0f - speedVariationStrength;
    float maxMultiplier = 1.0f + speedVariationStrength;
    
    float minSpeed = currentSpeedRPM * minMultiplier;
    float maxSpeed = currentSpeedRPM * maxMultiplier;
    
    // Constrain to motor limits
    minSpeed = constrain(minSpeed, minSpeedRPM, maxSpeedRPM);
    maxSpeed = constrain(maxSpeed, minSpeedRPM, maxSpeedRPM);
    
    // Calculate the maximum speed change
    float maxSpeedChange = maxSpeed - minSpeed;
    
    // Convert to steps per second
    uint32_t maxSpeedChangeSteps = rpmToStepsPerSecond(maxSpeedChange);
    
    // Calculate time for half a rotation at current RPM
    // Half rotation time = (0.5 rev / currentSpeedRPM) * 60 s/min = 30/currentSpeedRPM seconds
    float halfRotationTime = 30.0f / currentSpeedRPM;
    
    // The maximum speed change occurs over half a rotation
    // (from minimum to maximum or vice versa)
    uint32_t requiredAcceleration = static_cast<uint32_t>(maxSpeedChangeSteps / halfRotationTime);
    
    // Add safety margin (50%) to ensure smooth operation
    requiredAcceleration = static_cast<uint32_t>(requiredAcceleration * 1.5f);
    
    Serial.printf("Variable speed acceleration calculation:\n");
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
    
    // FastAccelStepper doesn't provide a getter for current acceleration,
    // so we need to track it ourselves or always apply the calculated value.
    // For now, we'll always set the required acceleration when variable speed is active
    // to ensure smooth operation regardless of the previous setting.
    stepper->setAcceleration(requiredAcceleration);
    currentAcceleration = requiredAcceleration;  // Track the set acceleration
    stepper->applySpeedAcceleration();
    
    Serial.printf("Acceleration set to %u steps/s² for variable speed operation\n", 
                  requiredAcceleration);
}

void StepperController::updateMotorSpeed() {
    if (!stepper || !motorEnabled || !speedVariationEnabled) {
        return;
    }
    
    // Calculate current variable speed
    float currentVariableSpeed = calculateVariableSpeed();
    
    // Apply the new speed
    uint32_t stepsPerSecond = rpmToStepsPerSecond(currentVariableSpeed);
    stepper->setSpeedInHz(stepsPerSecond);
    stepper->applySpeedAcceleration();
}

// Helper method to verify mean RPM calculation (for testing/debugging)
float StepperController::calculateMeanRPMOverRotation() const {
    if (!speedVariationEnabled || speedVariationStrength == 0.0f) {
        return currentSpeedRPM;
    }
    
    // Sample the speed function over a full rotation to verify mean
    const int samples = 360; // One sample per degree
    float sum = 0.0f;
    
    for (int i = 0; i < samples; i++) {
        float angle = (2.0f * PI * i) / samples;
        
        // Apply the same calculation as in calculateVariableSpeed()
        float variation = -cosf(angle);
        float speedMultiplier = 1.0f + (variation * speedVariationStrength);
        float speed = currentSpeedRPM * speedMultiplier;
        
        // Apply same constraints as the real function
        speed = constrain(speed, minSpeedRPM, maxSpeedRPM);
        sum += speed;
    }
    
    return sum / samples;
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
