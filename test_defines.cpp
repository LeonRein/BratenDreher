// Test compilation verification for StepperController defines
#include "lib/StepperController/StepperController.h"

// This file is just to verify that all defines work correctly
void test_defines() {
    // Test that all defines are accessible
    int testPin = TMC_EN_PIN;
    int testSteps = STEPS_PER_REVOLUTION;
    int testGear = GEAR_RATIO;
    int testTotal = TOTAL_STEPS_PER_REVOLUTION;
    int testMicro = MICRO_STEPS;
    float testMinSpeed = MIN_SPEED_RPM;
    float testMaxSpeed = MAX_SPEED_RPM;
    int testStatusInterval = STATUS_UPDATE_INTERVAL;
    int testMotorInterval = MOTOR_SPEED_UPDATE_INTERVAL;
    
    // Use variables to avoid unused variable warnings
    (void)testPin;
    (void)testSteps;
    (void)testGear;
    (void)testTotal;
    (void)testMicro;
    (void)testMinSpeed;
    (void)testMaxSpeed;
    (void)testStatusInterval;
    (void)testMotorInterval;
}
