#include "SystemStatus.h"

// Singleton implementation
SystemStatus& SystemStatus::getInstance() {
    static SystemStatus instance;
    return instance;
}

SystemStatus::SystemStatus() : notificationQueue(nullptr), statusUpdateQueue(nullptr) {
}

SystemStatus::~SystemStatus() {
    if (notificationQueue != nullptr) {
        vQueueDelete(notificationQueue);
        notificationQueue = nullptr;
    }
    if (statusUpdateQueue != nullptr) {
        vQueueDelete(statusUpdateQueue);
        statusUpdateQueue = nullptr;
    }
}

bool SystemStatus::begin() {
    // Create notification queue
    notificationQueue = xQueueCreate(NOTIFICATION_QUEUE_SIZE, sizeof(NotificationData));
    if (notificationQueue == nullptr) {
        Serial.println("ERROR: Failed to create notification queue");
        return false;
    }
    
    // Create status update queue
    statusUpdateQueue = xQueueCreate(STATUS_UPDATE_QUEUE_SIZE, sizeof(StatusUpdateData));
    if (statusUpdateQueue == nullptr) {
        Serial.println("ERROR: Failed to create status update queue");
        vQueueDelete(notificationQueue);
        notificationQueue = nullptr;
        return false;
    }
    
    return true;
}

// Notification management methods
void SystemStatus::sendNotification(NotificationType type, const String& message) {
    if (notificationQueue == nullptr) return;
    
    NotificationData notificationData;
    notificationData.type = type;
    
    // Use efficient string copying without temporary String objects
    const char* msgPtr = message.c_str();
    const size_t msgLen = message.length();
    const size_t maxLen = sizeof(notificationData.message) - 1;
    
    if (msgLen <= maxLen) {
        memcpy(notificationData.message, msgPtr, msgLen);
        notificationData.message[msgLen] = '\0';
    } else {
        memcpy(notificationData.message, msgPtr, maxLen);
        notificationData.message[maxLen] = '\0';
    }
    
    // Non-blocking send to avoid task delays
    xQueueSend(notificationQueue, &notificationData, 0);
}

bool SystemStatus::getNotification(NotificationData& notification) {
    if (notificationQueue == nullptr) return false;
    
    return xQueueReceive(notificationQueue, &notification, 0) == pdTRUE; // Non-blocking
}

bool SystemStatus::hasNotifications() const {
    if (notificationQueue == nullptr) return false;
    
    return uxQueueMessagesWaiting(notificationQueue) > 0;
}

UBaseType_t SystemStatus::getPendingNotificationCount() const {
    if (notificationQueue == nullptr) return 0;
    
    return uxQueueMessagesWaiting(notificationQueue);
}

void SystemStatus::clearNotifications() {
    if (notificationQueue == nullptr) return;
    
    xQueueReset(notificationQueue);
}

// Status update management methods
void SystemStatus::publishStatusUpdate(StatusUpdateType type, float value) {
    if (statusUpdateQueue == nullptr) return;
    
    StatusUpdateData statusData(type, value);
    // Don't block if queue is full - just drop the update
    xQueueSend(statusUpdateQueue, &statusData, 0);
}

void SystemStatus::publishStatusUpdate(StatusUpdateType type, bool value) {
    if (statusUpdateQueue == nullptr) return;
    
    StatusUpdateData statusData(type, value);
    xQueueSend(statusUpdateQueue, &statusData, 0);
}

void SystemStatus::publishStatusUpdate(StatusUpdateType type, int value) {
    if (statusUpdateQueue == nullptr) return;
    
    StatusUpdateData statusData(type, value);
    xQueueSend(statusUpdateQueue, &statusData, 0);
}

void SystemStatus::publishStatusUpdate(StatusUpdateType type, uint32_t value) {
    if (statusUpdateQueue == nullptr) return;
    
    StatusUpdateData statusData(type, value);
    xQueueSend(statusUpdateQueue, &statusData, 0);
}

void SystemStatus::publishStatusUpdate(StatusUpdateType type, unsigned long value) {
    if (statusUpdateQueue == nullptr) return;
    
    StatusUpdateData statusData(type, value);
    xQueueSend(statusUpdateQueue, &statusData, 0);
}

bool SystemStatus::getStatusUpdate(StatusUpdateData& status) {
    if (statusUpdateQueue == nullptr) return false;
    
    return xQueueReceive(statusUpdateQueue, &status, 0) == pdTRUE; // Non-blocking
}

bool SystemStatus::hasStatusUpdates() const {
    if (statusUpdateQueue == nullptr) return false;
    
    return uxQueueMessagesWaiting(statusUpdateQueue) > 0;
}

UBaseType_t SystemStatus::getPendingStatusUpdateCount() const {
    if (statusUpdateQueue == nullptr) return 0;
    
    return uxQueueMessagesWaiting(statusUpdateQueue);
}

void SystemStatus::clearStatusUpdates() {
    if (statusUpdateQueue == nullptr) return;
    
    xQueueReset(statusUpdateQueue);
}
