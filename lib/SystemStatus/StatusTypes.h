#ifndef STATUS_TYPES_H
#define STATUS_TYPES_H

/**
 * @file StatusTypes.h
 * @brief Common types and data structures for status updates and notifications
 * 
 * This header contains all the enums and data structures used for inter-task
 * communication of status updates and notifications in the stepper motor system.
 * Separated from StepperController to allow for better modularity and reuse.
 */

#include <Arduino.h>

// Notification types (for warnings and errors only)
enum class NotificationType {
    WARNING,    // Success with warning message
    ERROR       // Error occurred
};

// Status update types for inter-task communication
enum class StatusUpdateType {
    SPEED_SETPOINT_CHANGED,     // User setpoint changed (for UI controls)
    DIRECTION_CHANGED,
    ENABLED_CHANGED,
    CURRENT_CHANGED,
    ACCELERATION_CHANGED,
    SPEED_VARIATION_ENABLED_CHANGED,
    SPEED_VARIATION_STRENGTH_CHANGED,
    SPEED_VARIATION_PHASE_CHANGED,
    // Periodic updates - now separate for each value
    SPEED_UPDATE,
    TOTAL_REVOLUTIONS_UPDATE,
    RUNTIME_UPDATE,
    STALL_DETECTED_UPDATE,
    STALL_COUNT_UPDATE,
    TMC2209_STATUS_UPDATE,
    TMC2209_TEMPERATURE_UPDATE, // TMC2209 temperature status
    // Power Delivery status updates
    PD_NEGOTIATION_STATUS,      // Power delivery negotiation status
    PD_NEGOTIATED_VOLTAGE,      // Negotiated voltage from PD chip
    PD_CURRENT_VOLTAGE,         // Current measured voltage
    PD_POWER_GOOD_STATUS        // Power good signal status
};

// Notification structure (for warnings and errors only)
struct NotificationData {
    NotificationType type;
    char message[128];  // fixed-size buffer to prevent heap fragmentation
    NotificationData() : type(NotificationType::WARNING) { message[0] = '\0'; }
    void setMessage(const char* msg) {
        if (msg) {
            strncpy(message, msg, sizeof(message) - 1);
            message[sizeof(message) - 1] = '\0';
        } else {
            message[0] = '\0';
        }
    }
};

// Status update structure
struct StatusUpdateData {
    StatusUpdateType type;
    union {
        float floatValue;    // for speed, acceleration, revolutions, etc.
        bool boolValue;      // for direction, enabled, etc.
        int intValue;        // for current, counts
        uint32_t uint32Value; // for acceleration
        unsigned long ulongValue; // for runtime, timestamps
    };
    // Helper constructors
    StatusUpdateData() : type(StatusUpdateType::SPEED_UPDATE) {
        floatValue = 0.0f;
    }
    StatusUpdateData(StatusUpdateType t, float value) : type(t) {
        floatValue = value;
    }
    StatusUpdateData(StatusUpdateType t, bool value) : type(t) {
        boolValue = value;
    }
    StatusUpdateData(StatusUpdateType t, int value) : type(t) {
        intValue = value;
    }
    StatusUpdateData(StatusUpdateType t, uint32_t value) : type(t) {
        uint32Value = value;
    }
    StatusUpdateData(StatusUpdateType t, unsigned long value) : type(t) {
        ulongValue = value;
    }
};

#endif // STATUS_TYPES_H
