#include <Arduino.h>
#include "StepperController.h"
#include "BLEManager.h"
#include "SystemStatus.h"
#include "SystemCommand.h"
#include "PowerDeliveryTask.h"
#include "dbg_print.h"

// Global task objects
StepperController& stepperController = StepperController::getInstance();
BLEManager& bleManager = BLEManager::getInstance();
PowerDeliveryTask& powerDeliveryTask = PowerDeliveryTask::getInstance();

#include "OTA.h"

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
    
    dbg_println();
    dbg_println("=== BratenDreher Stepper Control ===");
    dbg_println("ESP32-S3 USB CDC Serial initialized");
    dbg_println("Initializing system with Task-based architecture...");
    
    // Initialize status LED
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
    
    // Initialize singleton managers before starting tasks
    dbg_println("Initializing SystemStatus...");
    if (!SystemStatus::getInstance().begin()) {
        dbg_println("ERROR: Failed to initialize SystemStatus!");
        while (1) {
            digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
            delay(50);
        }
    }
    
    dbg_println("Initializing SystemCommand...");
    if (!SystemCommand::getInstance().begin()) {
        dbg_println("ERROR: Failed to initialize SystemCommand!");
        while (1) {
            digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
            delay(75);
        }
    }
    
    dbg_println("System singletons initialized successfully!");
    
    // BLE manager now uses SystemCommand singleton directly - no need to connect to stepper controller
    
    // Start tasks - PowerDeliveryTask must start first
    if (!powerDeliveryTask.start()) {
        dbg_println("Failed to start Power Delivery Task!");
        while (1) {
            digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
            delay(50);
        }
    }
    
    if (!stepperController.start()) {
        dbg_println("Failed to start Stepper Task!");
        while (1) {
            digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
            delay(100);
        }
    }
    
    if (!bleManager.start()) {
        dbg_println("Failed to start BLE Task!");
        while (1) {
            digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
            delay(200);
        }
    }
    
    dbg_println("All tasks started successfully!");
    dbg_println("System initialization complete.");
    
    // Turn on status LED to indicate ready state
    digitalWrite(STATUS_LED_PIN, HIGH);

    setupOTA();
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(100)); // 100ms delay, plenty for LED control

    loopOTA(); // Handle OTA updates if available
}