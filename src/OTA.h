#ifndef OTA_H
#define OTA_H

#include "BoardPins.h"

// OTA update mode is entered by holding SW1 (active low) during boot.
#define OTA_UPDATE_BUTTON_PIN SW1_PIN

/**
 * Sample the OTA button. If it is held, bring up WiFi + ArduinoOTA and block
 * forever servicing update requests (this function then never returns).
 * If the button is not held, returns immediately and normal boot continues.
 *
 * The caller must have configured OTA_UPDATE_BUTTON_PIN as an input and allowed
 * the level to settle before calling this.
 */
void handleOTAButton();

#endif // OTA_H
