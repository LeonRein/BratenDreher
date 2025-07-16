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
    REQUEST_ALL_STATUS  // Request all current status values
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