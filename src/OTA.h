#include <ArduinoOTA.h>
#include "../../secrets.h"

void setupOTA()
{
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("BratenDreher");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.onEvent([](WiFiEvent_t event)
               {
        if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
          dbg_print("IP address: ");
          Serial.println(WiFi.localIP());
        } });

  ArduinoOTA
      .onStart([]()
               {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH) {
        type = "sketch";
      } else {  // U_SPIFFS
        type = "filesystem";
      }

      stepperController.stop();
      bleManager.stop();
      powerDeliveryTask.stop();

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type); })
      .onEnd([]()
             { Serial.println("\nEnd"); })
      .onProgress([](unsigned int progress, unsigned int total)
                  { dbg_printf("Progress: %u%%\r", (progress / (total / 100))); })
      .onError([](ota_error_t error)
               {
      dbg_printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) {
        Serial.println("Auth Failed");
      } else if (error == OTA_BEGIN_ERROR) {
        Serial.println("Begin Failed");
      } else if (error == OTA_CONNECT_ERROR) {
        Serial.println("Connect Failed");
      } else if (error == OTA_RECEIVE_ERROR) {
        Serial.println("Receive Failed");
      } else if (error == OTA_END_ERROR) {
        Serial.println("End Failed");
      } });

  ArduinoOTA.setHostname("BratenDreher");
  ArduinoOTA.begin();
}

void loopOTA()
{
  ArduinoOTA.handle();
}