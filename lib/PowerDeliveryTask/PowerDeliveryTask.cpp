#include "PowerDeliveryTask.h"

// Singleton instance
static PowerDeliveryTask* pdTaskInstance = nullptr;

PowerDeliveryTask::PowerDeliveryTask() 
    : Task("PowerDeliveryTask", 4096, 2, 1), // Stack: 4KB, Priority: 2, Core: 1
      targetVoltage(PD_VOLTAGE_12V),
      negotiatedVoltage(0),
      currentVoltage(0.0f),
      powerGoodState(false),
      lastPowerGoodState(false),
      powerGoodDebounceTime(0),
      negotiationState(PDNegotiationState::IDLE),
      negotiationStartTime(0),
      lastStatusUpdate(0),
      lastVoltageUpdate(0),
      isInitialized(false) {
    
    pdTaskInstance = this;
}

PowerDeliveryTask& PowerDeliveryTask::getInstance() {
    if (pdTaskInstance == nullptr) {
        pdTaskInstance = new PowerDeliveryTask();
    }
    return *pdTaskInstance;
}

void PowerDeliveryTask::run() {
    Serial.println("PowerDeliveryTask: Starting...");
    
    // Initialize hardware and load settings
    initializeHardware();
    loadSettings();
    
    isInitialized = true;
    Serial.println("PowerDeliveryTask: Initialization complete");
    
    // Start with default voltage negotiation
    startNegotiation(targetVoltage);
    
    // Main task loop
    while (true) {
        unsigned long currentTime = millis();
        
        // Process incoming commands
        processCommands();
        
        // Check power good state with debouncing (publishes on change)
        checkPowerGood();
        
        // Update negotiation state (publishes on change)
        updateNegotiationState();
        
        // Measure voltage at regular intervals (publishes on measurement)
        if (currentTime - lastVoltageUpdate >= PD_VOLTAGE_MEASURE_INTERVAL) {
            measureVoltage();
            lastVoltageUpdate = currentTime;
        }
        
        // Small delay to prevent overwhelming the system
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void PowerDeliveryTask::initializeHardware() {
    Serial.println("PowerDeliveryTask: Initializing hardware pins...");
    
    // Initialize PD control pins
    pinMode(PG_PIN, INPUT);
    pinMode(CFG1_PIN, OUTPUT);
    pinMode(CFG2_PIN, OUTPUT);
    pinMode(CFG3_PIN, OUTPUT);
    
    // Initialize analog pins
    pinMode(VBUS_PIN, INPUT);
    pinMode(NTC_PIN, INPUT);
    
    // Set default configuration (12V)
    configureVoltage(PD_VOLTAGE_12V);
    
    Serial.println("PowerDeliveryTask: Hardware initialization complete");
}

void PowerDeliveryTask::loadSettings() {
    preferences.begin("pd_settings", true); // read-only
    targetVoltage = preferences.getInt("target_voltage", PD_VOLTAGE_12V);
    preferences.end();
    
    Serial.printf("PowerDeliveryTask: Loaded target voltage: %dV\n", targetVoltage);
}

void PowerDeliveryTask::saveSettings() {
    preferences.begin("pd_settings", false); // read-write
    preferences.putInt("target_voltage", targetVoltage);
    preferences.end();
    
    Serial.printf("PowerDeliveryTask: Saved target voltage: %dV\n", targetVoltage);
}

void PowerDeliveryTask::configureVoltage(int voltage) {
    Serial.printf("PowerDeliveryTask: Configuring for %dV\n", voltage);
    
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
            Serial.printf("PowerDeliveryTask: Invalid voltage %dV, using 12V\n", voltage);
            voltage = PD_VOLTAGE_12V;
            digitalWrite(CFG1_PIN, LOW);
            digitalWrite(CFG2_PIN, LOW);
            digitalWrite(CFG3_PIN, HIGH);
            break;
    }
}

void PowerDeliveryTask::measureVoltage() {
    int adcValue = analogRead(VBUS_PIN);
    currentVoltage = adcValue * (VREF / ADC_RESOLUTION) / DIV_RATIO;
    
    SystemStatus::getInstance().publishStatusUpdate(StatusUpdateType::PD_CURRENT_VOLTAGE, currentVoltage);
}

bool PowerDeliveryTask::checkPowerGood() {
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
            Serial.printf("PowerDeliveryTask: Power Good state changed to: %s\n", 
                         powerGoodState ? "GOOD" : "BAD");
            
            // Publish power good status
            SystemStatus::getInstance().publishStatusUpdate(StatusUpdateType::PD_POWER_GOOD_STATUS, powerGoodState);
            
            // Update negotiation state based on power good
            if (powerGoodState && negotiationState == PDNegotiationState::NEGOTIATING) {
                negotiationState = PDNegotiationState::SUCCESS;
                negotiatedVoltage = targetVoltage;
                Serial.printf("PowerDeliveryTask: Negotiation successful at %dV\n", negotiatedVoltage);
                SystemStatus::getInstance().publishStatusUpdate(StatusUpdateType::PD_NEGOTIATED_VOLTAGE, (float)negotiatedVoltage);
            } else if (!powerGoodState && negotiationState == PDNegotiationState::SUCCESS) {
                // Power lost, reset negotiation state
                negotiationState = PDNegotiationState::FAILED;
                negotiatedVoltage = 0;
                SystemStatus::getInstance().sendNotification(NotificationType::WARNING, "Power delivery lost");
            }
        }
    }
    
    return powerGoodState;
}

