/*
 * Power Delivery Task Test Example
 * 
 * This example demonstrates the PowerDeliveryTask functionality:
 * - Starts power delivery negotiation
 * - Monitors power status
 * - Waits for stepper controller to initialize only after power is ready
 * - Displays power delivery status via Serial output
 */

#include <Arduino.h>
#include "PowerDeliveryTask.h"
#include "StepperController.h"
#include "SystemStatus.h"
#include "SystemCommand.h"

// Simple task to demonstrate the power delivery integration
PowerDeliveryTask& powerDeliveryTask = PowerDeliveryTask::getInstance();
StepperController stepperController;

// Status LED
const int STATUS_LED_PIN = LED_BUILTIN;
unsigned long lastStatusPrint = 0;

void setup() {
    delay(200);
    Serial.begin(115200);
    
    Serial.println();
    Serial.println("=== Power Delivery Task Demo ===");
    Serial.println("Demonstrating power delivery integration with stepper control");
    
    // Initialize status LED
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
    
    // Initialize singleton managers
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
    
    // Start PowerDeliveryTask first
    if (!powerDeliveryTask.start()) {
        Serial.println("Failed to start Power Delivery Task!");
        while (1) {
            digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
            delay(100);
        }
    }
    
    // Start StepperController (it will wait for power delivery to be ready)
    if (!stepperController.start()) {
        Serial.println("Failed to start Stepper Task!");
        while (1) {
            digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
            delay(200);
        }
    }
    
    Serial.println("All tasks started successfully!");
    Serial.println("System initialization complete.");
    Serial.println("Monitor the serial output to see power delivery status updates.");
    
    // Turn on status LED to indicate ready state
    digitalWrite(STATUS_LED_PIN, HIGH);
}

void loop() {
    unsigned long currentTime = millis();
    
    // Print power delivery status every 5 seconds
    if (currentTime - lastStatusPrint >= 5000) {
        Serial.println("\n--- Power Delivery Status ---");
        Serial.printf("Negotiation State: %d\n", static_cast<int>(powerDeliveryTask.getNegotiationState()));
        Serial.printf("Power Good: %s\n", powerDeliveryTask.isPowerGood() ? "YES" : "NO");
        Serial.printf("Negotiated Voltage: %dV\n", powerDeliveryTask.getNegotiatedVoltage());
        Serial.printf("Current Voltage: %.1fV\n", powerDeliveryTask.getCurrentVoltage());
        Serial.printf("Negotiation Complete: %s\n", powerDeliveryTask.isNegotiationComplete() ? "YES" : "NO");
        Serial.println("----------------------------\n");
        
        lastStatusPrint = currentTime;
    }
    
    // LED status indication
    if (powerDeliveryTask.isPowerGood()) {
        // Solid LED when power is good
        digitalWrite(STATUS_LED_PIN, HIGH);
    } else {
        // Slow blink when power delivery is not ready
        if (currentTime % 1000 < 500) {
            digitalWrite(STATUS_LED_PIN, HIGH);
        } else {
            digitalWrite(STATUS_LED_PIN, LOW);
        }
    }
    
    // Small delay for cooperative multitasking
    vTaskDelay(pdMS_TO_TICKS(100));
}
