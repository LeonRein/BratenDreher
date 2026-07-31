#include <Arduino.h>
#include "BoardPins.h"
#include "StepperController.h"
#include "BLEManager.h"
#include "SystemStatus.h"
#include "SystemCommand.h"
#include "PowerDeliveryTask.h"
#include "dbg_print.h"
#include "OTA.h"

// Global task objects
StepperController& stepperController = StepperController::getInstance();
BLEManager& bleManager = BLEManager::getInstance();
PowerDeliveryTask& powerDeliveryTask = PowerDeliveryTask::getInstance();

// Status LED (LED1 on the PD-Stepper board - NOT LED_BUILTIN, which would be
// GPIO 48 == CFG2 of the PD trigger).
static const int STATUS_LED_PIN = LED1_PIN;

// Blink the status LED forever to signal an unrecoverable init failure.
// The blink interval identifies which subsystem failed.
static void fatalError(const char* what, unsigned long blinkIntervalMs) {
    dbg_printf("FATAL: %s\n", what);
    while (true) {
        digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
        delay(blinkIntervalMs);
    }
}

void setup() {
    // Configure the status LED first so fatalError() can signal problems.
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);

    // The OTA button (SW1) is active low with an external pull-up. Configure it
    // and let the level settle before sampling, otherwise a floating read can
    // trap the board in the OTA loop at every boot.
    pinMode(OTA_UPDATE_BUTTON_PIN, INPUT);
    delay(200);

    // Initialize USB CDC for ESP32-S3
    Serial.begin(115200);

    handleOTAButton(); // Does not return while OTA mode is active

    dbg_println();
    dbg_println("=== BratenDreher Stepper Control ===");
    dbg_println("ESP32-S3 USB CDC Serial initialized");
    dbg_println("Initializing system with Task-based architecture...");

    // Initialize singleton managers before starting tasks
    dbg_println("Initializing SystemStatus...");
    if (!SystemStatus::getInstance().begin()) {
        fatalError("Failed to initialize SystemStatus!", 50);
    }

    dbg_println("Initializing SystemCommand...");
    if (!SystemCommand::getInstance().begin()) {
        fatalError("Failed to initialize SystemCommand!", 75);
    }

    dbg_println("System singletons initialized successfully!");

    // Start tasks - PowerDeliveryTask must start first so the stepper task can
    // wait for a negotiated supply voltage.
    if (!powerDeliveryTask.start()) {
        fatalError("Failed to start Power Delivery Task!", 50);
    }

    if (!stepperController.start()) {
        fatalError("Failed to start Stepper Task!", 100);
    }

    if (!bleManager.start()) {
        fatalError("Failed to start BLE Task!", 200);
    }

    dbg_println("All tasks started successfully!");
    dbg_println("System initialization complete.");

    // Turn on status LED to indicate ready state
    digitalWrite(STATUS_LED_PIN, HIGH);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(100));
}
