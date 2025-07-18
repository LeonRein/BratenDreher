/**
 * Command Manager - Handles all BLE communication and command processing
 * Separated from UI controls for better architecture
 */
class CommandManager {
    constructor() {
        // BLE Service and Characteristic UUIDs (must match ESP32)
        this.serviceUUID = '12345678-1234-1234-1234-123456789abc';
        this.commandCharacteristicUUID = '12345678-1234-1234-1234-123456789ab1';
        
        // BLE Connection objects
        this.device = null;
        this.server = null;
        this.service = null;
        this.commandCharacteristic = null;
        
        // Connection state
        this.connected = false;
        this.intentionalDisconnect = false;
        
        // Timeout and retry handling
        this.pendingCommands = new Map(); // Track pending commands for timeout/retry
        this.commandTimeout = 5000; // 5 second timeout
        
        // Event callbacks
        this.onConnectionChange = null;
        this.onStatusUpdate = null;
        this.onNotification = null;
        
        // Bind the disconnect handler
        this.onDisconnectedHandler = () => {
            console.log('GATT server disconnected event received');
            this.onDisconnected();
        };
        
        // Check Web Bluetooth support
        if (!navigator.bluetooth) {
            console.error('Web Bluetooth is not supported in this browser');
            return;
        }
        
        console.log('CommandManager initialized');
    }

    // Connection Management
    async connect() {
        try {
            console.log('Starting connection process...');
            this.intentionalDisconnect = false;
            this.updateConnectionStatus('Connecting...');
            
            // If we already have a device, try to reconnect to it first
            if (this.device) {
                try {
                    console.log('Attempting to reconnect to existing device:', this.device.name);
                    await this.reconnect();
                    return true;
                } catch (error) {
                    console.log('Reconnection failed, requesting new device:', error.message);
                    this.device = null;
                }
            }
            
            // Request device
            console.log('Requesting Bluetooth device with filter: name="BratenDreher"');
            this.device = await navigator.bluetooth.requestDevice({
                filters: [{ name: 'BratenDreher' }],
                optionalServices: [this.serviceUUID]
            });
            
            console.log('Device selected:', this.device.name, 'ID:', this.device.id);
            
            // Connect to GATT server
            console.log('Connecting to GATT server...');
            this.server = await this.device.gatt.connect();
            console.log('Connected to GATT server');
            
            // Get service
            console.log('Getting primary service:', this.serviceUUID);
            this.service = await this.server.getPrimaryService(this.serviceUUID);
            console.log('Service found');
            
            // Get command characteristic
            console.log('Getting command characteristic...');
            this.commandCharacteristic = await this.service.getCharacteristic(this.commandCharacteristicUUID);
            console.log('✓ Command characteristic found');
            
            // Subscribe to notifications
            console.log('Setting up command notifications...');
            await this.commandCharacteristic.startNotifications();
            this.commandCharacteristic.addEventListener('characteristicvaluechanged', (event) => {
                this.handleMessage(event);
            });
            console.log('Command notifications enabled');
            
            // Handle disconnection
            this.device.addEventListener('gattserverdisconnected', this.onDisconnectedHandler);
            
            this.connected = true;
            this.updateConnectionStatus('Connected');
            
            // Request all current status to synchronize
            console.log('Requesting current status...');
            await this.sendCommand('status_request', null);
            
            console.log('Successfully connected to BratenDreher');
            return true;
            
        } catch (error) {
            console.error('Connection failed:', error);
            
            let userMessage = `Connection failed: ${error.message}`;
            if (error.name === 'NotFoundError') {
                userMessage = 'No BratenDreher device found. Make sure the device is powered on and advertising.';
            } else if (error.name === 'NotSupportedError') {
                userMessage = 'Web Bluetooth is not supported or enabled in this browser.';
            } else if (error.name === 'SecurityError') {
                userMessage = 'Bluetooth access denied. Please enable Bluetooth and try again.';
            }
            
            this.showError(userMessage);
            this.updateConnectionStatus('Disconnected');
            return false;
        }
    }

    async disconnect() {
        console.log('User initiated disconnect');
        this.intentionalDisconnect = true;
        
        // Remove the disconnect event listener temporarily
        if (this.device) {
            this.device.removeEventListener('gattserverdisconnected', this.onDisconnectedHandler);
        }
        
        if (this.device && this.device.gatt.connected) {
            await this.device.gatt.disconnect();
        }
        
        // Call onDisconnected manually
        this.onDisconnected();
    }

