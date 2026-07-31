#ifndef BOARD_PINS_H
#define BOARD_PINS_H

/**
 * @file BoardPins.h
 * @brief Central pin map for the PD-Stepper board (ESP32-S3 + CH224K PD trigger + TMC2209)
 *
 * IMPORTANT: This is a custom PCB, NOT an ESP32-S3 devkit. Do not use Arduino
 * convenience macros such as LED_BUILTIN here - on the devkit variant that
 * resolves to GPIO 48, which on this board is the PD trigger's CFG2 pin.
 * Driving it would silently change the negotiated supply voltage.
 *
 * Pin assignments taken from the PD-Stepper V1.1 reference design
 * (https://github.com/Thingsbyjosh/PD-Stepper, Software/Basic_Functionality_Test).
 */

// --- TMC2209 stepper driver ---
#define TMC_EN_PIN 21 // Enable (active low)
#define STEP_PIN 5
#define DIR_PIN 6
#define MS1_PIN 1     // UART address select / microstep config
#define MS2_PIN 2     // UART address select / microstep config
#define SPREAD_PIN 7  // LOW = StealthChop, HIGH = SpreadCycle. Must be LOW for StallGuard.
#define TMC_TX_PIN 17
#define TMC_RX_PIN 18
#define DIAG_PIN 16   // StallGuard / diagnostic output
#define INDEX_PIN 11

// --- CH224K USB-PD trigger ---
#define PG_PIN 15   // Power good, active low
#define CFG1_PIN 38
#define CFG2_PIN 48
#define CFG3_PIN 47

// --- AS5600 hall effect encoder (unused by this firmware) ---
#define ENCODER_SCL_PIN 9
#define ENCODER_SDA_PIN 8

// --- Analog inputs ---
#define VBUS_PIN 4
// NOTE: the reference design lists NTC on GPIO 7, which is the same pin as
// SPREAD. This firmware uses the pin as SPREAD (StealthChop select) because
// StallGuard depends on it; the NTC is not read.

// --- Status LEDs (active high) ---
#define LED1_PIN 10 // General status
#define LED2_PIN 12 // Stall indicator

// --- User buttons (active low, external pull-ups on board) ---
#define SW1_PIN 35
#define SW2_PIN 36
#define SW3_PIN 37

// --- Auxiliary GPIO ---
#define AUX1_PIN 14
#define AUX2_PIN 13

#endif // BOARD_PINS_H