void PowerDeliveryTask::updateNegotiationState() {
    unsigned long currentTime = millis();
    
    // Check for negotiation timeout
    if (negotiationState == PDNegotiationState::NEGOTIATING) {
        if ((currentTime - negotiationStartTime) >= PD_NEGOTIATION_TIMEOUT) {
            negotiationState = PDNegotiationState::TIMEOUT;
            Serial.println("PowerDeliveryTask: Negotiation timed out");
            
            // Publish negotiation status change
            SystemStatus::getInstance().publishStatusUpdate(StatusUpdateType::PD_NEGOTIATION_STATUS, (float)static_cast<int>(negotiationState));
            SystemStatus::getInstance().sendNotification(NotificationType::ERROR, "PD negotiation timeout");
        }
    }
}

void PowerDeliveryTask::publishStatusUpdates() {
    // Publish all current status values (used for initial status and request all status)
    SystemStatus::getInstance().publishStatusUpdate(StatusUpdateType::PD_NEGOTIATION_STATUS, (float)static_cast<int>(negotiationState));
    SystemStatus::getInstance().publishStatusUpdate(StatusUpdateType::PD_POWER_GOOD_STATUS, powerGoodState);
    SystemStatus::getInstance().publishStatusUpdate(StatusUpdateType::PD_CURRENT_VOLTAGE, currentVoltage);
    
    // Publish negotiated voltage (0 if not negotiated)
    SystemStatus::getInstance().publishStatusUpdate(StatusUpdateType::PD_NEGOTIATED_VOLTAGE, (float)negotiatedVoltage);
}

void PowerDeliveryTask::processCommands() {
    PowerDeliveryCommandData command;
    
    // Process all pending commands
    while (SystemCommand::getInstance().getPowerDeliveryCommand(command, 0)) {
        switch (command.command) {
            case PowerDeliveryCommand::SET_TARGET_VOLTAGE:
                targetVoltage = command.intValue;
                startNegotiation(targetVoltage);
                saveSettings();
                Serial.printf("PowerDeliveryTask: Target voltage set to %dV\n", targetVoltage);
                break;
                
            case PowerDeliveryCommand::REQUEST_ALL_STATUS:
                publishStatusUpdates();
                break;
                
            default:
                Serial.printf("PowerDeliveryTask: Unknown command: %d\n", static_cast<int>(command.command));
                break;
        }
    }
}

bool PowerDeliveryTask::startNegotiation(int voltage) {
    if (voltage < PD_VOLTAGE_5V || voltage > PD_VOLTAGE_20V) {
        Serial.printf("PowerDeliveryTask: Invalid voltage %dV for negotiation\n", voltage);
        return false;
    }
    
    Serial.printf("PowerDeliveryTask: Starting negotiation for %dV\n", voltage);
    
    negotiationState = PDNegotiationState::NEGOTIATING;
    negotiationStartTime = millis();
    targetVoltage = voltage;
    
    // Configure hardware for target voltage
    configureVoltage(voltage);
    
    // Publish status update
    SystemStatus::getInstance().publishStatusUpdate(StatusUpdateType::PD_NEGOTIATION_STATUS, (float)static_cast<int>(negotiationState));
    
    return true;
}

bool PowerDeliveryTask::isNegotiationComplete() const {
    return (negotiationState == PDNegotiationState::SUCCESS || 
            negotiationState == PDNegotiationState::FAILED ||
            negotiationState == PDNegotiationState::TIMEOUT);
}

bool PowerDeliveryTask::isPowerGood() const {
    return powerGoodState;
}

float PowerDeliveryTask::getCurrentVoltage() const {
    return currentVoltage;
}

int PowerDeliveryTask::getNegotiatedVoltage() const {
    return negotiatedVoltage;
}

PDNegotiationState PowerDeliveryTask::getNegotiationState() const {
    return negotiationState;
}