    onDisconnected() {
        this.connected = false;
        this.server = null;
        this.service = null;
        this.commandCharacteristic = null;

        // Clear pending commands
        this.pendingCommands.clear();

        this.updateConnectionStatus('Disconnected');
        console.log('Disconnected from BratenDreher');

        // Only attempt automatic reconnection if it wasn't intentional
        if (!this.intentionalDisconnect) {
            // Re-add the event listener for future connections
            if (this.device) {
                this.device.addEventListener('gattserverdisconnected', this.onDisconnectedHandler);
            }
            
            setTimeout(() => {
                if (!this.connected && this.device) {
                    console.log('Attempting automatic reconnection...');
                    this.handleReconnect(true);
                }
            }, 3000);
        } else {
            console.log('Intentional disconnect - no automatic reconnection');
            this.intentionalDisconnect = false;
        }
    }

    async reconnect() {
        if (!this.device) {
            throw new Error('No device available for reconnection');
        }

        console.log('Attempting to reconnect...');
        this.updateConnectionStatus('Reconnecting...');

        // Reconnect to GATT server
        this.server = await this.device.gatt.connect();
        console.log('Reconnected to GATT server');
        
        // Get service
        this.service = await this.server.getPrimaryService(this.serviceUUID);
        console.log('Service found');
        
        // Get command characteristic
        this.commandCharacteristic = await this.service.getCharacteristic(this.commandCharacteristicUUID);
        console.log('Command characteristic found');
        
        // Subscribe to notifications
        await this.commandCharacteristic.startNotifications();
        this.commandCharacteristic.addEventListener('characteristicvaluechanged', (event) => {
            this.handleMessage(event);
        });
        console.log('Notifications re-enabled');
        
        // Re-add disconnect event listener
        this.device.removeEventListener('gattserverdisconnected', this.onDisconnectedHandler);
        this.device.addEventListener('gattserverdisconnected', this.onDisconnectedHandler);
        
        this.connected = true;
        this.updateConnectionStatus('Connected');

        await this.sendCommand('status_request', null);
        console.log('Successfully reconnected to BratenDreher');
    }

    async handleReconnect(automatic = false) {
        if (!this.device) {
            console.log('No stored device, attempting fresh connection...');
            try {
                await this.connect();
            } catch (error) {
                console.error('Fresh connection failed:', error);
            }
            return;
        }

        try {
            console.log(`${automatic ? 'Automatic' : 'User initiated'} reconnection...`);
            this.intentionalDisconnect = false;
            this.updateConnectionStatus('Reconnecting...', 'Attempting to reconnect to existing device...');
            await this.reconnect();
        } catch (error) {
            console.error('Reconnection failed:', error);
            if (!automatic) {
                this.showError(`Reconnection failed: ${error.message}. Try connecting again.`);
                this.device = null;
                this.updateConnectionStatus('Disconnected', 'Reconnection failed - Click Connect to try again');
            } else {
                console.log('Automatic reconnection failed, will try again later');
            }
        }
    }

    // Command Sending with Timeout and Retry
    async sendCommand(type, value, additionalParams = {}) {
        if (!this.device || !this.device.gatt || !this.device.gatt.connected) {
            console.error(`Cannot send ${type} command: Device not connected`);
            this.onDisconnected();
            return false;
        }
        
        if (!this.commandCharacteristic) {
            console.error(`Cannot send ${type} command: Characteristic not available`);
            return false;
        }

        try {
            const command = { type, value, ...additionalParams };
            const commandString = JSON.stringify(command);
            await this.commandCharacteristic.writeValue(new TextEncoder().encode(commandString));
            console.log(`Command sent: ${commandString}`);
            
            // Track command for timeout handling (if not a status request)
            if (type !== 'status_request') {
                const commandId = `${type}_${Date.now()}`;
                this.pendingCommands.set(commandId, {
                    type,
                    value,
                    additionalParams,
                    timestamp: Date.now(),
                    retryCount: 0
                });
                
                // Set timeout
                setTimeout(() => {
                    this.handleCommandTimeout(commandId);
                }, this.commandTimeout);
            }
            
            return true;
        } catch (error) {
            console.error(`Failed to send ${type} command:`, error);
            this.handleBLEError(error, `Failed to send ${type} command`);
            return false;
        }
    }

    handleCommandTimeout(commandId) {
        const command = this.pendingCommands.get(commandId);
        if (!command) return; // Command was already processed
        
        if (command.retryCount === 0) {
            // First timeout - attempt retry
            console.log(`Retrying command ${command.type} (attempt 2/2)...`);
            command.retryCount = 1;
            
            // Retry the command
            setTimeout(() => {
                this.sendCommand(command.type, command.value, command.additionalParams);
            }, 500);
        } else {
            // Second timeout - give up
            console.warn(`Command ${command.type} failed after retry - giving up`);
            this.pendingCommands.delete(commandId);
            
            // Notify about communication failure
            if (this.onNotification) {
                this.onNotification({
                    level: 'warning',
                    message: `Command ${command.type} failed after retry. Check connection.`
                });
            }
        }
    }

