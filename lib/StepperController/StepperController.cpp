#include "StepperController.h"

StepperController::StepperController() 
    : Task("Stepper_Task", 4096, 1, 1), // Task name, 4KB stack, priority 1, core 1
      stepper(nullptr), serialStream(Serial2), currentSpeedRPM(1.0f), minSpeedRPM(0.1f), maxSpeedRPM(30.0f),
      microSteps(32), runCurrent(30), motorEnabled(false), clockwise(true),
      startTime(0), totalSteps(0), isFirstStart(true),
      cachedCurrentPosition(0), cachedIsRunning(false), nextCommandId(1) {
    
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
    
    // Set acceleration (steps/s²) - smooth acceleration for rotisserie
    stepper->setAcceleration(500);
    
    // Set initial speed using internal method (avoid command queue during initialization)
    setSpeedInternal(currentSpeedRPM, 0); // Use commandId 0 for internal calls
    
    // Initially disabled
    stepperDriver.disable();
    
    Serial.println("FastAccelStepper with TMC2209 initialized successfully");
    Serial.printf("Steps per output revolution: %d\n", TOTAL_STEPS_PER_REVOLUTION);
    Serial.printf("Speed range: %.1f - %.1f RPM\n", minSpeedRPM, maxSpeedRPM);
    Serial.printf("Microsteps: %d, Run current: %d%%\n", microSteps, runCurrent);
    
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
    stepperDriver.enableStealthChop();
    stepperDriver.setCoolStepDurationThreshold(5000);
    
    Serial.printf("TMC2209 configured: %d microsteps, %d%% current\n", microSteps, runCurrent);
}

uint32_t StepperController::rpmToStepsPerSecond(float rpm) {
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
    
    // Main event loop
    while (true) {
        // Calculate smart timeout for queue operations
        TickType_t timeout = calculateQueueTimeout(nextCacheUpdate);
        
        // Block on queue until command arrives or cache update is due
        if (xQueueReceive(commandQueue, &cmd, timeout) == pdTRUE) {
            processCommand(cmd);
        }
        
        // Perform cache update if due
        if (isCacheUpdateDue(nextCacheUpdate)) {
            updateCache();
            nextCacheUpdate = millis() + CACHE_UPDATE_INTERVAL;
        }
    }
}

TickType_t StepperController::calculateQueueTimeout(unsigned long nextCacheUpdate) {
    unsigned long currentTime = millis();
    
    // Handle millis() overflow gracefully
    if (nextCacheUpdate < currentTime) {
        return 0; // Cache update overdue
    }
    
    unsigned long timeUntilUpdate = nextCacheUpdate - currentTime;
    
    // Clamp timeout to reasonable bounds
    if (timeUntilUpdate > 100) {
        timeUntilUpdate = 100; // Max 100ms timeout for responsiveness
    }
    
    return timeUntilUpdate > 0 ? pdMS_TO_TICKS(timeUntilUpdate) : 0;
}

bool StepperController::isCacheUpdateDue(unsigned long nextCacheUpdate) {
    unsigned long currentTime = millis();
    
    // Handle millis() overflow case
    if (nextCacheUpdate > currentTime && (nextCacheUpdate - currentTime) > 0x80000000UL) {
        return true; // Overflow detected
    }
    
    return currentTime >= nextCacheUpdate;
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
            
        case StepperCommand::SET_MICROSTEPS:
            setMicroStepsInternal(cmd.intValue, cmd.commandId);
            break;
            
        case StepperCommand::SET_CURRENT:
            setRunCurrentInternal(cmd.intValue, cmd.commandId);
            break;
            
        case StepperCommand::RESET_COUNTERS:
            resetCountersInternal(cmd.commandId);
            break;
    }
}

void StepperController::resetCountersInternal(uint32_t commandId) {
    if (stepper) {
        stepper->setCurrentPosition(0);
    }
    totalSteps = 0;
    startTime = millis();
    isFirstStart = false;
    
    Serial.println("Counters reset");
    reportResult(commandId, CommandResult::SUCCESS);
}

void StepperController::setSpeedInternal(float rpm, uint32_t commandId) {
    // Validate input
    if (rpm < minSpeedRPM || rpm > maxSpeedRPM) {
        reportResult(commandId, CommandResult::INVALID_PARAMETER, 
                    "Speed out of range (" + String(minSpeedRPM) + "-" + String(maxSpeedRPM) + " RPM)");
        return;
    }
    
    // Check if driver is responding
    if (!isDriverResponding()) {
        reportResult(commandId, CommandResult::DRIVER_NOT_RESPONDING, 
                    "TMC2209 driver not responding");
        return;
    }
    
    if (!stepper) {
        reportResult(commandId, CommandResult::HARDWARE_ERROR, "Stepper not initialized");
        return;
    }
    
    currentSpeedRPM = rpm;
    uint32_t stepsPerSecond = rpmToStepsPerSecond(rpm);
    stepper->setSpeedInHz(stepsPerSecond);
    
    Serial.printf("Speed set to %.2f RPM (%u steps/sec)\n", rpm, stepsPerSecond);
    reportResult(commandId, CommandResult::SUCCESS);
}

