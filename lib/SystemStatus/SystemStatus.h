#ifndef COMMUNICATION_MANAGER_H
#define COMMUNICATION_MANAGER_H

/**
 * @file SystemStatus.h
 * @brief Unified manager class for handling both notifications and status updates
 * 
 * This class provides thread-safe communication management using FreeRTOS queues.
 * It handles both notifications (warnings and errors) and status updates in a single
 * unified interface for the stepper motor system.
 */

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "StatusTypes.h"

// Queue size configuration
#define NOTIFICATION_QUEUE_SIZE     10     // Notification queue size (smaller since only warnings/errors)
#define STATUS_UPDATE_QUEUE_SIZE    30     // Status update queue size

class SystemStatus {
private:
    QueueHandle_t notificationQueue;
    QueueHandle_t statusUpdateQueue;
    
    // Singleton implementation
    SystemStatus();
    ~SystemStatus();
    
    // Delete copy constructor and assignment operator
    SystemStatus(const SystemStatus&) = delete;
    SystemStatus& operator=(const SystemStatus&) = delete;
    
public:
    // Singleton access method
    static SystemStatus& getInstance();
    
    // Initialization
    bool begin();
    
    // Notification management (thread-safe)
    void sendNotification(NotificationType type, const String& message = "");
    bool getNotification(NotificationData& notification);
    bool hasNotifications() const;
    UBaseType_t getPendingNotificationCount() const;
    void clearNotifications();
    
    // Status update management (thread-safe, overloaded for different types)
    void publishStatusUpdate(StatusUpdateType type, float value);
    void publishStatusUpdate(StatusUpdateType type, bool value);
    void publishStatusUpdate(StatusUpdateType type, int value);
    void publishStatusUpdate(StatusUpdateType type, uint32_t value);
    void publishStatusUpdate(StatusUpdateType type, unsigned long value);
    
    // Status update retrieval (thread-safe)
    bool getStatusUpdate(StatusUpdateData& status);
    bool hasStatusUpdates() const;
    UBaseType_t getPendingStatusUpdateCount() const;
    void clearStatusUpdates();
};

#endif // COMMUNICATION_MANAGER_H