    // Message Handling
    handleMessage(event) {
        try {
            const value = new TextDecoder().decode(event.target.value);
            const message = JSON.parse(value);
            
            if (message.type === 'status_update') {
                // Clear any pending commands that might be related to this status
                this.clearRelatedPendingCommands(message);
                
                if (this.onStatusUpdate) {
                    this.onStatusUpdate(message);
                }
            } else if (message.type === 'notification') {
                if (this.onNotification) {
                    this.onNotification(message);
                }
            } else {
                console.log('Unknown message type:', message.type);
            }
        } catch (error) {
            console.error('Failed to parse message:', error);
        }
    }

    clearRelatedPendingCommands(statusUpdate) {
        // Clear pending commands when we receive related status updates
        const commandsToRemove = [];
        
        this.pendingCommands.forEach((command, commandId) => {
            // Map status updates to command types
            const isRelated = this.isStatusRelatedToCommand(statusUpdate, command.type);
            if (isRelated) {
                commandsToRemove.push(commandId);
            }
        });
        
        commandsToRemove.forEach(commandId => {
            this.pendingCommands.delete(commandId);
        });
    }

    isStatusRelatedToCommand(statusUpdate, commandType) {
        // Map status update fields to command types
        const statusToCommandMap = {
            'speed': ['speed'],
            'currentSpeed': ['speed'],
            'direction': ['direction'],
            'enabled': ['enable'],
            'current': ['current'],
            'acceleration': ['acceleration'],
            'speedVariationEnabled': ['enable_speed_variation', 'disable_speed_variation'],
            'speedVariationStrength': ['speed_variation_strength'],
            'speedVariationPhase': ['speed_variation_phase'],
            'stallguardThreshold': ['stallguard_threshold'],
            'pdNegotiationStatus': ['pd_voltage', 'pd_auto_negotiate']
        };
        
        for (const [statusKey, relatedCommands] of Object.entries(statusToCommandMap)) {
            if (statusUpdate[statusKey] !== undefined && relatedCommands.includes(commandType)) {
                return true;
            }
        }
        
        return false;
    }

    // Utility Methods
    updateConnectionStatus(status, info = '') {
        if (this.onConnectionChange) {
            this.onConnectionChange(status, info);
        }
    }

    handleBLEError(error, userMessage) {
        console.error('BLE Error:', error);
        
        if (error.message && error.message.includes('GATT Server is disconnected')) {
            console.log('GATT disconnection detected, updating connection state');
            this.onDisconnected();
            this.showError(`${userMessage}. Device disconnected. Please reconnect.`);
        } else {
            this.showError(userMessage);
        }
    }

    showError(message) {
        console.error(message);
        
        // Create a toast notification
        const toast = document.createElement('div');
        toast.className = 'error-toast';
        toast.textContent = message;
        
        toast.style.cssText = `
            position: fixed;
            top: 20px;
            right: 20px;
            background: #ef4444;
            color: white;
            padding: 16px 20px;
            border-radius: 8px;
            box-shadow: 0 4px 12px rgba(0,0,0,0.3);
            z-index: 1000;
            max-width: 350px;
            font-size: 0.9rem;
            animation: slideIn 0.3s ease;
        `;
        
        // Add animation keyframes if not already present
        if (!document.getElementById('toast-styles')) {
            const style = document.createElement('style');
            style.id = 'toast-styles';
            style.textContent = `
                @keyframes slideIn {
                    from { transform: translateX(100%); opacity: 0; }
                    to { transform: translateX(0); opacity: 1; }
                }
                @keyframes slideOut {
                    from { transform: translateX(0); opacity: 1; }
                    to { transform: translateX(100%); opacity: 0; }
                }
            `;
            document.head.appendChild(style);
        }
        
        document.body.appendChild(toast);
        
        // Remove toast after 5 seconds
        setTimeout(() => {
            toast.style.animation = 'slideOut 0.3s ease';
            setTimeout(() => {
                if (toast.parentNode) {
                    toast.parentNode.removeChild(toast);
                }
            }, 300);
        }, 5000);
    }

    showWarning(message) {
        console.warn(message);
        
        const toast = document.createElement('div');
        toast.className = 'warning-toast';
        toast.textContent = message;
        
        toast.style.cssText = `
            position: fixed;
            top: 20px;
            right: 20px;
            background: #f59e0b;
            color: white;
            padding: 16px 20px;
            border-radius: 8px;
            box-shadow: 0 4px 12px rgba(0,0,0,0.3);
            z-index: 1000;
            max-width: 350px;
            font-size: 0.9rem;
            animation: slideIn 0.3s ease;
        `;
        
        document.body.appendChild(toast);
        
        setTimeout(() => {
            toast.style.animation = 'slideOut 0.3s ease';
            setTimeout(() => {
                if (toast.parentNode) {
                    toast.parentNode.removeChild(toast);
                }
            }, 300);
        }, 4000);
    }

    // Getters
    isConnected() {
        return this.connected;
    }

    getDevice() {
        return this.device;
    }
}
