#include "StepperController.h"

StepperController::StepperController() 
    : stepper(nullptr), serialStream(Serial2), currentSpeedRPM(1.0f), minSpeedRPM(0.1f), maxSpeedRPM(30.0f),
      microSteps(32), runCurrent(30), motorEnabled(false), clockwise(true),
      startTime(0), totalSteps(0), isFirstStart(true) {
}

bool StepperController::begin() {
    Serial.println("Initializing FastAccelStepper with TMC2209...");
    
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
    
    // Set initial speed
    setSpeed(currentSpeedRPM);
    
    // Initially disabled
    stepperDriver.disable();
    
    Serial.println("FastAccelStepper with TMC2209 initialized successfully");
    Serial.printf("Steps per output revolution: %d\n", TOTAL_STEPS_PER_REVOLUTION);
    Serial.printf("Speed range: %.1f - %.1f RPM\n", minSpeedRPM, maxSpeedRPM);
    Serial.printf("Microsteps: %d, Run current: %d%%\n", microSteps, runCurrent);
    
    return true;
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

void StepperController::setSpeed(float rpm) {
    // Clamp speed to valid range
    rpm = constrain(rpm, minSpeedRPM, maxSpeedRPM);
    currentSpeedRPM = rpm;
    
    if (stepper && rpm > 0) {
        uint32_t stepsPerSecond = rpmToStepsPerSecond(rpm);
        stepper->setSpeedInHz(stepsPerSecond);
        
        Serial.printf("Speed set to %.2f RPM (%d steps/s)\n", rpm, stepsPerSecond);
    }
}

void StepperController::setDirection(bool newClockwise) {
    clockwise = newClockwise;
    Serial.printf("Direction set to %s\n", clockwise ? "clockwise" : "counter-clockwise");
}

void StepperController::enable() {
    if (!stepper || motorEnabled) return;
    
    motorEnabled = true;
    stepperDriver.enable();
    
    // Record start time if this is the first start
    if (isFirstStart) {
        startTime = millis();
        isFirstStart = false;
        totalSteps = 0;
    }
    
    // Start continuous rotation in the set direction
    if (clockwise) {
        stepper->runForward();
    } else {
        stepper->runBackward();
    }
    
    Serial.printf("Motor enabled - running %s at %.2f RPM\n", 
                  clockwise ? "clockwise" : "counter-clockwise", currentSpeedRPM);
}

void StepperController::disable() {
    if (!stepper || !motorEnabled) return;
    
    motorEnabled = false;
    stepper->stopMove();
    stepperDriver.disable();
    
    Serial.println("Motor disabled");
}

void StepperController::emergencyStop() {
    if (!stepper) return;
    
    motorEnabled = false;
    stepper->forceStopAndNewPosition(0);
    stepperDriver.disable();
    
    Serial.println("Emergency stop executed");
}

float StepperController::getTotalRevolutions() const {
    if (!stepper) return 0.0f;
    
    // Get total steps from FastAccelStepper
    int32_t currentPosition = stepper->getCurrentPosition();
    float motorRevolutions = abs(currentPosition) / (float)(STEPS_PER_REVOLUTION * microSteps);
    return motorRevolutions / GEAR_RATIO; // Convert to output shaft revolutions
}

unsigned long StepperController::getRunTime() const {
    if (isFirstStart) return 0;
    return (millis() - startTime) / 1000; // Convert to seconds
}

bool StepperController::isRunning() const {
    if (!stepper) return false;
    return stepper->isRunning() && motorEnabled;
}

void StepperController::setMicroSteps(int steps) {
    // Stop motor before changing microsteps
    bool wasEnabled = motorEnabled;
    if (motorEnabled) {
        disable();
    }
    
    microSteps = steps;
    configureDriver();
    
    // Update speed calculation
    setSpeed(currentSpeedRPM);
    
    // Re-enable if it was running
    if (wasEnabled) {
        enable();
    }
    
    saveSettings();
}

void StepperController::setRunCurrent(int current) {
    runCurrent = constrain(current, 10, 100);
    configureDriver();
    saveSettings();
}

void StepperController::update() {
    // FastAccelStepper handles everything internally via interrupts
    // No manual update needed, but we can add monitoring here if needed
}

void StepperController::resetCounters() {
    if (stepper) {
        stepper->setCurrentPosition(0);
    }
    totalSteps = 0;
    startTime = millis();
    isFirstStart = false;
    
    Serial.println("Counters reset");
}

void StepperController::saveSettings() {
    preferences.begin("stepper", false);
    preferences.putFloat("speed", currentSpeedRPM);
    preferences.putBool("clockwise", clockwise);
    preferences.putInt("microsteps", microSteps);
    preferences.putInt("current", runCurrent);
    preferences.end();
    
    Serial.println("Settings saved to flash");
}

void StepperController::loadSettings() {
    preferences.begin("stepper", true); // read-only
    
    currentSpeedRPM = preferences.getFloat("speed", 1.0f);
    clockwise = preferences.getBool("clockwise", true);
    microSteps = preferences.getInt("microsteps", 32);
    runCurrent = preferences.getInt("current", 30);
    
    preferences.end();
    
    // Validate loaded values
    currentSpeedRPM = constrain(currentSpeedRPM, minSpeedRPM, maxSpeedRPM);
    runCurrent = constrain(runCurrent, 10, 100);
    
    Serial.println("Settings loaded from flash");
    Serial.printf("Loaded: Speed=%.2f RPM, Direction=%s, Microsteps=%d, Current=%d%%\n",
                  currentSpeedRPM, clockwise ? "CW" : "CCW", microSteps, runCurrent);
}