void StepperController::setDirectionInternal(bool clockwise, uint32_t commandId) {
    this->clockwise = clockwise;
    
    Serial.printf("Direction set to %s\n", clockwise ? "clockwise" : "counter-clockwise");
    reportResult(commandId, CommandResult::SUCCESS);
}

void StepperController::enableInternal(uint32_t commandId) {
    // Check if driver is responding
    if (!isDriverResponding()) {
        reportResult(commandId, CommandResult::DRIVER_NOT_RESPONDING, 
                    "TMC2209 driver not responding");
        return;
    }
    
    if (!stepper) {
        reportResult(commandId, CommandResult::HARDWARE_ERROR, "Stepper not initialized");
        return;
    }
    
    motorEnabled = true;
    stepperDriver.enable();
    
    // Start continuous movement in the correct direction
    if (clockwise) {
        stepper->runForward();
    } else {
        stepper->runBackward();
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
        stepper->stopMove();
    }
    
    stepperDriver.disable();
    
    Serial.println("Motor disabled and stopped");
    reportResult(commandId, CommandResult::SUCCESS);
}

void StepperController::emergencyStopInternal(uint32_t commandId) {
    motorEnabled = false;
    
    if (stepper) {
        stepper->forceStopAndNewPosition(stepper->getCurrentPosition());
    }
    
    stepperDriver.disable();
    
    Serial.println("EMERGENCY STOP executed");
    reportResult(commandId, CommandResult::SUCCESS);
}

void StepperController::setMicroStepsInternal(int steps, uint32_t commandId) {
    // Validate microsteps (must be power of 2, typically 1, 2, 4, 8, 16, 32, 64, 128, 256)
    if (steps <= 0 || (steps & (steps - 1)) != 0 || steps > 256) {
        reportResult(commandId, CommandResult::INVALID_PARAMETER, 
                    "Invalid microsteps value (must be power of 2, 1-256)");
        return;
    }
    
    // Check if driver is responding
    if (!isDriverResponding()) {
        reportResult(commandId, CommandResult::DRIVER_NOT_RESPONDING, 
                    "TMC2209 driver not responding");
        return;
    }
    
    microSteps = steps;
    
    try {
        stepperDriver.setMicrostepsPerStep(steps);
        saveSettings();
        
        Serial.printf("Microsteps set to %d\n", steps);
        reportResult(commandId, CommandResult::SUCCESS);
    } catch (...) {
        reportResult(commandId, CommandResult::COMMUNICATION_ERROR, 
                    "Failed to communicate with TMC2209 driver");
    }
}

void StepperController::setRunCurrentInternal(int current, uint32_t commandId) {
    // Validate current (10-100%)
    if (current < 10 || current > 100) {
        reportResult(commandId, CommandResult::INVALID_PARAMETER, 
                    "Current out of range (10-100%)");
        return;
    }
    
    // Check if driver is responding
    if (!isDriverResponding()) {
        reportResult(commandId, CommandResult::DRIVER_NOT_RESPONDING, 
                    "TMC2209 driver not responding");
        return;
    }
    
    runCurrent = current;
    
    try {
        stepperDriver.setRunCurrent(current);
        saveSettings();
        
        Serial.printf("Run current set to %d%%\n", current);
        reportResult(commandId, CommandResult::SUCCESS);
    } catch (...) {
        reportResult(commandId, CommandResult::COMMUNICATION_ERROR, 
                    "Failed to communicate with TMC2209 driver");
    }
}

void StepperController::reportResult(uint32_t commandId, CommandResult result, const String& errorMessage) {
    if (resultQueue == nullptr) return;
    
    CommandResultData resultData;
    resultData.commandId = commandId;
    resultData.result = result;
    resultData.errorMessage = errorMessage;
    
    // Try to send result, but don't block if queue is full
    xQueueSend(resultQueue, &resultData, 0);
}

bool StepperController::isDriverResponding() {
    // Try to read a register from the TMC2209 to check if it's responding
    try {
        // Read the driver version register - this should work if driver is connected
        uint8_t version = stepperDriver.getVersion();
        // Version should be non-zero for a responding TMC2209
        return (version != 0);
    } catch (...) {
        // Any exception means driver is not responding
        return false;
    }
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

uint32_t StepperController::setMicroSteps(int steps) {
    if (commandQueue == nullptr) return 0;
    
    uint32_t commandId = nextCommandId++;
    StepperCommandData cmd;
    cmd.command = StepperCommand::SET_MICROSTEPS;
    cmd.intValue = steps;
    cmd.commandId = commandId;
    
    if (xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE) {
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

bool StepperController::getCommandResult(CommandResultData& result) {
    if (resultQueue == nullptr) return false;
    
    return xQueueReceive(resultQueue, &result, 0) == pdTRUE; // Non-blocking
}
