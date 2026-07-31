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

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "Task.h"
#include "BoardPins.h"
#include "SystemStatus.h"
#include <Preferences.h>
#include "SystemCommand.h"
#include "dbg_print.h"

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
#define PD_VOLTAGE_COUNT    5   // Number of entries in autoNegotiationVoltages

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
    Preferences preferences;
    bool powerGoodState;
    bool lastPowerGoodState;
    unsigned long powerGoodDebounceTime;
    PDNegotiationState negotiationState;
    unsigned long negotiationStartTime;
    
    // Auto-negotiation state variables
    int autoNegotiationVoltageIndex;
    static const int autoNegotiationVoltages[PD_VOLTAGE_COUNT]; // Available voltages in descending order
    int autoNegotiationHighestVoltage;

    // Timing variables
    unsigned long lastStatusUpdate;

    // Initialization flag
    bool isInitialized;

    // True only for the five USB-PD levels the CH224K can request
    static bool isSupportedVoltage(int voltage);

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
    void loadSettings();
    void saveSettings();

protected:
    void run() override;
public:
    // Public interface (read-only accessors, safe to call from other tasks).
    // To change the voltage, post a PowerDeliveryCommand via SystemCommand.
    bool isNegotiationComplete() const;
    bool isPowerGood() const;
    float getCurrentVoltage() const;
    int getNegotiatedVoltage() const;
    PDNegotiationState getNegotiationState() const;

    static PowerDeliveryTask& getInstance() {
        static PowerDeliveryTask instance;
        return instance;
    }
};

#endif // POWER_DELIVERY_TASK_H
