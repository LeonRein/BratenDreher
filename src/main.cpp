#include <Arduino.h>
#include "StepperController.h"
#include "BLEManager.h"

// Global task objects
StepperController stepperController;
BLEManager bleManager;

// Status LED
const int STATUS_LED_PIN = LED_BUILTIN;
unsigned long lastLedToggle = 0;
bool ledState = false;

void setup() {
    delay(200);
    // Initialize USB CDC for ESP32-S3
    Serial.begin(115200);
    
    // Wait for USB CDC connection (ESP32-S3 specific)
    unsigned long startTime = millis();
    while (!Serial && (millis() - startTime < 1000)) {
        delay(100);
    }
    
    Serial.println();
    Serial.println("=== BratenDreher Stepper Control ===");
    Serial.println("ESP32-S3 USB CDC Serial initialized");
    Serial.println("Initializing system with Task-based architecture...");
    
    // Initialize status LED
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
    
    // Connect BLE manager to stepper controller before starting tasks
    bleManager.setStepperController(&stepperController);
    
    // Start tasks
    if (!stepperController.start()) {
        Serial.println("Failed to start Stepper Task!");
        while (1) {
            digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
            delay(100);
        }
    }
    
    if (!bleManager.start()) {
        Serial.println("Failed to start BLE Task!");
        while (1) {
            digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
            delay(200);
        }
    }
    
    Serial.println("All tasks started successfully!");
    Serial.println("System initialization complete.");
    
    // Turn on status LED to indicate ready state
    digitalWrite(STATUS_LED_PIN, HIGH);
}

void loop() {
    // Main loop now only handles LED status indication
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
    
    // Watchdog feed and task monitoring
    vTaskDelay(pdMS_TO_TICKS(100)); // 100ms delay, plenty for LED control
}