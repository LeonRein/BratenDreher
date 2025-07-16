# Power Delivery Task Implementation

This implementation adds a comprehensive Power Delivery management system to the BratenDreher project that handles USB-C Power Delivery negotiation and integrates seamlessly with the existing stepper motor control system.

## Overview

The PowerDeliveryTask is a FreeRTOS-based task that:
- Manages USB-C Power Delivery negotiation using the CH224K PD trigger IC
- Monitors power good signals and voltage measurements
- Prevents stepper motor initialization until power delivery is stable
- Publishes power delivery status updates to the web UI via SystemStatus
- Provides thread-safe command interface via SystemCommand

## Architecture

### Key Components

1. **PowerDeliveryTask** (`lib/PowerDeliveryTask/`)
   - Handles PD negotiation and monitoring
   - Integrates with SystemCommand/SystemStatus
   - Runs as independent FreeRTOS task

2. **Extended StatusTypes** (`lib/SystemStatus/StatusTypes.h`)
   - Added PD-specific status update types
   - Power delivery state reporting

3. **Extended CommandTypes** (`lib/SystemCommand/CommandTypes.h`)  
   - Added PowerDeliveryCommand enumeration
   - PowerDeliveryCommandData structure

4. **Modified StepperController** (`lib/StepperController/`)
   - Waits for power delivery negotiation before initialization
   - Automatically disables motor if power is lost
   - Checks power status before enabling motor

5. **Enhanced Web UI** (`web/`)
   - Real-time power delivery status display
   - Voltage negotiation controls
   - Power good indication

## Power Delivery States

The system tracks the following power delivery states:

```cpp
enum class PDNegotiationState {
    IDLE,                   // Not negotiating
    NEGOTIATING,           // Negotiation in progress  
    SUCCESS,               // Negotiation successful
    FAILED,                // Negotiation failed
    TIMEOUT                // Negotiation timed out
};
```

## Hardware Integration

### PD-Stepper Board Pins

```cpp
#define PG_PIN              15  // Power good signal (active low)
#define CFG1_PIN            38  // PD configuration pin 1
#define CFG2_PIN            48  // PD configuration pin 2
#define CFG3_PIN            47  // PD configuration pin 3
#define VBUS_PIN            4   // Voltage measurement (analog)
```

### Voltage Configuration

| Voltage | CFG1 | CFG2 | CFG3 |
|---------|------|------|------|
| 5V      | 1    | -    | -    |
| 9V      | 0    | 0    | 0    |
| 12V     | 0    | 0    | 1    |
| 15V     | 0    | 1    | 1    |
| 20V     | 0    | 1    | 0    |

## Usage Examples

### Basic Usage

```cpp
#include "PowerDeliveryTask.h"

void setup() {
    // Initialize system singletons
    SystemStatus::getInstance().begin();
    SystemCommand::getInstance().begin();
    
    // Start power delivery task
    PowerDeliveryTask& pd = PowerDeliveryTask::getInstance();
    pd.start();
    
    // Power delivery will automatically start negotiating default voltage (12V)
}

void loop() {
    PowerDeliveryTask& pd = PowerDeliveryTask::getInstance();
    
    if (pd.isPowerGood()) {
        Serial.println("Power delivery is ready!");
        Serial.printf("Voltage: %.1fV\n", pd.getCurrentVoltage());
    }
}
```

### Voltage Control

```cpp
// Change target voltage and renegotiate
PowerDeliveryTask::getInstance().setTargetVoltage(20); // 20V

// Check negotiation status
if (pd.isNegotiationComplete()) {
    Serial.printf("Negotiated: %dV\n", pd.getNegotiatedVoltage());
}
```

### Integration with Stepper Control

The StepperController automatically:
- Waits for power delivery before initializing
- Monitors power status during operation
- Disables motor if power is lost

```cpp
// StepperController will not initialize until power delivery is ready
StepperController stepperController;
stepperController.start(); // Blocks until PD negotiation complete
```

## Web UI Integration

The web interface displays real-time power delivery status:

- **Negotiation Status**: Current state of PD negotiation
- **Power Good**: Real-time power good signal status  
- **Negotiated Voltage**: Successfully negotiated voltage level
- **Current Voltage**: Live voltage measurement
- **Voltage Selection**: Dropdown to change target voltage

### Status Updates

The following status updates are published to the web UI:

```cpp
StatusUpdateType::PD_NEGOTIATION_STATUS    // Negotiation state (0-4)
StatusUpdateType::PD_NEGOTIATED_VOLTAGE    // Negotiated voltage (V)
StatusUpdateType::PD_CURRENT_VOLTAGE       // Measured voltage (V)  
StatusUpdateType::PD_POWER_GOOD_STATUS     // Power good signal (bool)
```

## Safety Features

1. **Power Validation**: Stepper motor cannot be enabled without stable power
2. **Automatic Shutdown**: Motor automatically disabled if power is lost
3. **Negotiation Timeout**: 5-second timeout prevents infinite waiting
4. **Debounced Power Good**: 100ms debounce prevents false triggers
5. **Voltage Monitoring**: Continuous voltage measurement and reporting

## Configuration

### Timing Configuration

```cpp
#define PD_STATUS_UPDATE_INTERVAL       500     // Status updates every 500ms
#define PD_VOLTAGE_MEASURE_INTERVAL     100     // Voltage measurement every 100ms  
#define PD_NEGOTIATION_TIMEOUT          5000    // 5 second negotiation timeout
#define PD_POWER_GOOD_DEBOUNCE          100     // 100ms power good debounce
```

### Task Configuration

```cpp
PowerDeliveryTask() 
    : Task("PowerDeliveryTask", 4096, 2, 1)  // 4KB stack, priority 2, core 1
```

## Testing

Use the provided example (`examples/PowerDeliveryDemo/`) to test the power delivery functionality:

```bash
# Upload the demo
pio run -t upload -e esp32-s3-devkitc-1

# Monitor serial output  
pio device monitor
```

The demo will show:
- Power delivery negotiation progress
- Real-time voltage measurements
- Integration with stepper controller
- Status LED indication

## Benefits

1. **Safe Operation**: Ensures stable power before motor operation
2. **Real-time Monitoring**: Continuous power status feedback
3. **Thread-safe**: Integrates cleanly with existing architecture
4. **Modular Design**: Self-contained task with clean interfaces
5. **Web Integration**: Full status visibility in web interface
6. **Flexible**: Easy to modify voltage targets and timing
7. **Robust**: Handles negotiation failures and power loss gracefully

This implementation provides a production-ready power delivery management system that enhances the safety and reliability of the BratenDreher stepper motor control system.
