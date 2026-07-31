#include "OTA.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>

#include "secrets.h"
#include "BLEManager.h"
#include "PowerDeliveryTask.h"
#include "StepperController.h"
#include "SystemCommand.h"
#include "dbg_print.h"

static void setupOTA() {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("BratenDreher");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    WiFi.onEvent([](WiFiEvent_t event) {
        if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
            info_print("IP address: ");
            info_println(WiFi.localIP());
        }
    });

    ArduinoOTA
        .onStart([]() {
            const char* type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";

            // Stop the motor and shut the tasks down before flashing. These are
            // no-ops when OTA was entered from boot, since the tasks were never
            // started in that path.
            SystemCommand::getInstance().sendCommand(StepperCommand::DISABLE);
            BLEManager::getInstance().stop();
            PowerDeliveryTask::getInstance().stop();
            delay(100); // Allow time for the stepper to stop
            StepperController::getInstance().stop();

            info_printf("Start updating %s\n", type);
        })
        .onEnd([]() { info_println("\nOTA update complete"); })
        .onProgress([](unsigned int progress, unsigned int total) {
            // Guard against total < 100, which would divide by zero.
            const unsigned int percent = (total > 0) ? (unsigned int)((uint64_t)progress * 100 / total) : 0;
            info_printf("Progress: %u%%\r", percent);
        })
        .onError([](ota_error_t error) {
            const char* reason;
            switch (error) {
                case OTA_AUTH_ERROR:    reason = "Auth Failed";    break;
                case OTA_BEGIN_ERROR:   reason = "Begin Failed";   break;
                case OTA_CONNECT_ERROR: reason = "Connect Failed"; break;
                case OTA_RECEIVE_ERROR: reason = "Receive Failed"; break;
                case OTA_END_ERROR:     reason = "End Failed";     break;
                default:                reason = "Unknown";        break;
            }
            info_printf("OTA Error[%u]: %s\n", error, reason);
        });

    ArduinoOTA.setHostname("BratenDreher");
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.begin();
}

void handleOTAButton() {
    // SW1 is active low with an external pull-up.
    if (digitalRead(OTA_UPDATE_BUTTON_PIN) != LOW) {
        return;
    }

    info_println("OTA update button pressed, starting OTA...");
    setupOTA();

    while (true) {
        ArduinoOTA.handle();
        delay(100);
    }
}
