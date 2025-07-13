#include <Arduino.h>
#include "StepperController.h"
#include "BLEManager.h"

// Global objects
StepperController stepperController;
BLEManager bleManager;

// Status LED
const int STATUS_LED_PIN = LED_BUILTIN;
unsigned long lastLedToggle = 0;
bool ledState = false;

void setup() {
    delay(1000); // Give time for serial monitor to connect
    Serial.begin(115200);
    Serial.println("=== BratenDreher Stepper Control ===");
    Serial.println("Initializing system...");
    
    // Initialize status LED
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
    
    // Initialize stepper controller
    if (!stepperController.begin()) {
        Serial.println("Failed to initialize stepper controller!");
        while (1) {
            // Flash LED rapidly to indicate error
            digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
            delay(100);
        }
    }
    
    // Initialize BLE manager
    if (!bleManager.begin("BratenDreher")) {
        Serial.println("Failed to initialize BLE manager!");
        while (1) {
            // Flash LED rapidly to indicate error
            digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
            delay(200);
        }
    }
    
    // Connect BLE manager to stepper controller
    bleManager.setStepperController(&stepperController);
    
    Serial.println("System initialized successfully!");
    Serial.println("Ready for BLE connections...");
    
    // Turn on status LED to indicate ready state
    digitalWrite(STATUS_LED_PIN, HIGH);
}

void loop() {
    // Update BLE manager
    bleManager.update();
    
    // Update stepper controller
    stepperController.update();
    
    // Status LED indication
    unsigned long currentTime = millis();
    if (bleManager.isConnected()) {
        // Solid LED when connected
        digitalWrite(STATUS_LED_PIN, HIGH);
    } else {
        // Slow blink when waiting for connection
        if (currentTime - lastLedToggle >= 1000) {
            ledState = !ledState;
            digitalWrite(STATUS_LED_PIN, ledState);
            lastLedToggle = currentTime;
        }
    }
    
    // Small delay to prevent watchdog issues
    delay(10);
}