#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// Forward declaration
class StepperController;

class BLEManager {
private:
    // BLE Service and Characteristic UUIDs
    static const char* SERVICE_UUID;
    static const char* SPEED_CHARACTERISTIC_UUID;
    static const char* DIRECTION_CHARACTERISTIC_UUID;
    static const char* ENABLE_CHARACTERISTIC_UUID;
    static const char* STATUS_CHARACTERISTIC_UUID;
    static const char* MICROSTEPS_CHARACTERISTIC_UUID;
    static const char* CURRENT_CHARACTERISTIC_UUID;
    static const char* RESET_CHARACTERISTIC_UUID;
    
    // BLE objects
    BLEServer* server;
    BLEService* service;
    BLECharacteristic* speedCharacteristic;
    BLECharacteristic* directionCharacteristic;
    BLECharacteristic* enableCharacteristic;
    BLECharacteristic* statusCharacteristic;
    BLECharacteristic* microstepsCharacteristic;
    BLECharacteristic* currentCharacteristic;
    BLECharacteristic* resetCharacteristic;
    
    // State
    bool deviceConnected;
    bool oldDeviceConnected;
    
    // Reference to stepper controller
    StepperController* stepperController;
    
    // Status update timing
    unsigned long lastStatusUpdate;
    static const unsigned long STATUS_UPDATE_INTERVAL = 1000; // 1 second
    
public:
    BLEManager();
    
    // Initialization
    bool begin(const char* deviceName = "BratenDreher");
    void setStepperController(StepperController* controller);
    
    // Connection status
    bool isConnected() const { return deviceConnected; }
    
    // Status updates
    void updateStatus();
    
    // Main loop
    void update();
    
    // Server callbacks
    class ServerCallbacks;
    friend class ServerCallbacks;
    
    // Characteristic callbacks
    class SpeedCharacteristicCallbacks;
    class DirectionCharacteristicCallbacks;
    class EnableCharacteristicCallbacks;
    class MicrostepsCharacteristicCallbacks;
    class CurrentCharacteristicCallbacks;
    class ResetCharacteristicCallbacks;
    friend class SpeedCharacteristicCallbacks;
    friend class DirectionCharacteristicCallbacks;
    friend class EnableCharacteristicCallbacks;
    friend class MicrostepsCharacteristicCallbacks;
    friend class CurrentCharacteristicCallbacks;
    friend class ResetCharacteristicCallbacks;
};

#endif // BLE_MANAGER_H
