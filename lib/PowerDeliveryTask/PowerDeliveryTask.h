#ifndef POWER_DELIVERY_TASK_H
#define POWER_DELIVERY_TASK_H

/**
 * @file PowerDeliveryTask.h
 * @brief Task for handling USB-C Power Delivery negotiation and monitoring
 * 
 * This task manages the CH224K PD trigger IC, negotiates voltage levels,
 * monitors power good signals, and provides voltage measurements to the system.
 * It integrates with SystemCommand and SystemStatus for thread-safe communication.
 */

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "Task.h"
#include "SystemStatus.h"
#include "SystemCommand.h"
#include "dbg_print.h"

// Hardware pin definitions (from PD-Stepper example)
#define PG_PIN              15  // Power good signal (don't enable stepper until this is good)
#define CFG1_PIN            38  // PD configuration pin 1
#define CFG2_PIN            48  // PD configuration pin 2  
#define CFG3_PIN            47  // PD configuration pin 3
#define VBUS_PIN            4   // Voltage measurement pin
#define NTC_PIN             7   // Temperature sensor pin

// Voltage measurement configuration
#define VREF                3.3f
#define DIV_RATIO           0.1189427313f  // 20k&2.7K Voltage Divider
#define ADC_RESOLUTION      4096.0f

// PD voltage options
#define PD_VOLTAGE_5V       5
#define PD_VOLTAGE_9V       9
#define PD_VOLTAGE_12V      12
#define PD_VOLTAGE_15V      15
#define PD_VOLTAGE_20V      20

// Timing configuration
#define PD_STATUS_UPDATE_INTERVAL       500     // Update every 500ms
#define PD_NEGOTIATION_TIMEOUT          2000    // 2 second timeout for negotiation
#define PD_POWER_GOOD_DEBOUNCE          100     // Debounce power good signal

// Power delivery states
enum class PDNegotiationState {
    IDLE,                   // Not negotiating
    NEGOTIATING,           // Single voltage negotiation in progress
    SUCCESS,               // Negotiation successful
    FAILED,                // Negotiation failed (includes timeout and auto-failed)
    AUTO_NEGOTIATING       // Auto-negotiating highest voltage
};

class PowerDeliveryTask : public Task {
private:
    // PD configuration and state
    int targetVoltage;
    int negotiatedVoltage;
    bool powerGoodState;
    bool lastPowerGoodState;
    unsigned long powerGoodDebounceTime;
    PDNegotiationState negotiationState;
    unsigned long negotiationStartTime;
    
    // Auto-negotiation state variables
    bool isAutoNegotiating;
    int autoNegotiationVoltageIndex;
    static const int autoNegotiationVoltages[5]; // Available voltages in descending order
    int autoNegotiationHighestVoltage;
    
    // Timing variables
    unsigned long lastStatusUpdate;
    unsigned long lastVoltageUpdate;
    
    // Initialization flag
    bool isInitialized;

    // Singleton implementation
    PowerDeliveryTask();
    ~PowerDeliveryTask() {}
    PowerDeliveryTask(const PowerDeliveryTask&) = delete;
    PowerDeliveryTask& operator=(const PowerDeliveryTask&) = delete;

    
    // Hardware abstraction layer (pure hardware control)
    void pdConfigureVoltage(int voltage);
    float pdMeasureVoltage();
    bool pdCheckPowerGood();
    void pdInvalidatePowerGood();
    
    // Apply methods (hardware control + state updates + status publishing)
    void applyNegotiationVoltage(int voltage);
    
    // Publish methods (status communication only)
    void publishNegotiationStatus();
    void publishPowerGoodStatus();
    void publishVoltageStatus();
    void publishPeriodicStatusUpdates();
    
    // State machine and command processing
    void updateNegotiationState();
    void handleSingleVoltageNegotiation(unsigned long currentTime);
    void handleAutoNegotiation(unsigned long currentTime);
    void processCommands();
    
    // Internal command processors (with validation)
    void setTargetVoltageInternal(int voltage);
    void autoNegotiateHighestVoltageInternal();
    void requestAllStatusInternal();
    
    // Initialization and settings
    void initializeHardware();

protected:
    void run() override;
public:
    // Public interface
    bool startNegotiation(int voltage);
    bool isNegotiationComplete() const;
    bool isPowerGood() const;
    float getCurrentVoltage() const;
    int getNegotiatedVoltage() const;
    PDNegotiationState getNegotiationState() const;
    
    // Command interface (thread-safe)
    bool setTargetVoltage(int voltage);
    bool autoNegotiateHighestVoltage();
    bool requestStatus();

    static PowerDeliveryTask& getInstance() {
        static PowerDeliveryTask instance;
        return instance;
    }
};

#endif // POWER_DELIVERY_TASK_H
