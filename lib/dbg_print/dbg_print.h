#ifndef DBG_PRINT_H
#define DBG_PRINT_H

#include <Arduino.h>

/**
 * @file dbg_print.h
 * @brief Serial logging macros.
 *
 * dbg_*  - verbose tracing, compiled out entirely unless DEBUG is defined.
 * info_* - always compiled in; use sparingly for messages that matter in
 *          production (startup banner, OTA progress, fatal errors).
 *
 * Build the `esp32-s3-devkitm-1-debug` environment to enable the dbg_* output.
 */

// info_* is always available, in every build configuration.
#define info_print(...) Serial.print(__VA_ARGS__)
#define info_printf(...) Serial.printf(__VA_ARGS__)
#define info_println(...) Serial.println(__VA_ARGS__)

#ifdef DEBUG
#define dbg_print(...) Serial.print(__VA_ARGS__)
#define dbg_printf(...) Serial.printf(__VA_ARGS__)
#define dbg_println(...) Serial.println(__VA_ARGS__)
#else
// Arguments are discarded entirely, so anything referenced only by a dbg_*
// call must not be declared outside of the call itself - it would become an
// unused variable. Inline such expressions into the dbg_* arguments.
#define dbg_print(...) ((void)0)
#define dbg_printf(...) ((void)0)
#define dbg_println(...) ((void)0)
#endif

#endif // DBG_PRINT_H
