#include "PowerDeliveryTask.h"

// Static voltage array for auto-negotiation (highest to lowest)
const int PowerDeliveryTask::autoNegotiationVoltages[5] = {PD_VOLTAGE_20V, PD_VOLTAGE_15V, PD_VOLTAGE_12V, PD_VOLTAGE_9V, PD_VOLTAGE_5V};

PowerDeliveryTask::PowerDeliveryTask() 
    : Task("PowerDeliveryTask", 4096, 2, 1), // Stack: 4KB, Priority: 2, Core: 1
      targetVoltage(PD_VOLTAGE_12V),
      negotiatedVoltage(0),
      powerGoodState(false),
      lastPowerGoodState(false),
      powerGoodDebounceTime(0),
      negotiationState(PDNegotiationState::IDLE),
      negotiationStartTime(0),
      isAutoNegotiating(false),
      autoNegotiationVoltageIndex(0),
      autoNegotiationHighestVoltage(0),
      lastStatusUpdate(0),
      lastVoltageUpdate(0),
      isInitialized(false) {
}

// ============================================================================
// MAIN TASK LOOP
// ============================================================================

void PowerDeliveryTask::run() {
    dbg_println("PowerDeliveryTask: Starting...");
    
    // Initialize hardware and load settings
    initializeHardware();
    
    isInitialized = true;
    dbg_println("PowerDeliveryTask: Initialization complete");
    
    // Start with automatic highest voltage negotiation
    autoNegotiateHighestVoltageInternal();
    
    // Main task loop
    while (true) {
        unsigned long currentTime = millis();
        
        // Process incoming commands
        processCommands();
        
        // Update negotiation state machine
        updateNegotiationState();
        
        // Publish periodic status updates
        if (currentTime - lastStatusUpdate >= PD_STATUS_UPDATE_INTERVAL) {
            publishPeriodicStatusUpdates();
            publishPowerGoodStatus();
            publishVoltageStatus();
            lastStatusUpdate = currentTime;
        }
        
        // Small delay to prevent overwhelming the system
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ============================================================================
// HARDWARE ABSTRACTION LAYER (Pure Hardware Control)
// ============================================================================

void PowerDeliveryTask::pdConfigureVoltage(int voltage) {
    dbg_printf("PowerDeliveryTask: Configuring CFG pins for %dV\n", voltage);
    
    // Configure CFG pins based on desired voltage
    // From PD_Stepper example:
    //                          5V   9V   12V   15V   20V
    // CFG1 (pin 38):           1    0     0     0     0
    // CFG2 (pin 48):           -    0     0     1     1  
    // CFG3 (pin 47):           -    0     1     1     0
    
    switch (voltage) {
        case PD_VOLTAGE_5V:
            digitalWrite(CFG1_PIN, HIGH);
            digitalWrite(CFG2_PIN, LOW);
            digitalWrite(CFG3_PIN, LOW);
            break;
        case PD_VOLTAGE_9V:
            digitalWrite(CFG1_PIN, LOW);
            digitalWrite(CFG2_PIN, LOW);
            digitalWrite(CFG3_PIN, LOW);
            break;
        case PD_VOLTAGE_12V:
            digitalWrite(CFG1_PIN, LOW);
            digitalWrite(CFG2_PIN, LOW);
            digitalWrite(CFG3_PIN, HIGH);
            break;
        case PD_VOLTAGE_15V:
            digitalWrite(CFG1_PIN, LOW);
            digitalWrite(CFG2_PIN, HIGH);
            digitalWrite(CFG3_PIN, HIGH);
            break;
        case PD_VOLTAGE_20V:
            digitalWrite(CFG1_PIN, LOW);
            digitalWrite(CFG2_PIN, HIGH);
            digitalWrite(CFG3_PIN, LOW);
            break;
        default:
            dbg_printf("PowerDeliveryTask: Invalid voltage %dV, using 12V\n", voltage);
            voltage = PD_VOLTAGE_12V;
            digitalWrite(CFG1_PIN, LOW);
            digitalWrite(CFG2_PIN, LOW);
            digitalWrite(CFG3_PIN, HIGH);
            break;
    }
    
    dbg_printf("PowerDeliveryTask: CFG pins configured for %dV\n", voltage);
    
    // Invalidate power good status when voltage configuration changes
    pdInvalidatePowerGood();
}

float PowerDeliveryTask::pdMeasureVoltage() {
    int adcValue = analogRead(VBUS_PIN);
    return (adcValue * VREF / ADC_RESOLUTION) / DIV_RATIO;
}

bool PowerDeliveryTask::pdCheckPowerGood() {
    bool currentPGState = (digitalRead(PG_PIN) == LOW); // PG is active low
    unsigned long currentTime = millis();
    
    // Debounce logic
    if (currentPGState != lastPowerGoodState) {
        powerGoodDebounceTime = currentTime;
        lastPowerGoodState = currentPGState;
    }
    
    if ((currentTime - powerGoodDebounceTime) >= PD_POWER_GOOD_DEBOUNCE) {
        if (powerGoodState != currentPGState) {
            powerGoodState = currentPGState;
            dbg_printf("PowerDeliveryTask: Power Good state changed to: %s\n", 
                         powerGoodState ? "GOOD" : "BAD");
        }
    }
    
    return powerGoodState;
}

void PowerDeliveryTask::pdInvalidatePowerGood() {
    dbg_println("PowerDeliveryTask: Invalidating power good status");
    powerGoodState = false;
    lastPowerGoodState = false;
    powerGoodDebounceTime = millis();
}

// ============================================================================
// APPLY METHODS (Hardware Control + State Updates + Status Publishing)
// ============================================================================

void PowerDeliveryTask::applyNegotiationVoltage(int voltage) {
    if (!isInitialized) {
        dbg_println("WARNING: Cannot start negotiation - hardware not initialized");
        return;
    }
    
    if (voltage < PD_VOLTAGE_5V || voltage > PD_VOLTAGE_20V) {
        dbg_printf("PowerDeliveryTask: Invalid voltage %dV for negotiation\n", voltage);
        return;
    }
    
    dbg_printf("PowerDeliveryTask: Starting negotiation for %dV (previous state: %d)\n", 
                 voltage, static_cast<int>(negotiationState));
    
    // Reset negotiation state and start fresh
    negotiationState = PDNegotiationState::NEGOTIATING;
    negotiationStartTime = millis();
    targetVoltage = voltage;
    negotiatedVoltage = 0; // Reset negotiated voltage until success
    
    // Configure hardware for target voltage
    pdConfigureVoltage(voltage);

    publishNegotiationStatus();
}

// ============================================================================
// PUBLISH METHODS (Status Communication Only)
// ============================================================================

void PowerDeliveryTask::publishNegotiationStatus() {
    SystemStatus::getInstance().publishStatusUpdate(StatusUpdateType::PD_NEGOTIATION_STATUS, static_cast<int>(negotiationState));
    SystemStatus::getInstance().publishStatusUpdate(StatusUpdateType::PD_NEGOTIATED_VOLTAGE, (float)negotiatedVoltage);
}

void PowerDeliveryTask::publishPowerGoodStatus() {
    SystemStatus::getInstance().publishStatusUpdate(StatusUpdateType::PD_POWER_GOOD_STATUS, pdCheckPowerGood());
}

void PowerDeliveryTask::publishVoltageStatus() {
    SystemStatus::getInstance().publishStatusUpdate(StatusUpdateType::PD_CURRENT_VOLTAGE, pdMeasureVoltage());
}

void PowerDeliveryTask::publishPeriodicStatusUpdates() {
    // Publish all current status values in batch
    publishPowerGoodStatus();
    publishVoltageStatus();
}

// ============================================================================
// STATE MACHINE LOGIC
// ============================================================================

void PowerDeliveryTask::updateNegotiationState() {
    unsigned long currentTime = millis();
    
    // Handle different negotiation states
    switch (negotiationState) {
        case PDNegotiationState::NEGOTIATING:
            handleSingleVoltageNegotiation(currentTime);
            break;
            
        case PDNegotiationState::AUTO_NEGOTIATING:
            handleAutoNegotiation(currentTime);
            break;
            
        default:
            // No active negotiation
            break;
    }
}

void PowerDeliveryTask::handleSingleVoltageNegotiation(unsigned long currentTime) {
    // Check for successful negotiation (PG is good)
    bool currentPGState = pdCheckPowerGood();
    if (currentPGState) {
        negotiationState = PDNegotiationState::SUCCESS;
        negotiatedVoltage = targetVoltage;
        dbg_printf("PowerDeliveryTask: Single voltage negotiation successful at %dV\n", negotiatedVoltage);
        
        // Publish immediate status updates
        publishNegotiationStatus();
        publishVoltageStatus();
        return;
    }
    
    // Check for timeout (treat as FAILED)
    if (currentTime - negotiationStartTime >= PD_NEGOTIATION_TIMEOUT) {
        negotiationState = PDNegotiationState::FAILED;
        negotiatedVoltage = 0;
        dbg_printf("PowerDeliveryTask: Single voltage negotiation failed (timeout) after %dms\n", PD_NEGOTIATION_TIMEOUT);
        
        // Publish immediate status updates
        publishNegotiationStatus();
        publishVoltageStatus();
        return;
    }
}

void PowerDeliveryTask::handleAutoNegotiation(unsigned long currentTime) {
    // Check for successful negotiation (PG is good)
    bool currentPGState = pdCheckPowerGood();
    if (currentPGState) {
        // Success! This voltage works
        autoNegotiationHighestVoltage = autoNegotiationVoltages[autoNegotiationVoltageIndex];
        negotiationState = PDNegotiationState::SUCCESS;
        negotiatedVoltage = autoNegotiationHighestVoltage;
        targetVoltage = autoNegotiationHighestVoltage; // Update target to match
        isAutoNegotiating = false;
        
        dbg_printf("PowerDeliveryTask: Auto-negotiation successful! Highest voltage: %dV\n", autoNegotiationHighestVoltage);
        
        // Publish immediate status updates
        publishNegotiationStatus();
        publishVoltageStatus();
        return;
    }
    
    // Check for timeout on current voltage
    if (currentTime - negotiationStartTime >= PD_NEGOTIATION_TIMEOUT) {
        // This voltage failed, try next lower voltage
        autoNegotiationVoltageIndex++;
        
        if (autoNegotiationVoltageIndex >= 5) {
            // All voltages failed
            negotiationState = PDNegotiationState::FAILED;
            negotiatedVoltage = 0;
            isAutoNegotiating = false;
            dbg_println("PowerDeliveryTask: Auto-negotiation failed - no voltages work");
            
            // Publish immediate status updates
            publishNegotiationStatus();
            publishVoltageStatus();
            return;
        }
        
        // Try next voltage
        int nextVoltage = autoNegotiationVoltages[autoNegotiationVoltageIndex];
        dbg_printf("PowerDeliveryTask: Auto-negotiation - trying next voltage: %dV (attempt %d/5)\n", 
                     nextVoltage, autoNegotiationVoltageIndex + 1);
        
        // Configure hardware for next voltage
        pdConfigureVoltage(nextVoltage);
        negotiationStartTime = currentTime; // Reset timeout for new voltage
        
        // Update status to show progress
        publishNegotiationStatus();
    }
}

// ============================================================================
// COMMAND PROCESSING (Internal Methods)
// ============================================================================

void PowerDeliveryTask::processCommands() {
    PowerDeliveryCommandData command;
    
    // Process all pending commands
    while (SystemCommand::getInstance().getPowerDeliveryCommand(command, 0)) {
        switch (command.command) {
            case PowerDeliveryCommand::SET_TARGET_VOLTAGE:
                setTargetVoltageInternal(command.intValue);
                break;
                
            case PowerDeliveryCommand::AUTO_NEGOTIATE_HIGHEST:
                autoNegotiateHighestVoltageInternal();
                break;
                
            case PowerDeliveryCommand::REQUEST_ALL_STATUS:
                requestAllStatusInternal();
                break;
                
            default:
                dbg_printf("PowerDeliveryTask: Unknown command %d\n", static_cast<int>(command.command));
                break;
        }
    }
}

void PowerDeliveryTask::setTargetVoltageInternal(int voltage) {
    // Validate voltage range
    if (voltage < PD_VOLTAGE_5V || voltage > PD_VOLTAGE_20V) {
        dbg_printf("PowerDeliveryTask: Invalid target voltage %dV (allowed: 5V, 9V, 12V, 15V, 20V)\n", voltage);
        
        // Publish current status to indicate no change
        publishNegotiationStatus();
        publishVoltageStatus();
        SystemStatus::getInstance().sendNotification(NotificationType::ERROR, 
            "Invalid target voltage requested: " + String(voltage) + "V");
        return;
    }
    
    // Apply voltage negotiation
    applyNegotiationVoltage(voltage);
    
    dbg_printf("PowerDeliveryTask: Target voltage set to %dV\n", voltage);
}

void PowerDeliveryTask::autoNegotiateHighestVoltageInternal() {
    if (!isInitialized) {
        dbg_println("WARNING: Cannot start auto-negotiation - hardware not initialized");
        return;
    }
    
    dbg_println("PowerDeliveryTask: Starting auto-negotiation for highest available voltage");
    
    // Reset auto-negotiation state
    isAutoNegotiating = true;
    autoNegotiationVoltageIndex = 0; // Start with highest voltage (20V)
    autoNegotiationHighestVoltage = 0;
    negotiationState = PDNegotiationState::AUTO_NEGOTIATING;
    negotiationStartTime = millis();
    negotiatedVoltage = 0; // Reset until we find a working voltage
    
    // Start with the highest voltage
    int startVoltage = autoNegotiationVoltages[0]; // 20V
    targetVoltage = startVoltage; // Set target for status reporting
    
    dbg_printf("PowerDeliveryTask: Auto-negotiation starting with %dV (attempt 1/5)\n", startVoltage);
    
    // Configure hardware for first voltage
    pdConfigureVoltage(startVoltage);
    
    // Publish status update
    publishNegotiationStatus();
}

void PowerDeliveryTask::requestAllStatusInternal() {
    dbg_println("PowerDeliveryTask: Publishing all current status values...");
    
    // Publish all current status values
    publishNegotiationStatus();
    publishPowerGoodStatus();
    publishVoltageStatus();
}

// ============================================================================
// INITIALIZATION AND SETTINGS
// ============================================================================

void PowerDeliveryTask::initializeHardware() {
    dbg_println("PowerDeliveryTask: Initializing hardware pins...");
    
    // Initialize PD control pins
    pinMode(PG_PIN, INPUT);
    pinMode(CFG1_PIN, OUTPUT);
    pinMode(CFG2_PIN, OUTPUT);
    pinMode(CFG3_PIN, OUTPUT);
    
    // Initialize analog pins
    pinMode(VBUS_PIN, INPUT);
    pinMode(NTC_PIN, INPUT);
    
    // Set default configuration (12V)
    pdConfigureVoltage(PD_VOLTAGE_12V);
    
    dbg_println("PowerDeliveryTask: Hardware initialization complete");
}

// ============================================================================
// PUBLIC INTERFACE (Thread-safe accessors)
// ============================================================================

bool PowerDeliveryTask::startNegotiation(int voltage) {
    // This method is kept for compatibility but delegates to command system
    SystemCommand::getInstance().sendPowerDeliveryCommand(PowerDeliveryCommand::SET_TARGET_VOLTAGE, voltage);
    return true;
}

bool PowerDeliveryTask::isNegotiationComplete() const {
    // Check if negotiation is complete (either success or failure)
    // Using integer comparison to avoid enum issues
    int state = static_cast<int>(negotiationState);
    return (state == 2 || state == 3); // SUCCESS=2, FAILED=3
}

bool PowerDeliveryTask::isPowerGood() const {
    // Read fresh power good state from hardware
    return (digitalRead(PG_PIN) == LOW); // PG is active low
}

float PowerDeliveryTask::getCurrentVoltage() const {
    // Read fresh voltage from hardware (const_cast needed for hardware access)
    int adcValue = analogRead(VBUS_PIN);
    return (adcValue * VREF / ADC_RESOLUTION) / DIV_RATIO;
}

int PowerDeliveryTask::getNegotiatedVoltage() const {
    return negotiatedVoltage;
}

PDNegotiationState PowerDeliveryTask::getNegotiationState() const {
    return negotiationState;
}

bool PowerDeliveryTask::setTargetVoltage(int voltage) {
    SystemCommand::getInstance().sendPowerDeliveryCommand(PowerDeliveryCommand::SET_TARGET_VOLTAGE, voltage);
    return true;
}

bool PowerDeliveryTask::autoNegotiateHighestVoltage() {
    SystemCommand::getInstance().sendPowerDeliveryCommand(PowerDeliveryCommand::AUTO_NEGOTIATE_HIGHEST, 0);
    return true;
}

bool PowerDeliveryTask::requestStatus() {
    SystemCommand::getInstance().sendPowerDeliveryCommand(PowerDeliveryCommand::REQUEST_ALL_STATUS, 0);
    return true;
}
