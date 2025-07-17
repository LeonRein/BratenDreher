#ifndef COMMAND_TYPES_H
#define COMMAND_TYPES_H

// Command types for inter-task communication
enum class StepperCommand {
    SET_SPEED,
    SET_DIRECTION,
    ENABLE,
    DISABLE,
    EMERGENCY_STOP,
    SET_CURRENT,
    SET_ACCELERATION,
    RESET_COUNTERS,
    RESET_STALL_COUNT,
    SET_SPEED_VARIATION,
    SET_SPEED_VARIATION_PHASE,
    ENABLE_SPEED_VARIATION,
    DISABLE_SPEED_VARIATION,
    SET_STALLGUARD_THRESHOLD,   // Set StallGuard threshold (0-63)
    REQUEST_ALL_STATUS  // Request all current status values
};

// Power delivery command types
enum class PowerDeliveryCommand {
    SET_TARGET_VOLTAGE,         // Set target voltage for PD negotiation
    AUTO_NEGOTIATE_HIGHEST,     // Automatically find and negotiate highest available voltage
    REQUEST_ALL_STATUS              // Request all status updates
};

// Command data structure
struct StepperCommandData {
    StepperCommand command;
    union {
        float floatValue;    // for speed
        bool boolValue;      // for direction, enable/disable
        int intValue;        // for microsteps, current
        uint32_t uint32Value; // for acceleration
    };
    
    // Helper constructors
    StepperCommandData() : command(StepperCommand::ENABLE) {
        floatValue = 0.0f;
    }
    StepperCommandData(StepperCommand cmd) : command(cmd) {
        floatValue = 0.0f;
    }
    StepperCommandData(StepperCommand cmd, float value) : command(cmd) {
        floatValue = value;
    }
    StepperCommandData(StepperCommand cmd, bool value) : command(cmd) {
        boolValue = value;
    }
    StepperCommandData(StepperCommand cmd, int value) : command(cmd) {
        intValue = value;
    }
    StepperCommandData(StepperCommand cmd, uint32_t value) : command(cmd) {
        uint32Value = value;
    }
};

// Power delivery command data structure
struct PowerDeliveryCommandData {
    PowerDeliveryCommand command;
    union {
        float floatValue;    // for voltage setting
        bool boolValue;      // for enable/disable
        int intValue;        // for voltage selection
    };
    
    PowerDeliveryCommandData() : command(PowerDeliveryCommand::REQUEST_ALL_STATUS) {
        floatValue = 0.0f;
    }
    PowerDeliveryCommandData(PowerDeliveryCommand cmd) : command(cmd) {
        floatValue = 0.0f;
    }
    PowerDeliveryCommandData(PowerDeliveryCommand cmd, float value) : command(cmd) {
        floatValue = value;
    }
    PowerDeliveryCommandData(PowerDeliveryCommand cmd, bool value) : command(cmd) {
        boolValue = value;
    }
    PowerDeliveryCommandData(PowerDeliveryCommand cmd, int value) : command(cmd) {
        intValue = value;
    }
};

#endif // COMMAND_TYPES_H