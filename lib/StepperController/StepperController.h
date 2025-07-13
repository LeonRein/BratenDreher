#ifndef STEPPER_CONTROLLER_H
#define STEPPER_CONTROLLER_H

#include <Arduino.h>
#include "FastAccelStepper.h"
#include <TMC2209.h>
#include <Preferences.h>

class StepperController {
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
    
    // Speed settings (in RPM) - max 30 RPM for the final output (0.5 RPS)
    float currentSpeedRPM;
    float minSpeedRPM;
    float maxSpeedRPM;
    int microSteps;
    int runCurrent;
    
    // State tracking
    bool motorEnabled;
    bool clockwise;
    unsigned long startTime;
    unsigned long totalSteps;
    bool isFirstStart;
    
    // Helper methods
    bool initPreferences();
    void saveSettings();
    void loadSettings();
    void configureDriver();
    uint32_t rpmToStepsPerSecond(float rpm);
    
public:
    StepperController();
    
    // Initialization
    bool begin();
    
    // Motor control
    void setSpeed(float rpm);
    void setDirection(bool clockwise);
    void enable();
    void disable();
    void emergencyStop();
    
    // Getters
    float getSpeed() const { return currentSpeedRPM; }
    bool isEnabled() const { return motorEnabled; }
    bool isClockwise() const { return clockwise; }
    float getMinSpeed() const { return minSpeedRPM; }
    float getMaxSpeed() const { return maxSpeedRPM; }
    
    // Statistics
    float getTotalRevolutions() const;
    unsigned long getRunTime() const; // in seconds
    bool isRunning() const;
    
    // Settings
    void setMicroSteps(int steps);
    void setRunCurrent(int current);
    int getMicroSteps() const { return microSteps; }
    int getRunCurrent() const { return runCurrent; }
    
    // Update loop - FastAccelStepper handles everything internally
    void update();
    
    // Reset statistics
    void resetCounters();
};

#endif // STEPPER_CONTROLLER_H
