#include <Arduino.h>
#include "StepperController.h"
#include "BLEManager.h"
#include "SystemStatus.h"
#include "SystemCommand.h"
#include "PowerDeliveryTask.h"

// Global task objects
StepperController stepperController;
BLEManager bleManager;
PowerDeliveryTask& powerDeliveryTask = PowerDeliveryTask::getInstance();

// Status LED
const int STATUS_LED_PIN = LED_BUILTIN;
unsigned long lastLedToggle = 0;
bool ledState = false;

void setup() {
    delay(200);
    // Initialize USB CDC for ESP32-S3
    Serial.begin(115200);
    
    // // Wait for USB CDC connection (ESP32-S3 specific)
    // unsigned long startTime = millis();
    // while (!Serial && (millis() - startTime < 1000)) {
    //     delay(100);
    // }
    
    Serial.println();
    Serial.println("=== BratenDreher Stepper Control ===");
    Serial.println("ESP32-S3 USB CDC Serial initialized");
    Serial.println("Initializing system with Task-based architecture...");
    
    // Initialize status LED
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
    
    // Initialize singleton managers before starting tasks
    Serial.println("Initializing SystemStatus...");
    if (!SystemStatus::getInstance().begin()) {
        Serial.println("ERROR: Failed to initialize SystemStatus!");
        while (1) {
            digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
            delay(50);
        }
    }
    
    Serial.println("Initializing SystemCommand...");
    if (!SystemCommand::getInstance().begin()) {
        Serial.println("ERROR: Failed to initialize SystemCommand!");
        while (1) {
            digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
            delay(75);
        }
    }
    
    Serial.println("System singletons initialized successfully!");
    
    // BLE manager now uses SystemCommand singleton directly - no need to connect to stepper controller
    
    // Start tasks - PowerDeliveryTask must start first
    if (!powerDeliveryTask.start()) {
        Serial.println("Failed to start Power Delivery Task!");
        while (1) {
            digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
            delay(50);
        }
    }
    
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