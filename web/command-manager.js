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

        // Automatic reconnection with exponential backoff
        this.reconnectTimer = null;
        this.reconnectAttempt = 0;
        this.maxReconnectDelay = 30000;
        
        // Timeout and retry handling
        this.pendingCommands = new Map(); // Track pending commands for timeout/retry
        this.commandTimeout = 5000; // 5 second timeout
        this.maxCommandRetries = 1; // Number of retries (1 = 2 attempts total)
        
        // Event callbacks
        this.onConnectionChange = null;
        this.onStatusUpdate = null;
        this.onNotification = null;
        
        // Bind the event handlers once so they can be added and removed by
        // identity - re-creating them would make removeEventListener a no-op
        // and stack up duplicate handlers on every reconnect.
        this.onDisconnectedHandler = () => {
            console.log('GATT server disconnected event received');
            this.onDisconnected();
        };
        this.onCharacteristicValueChangedHandler = (event) => {
            this.handleMessage(event);
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
            
            // Get command characteristic and subscribe
            console.log('Getting command characteristic...');
            await this.subscribeToCharacteristic();
            console.log('✓ Command characteristic found, notifications enabled');

            this.connected = true;
            this.reconnectAttempt = 0;
            this.updateConnectionStatus('Connected');
            
            // Request all current status to synchronize
            console.log('Requesting current status...');
            await this.sendCommand('ras', null);
            
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

    // Resolve the command characteristic and (re)attach listeners exactly once.
    // Listeners are removed before being added so repeated connects cannot
    // accumulate duplicate handlers, which would process every message twice.
    async subscribeToCharacteristic() {
        this.commandCharacteristic = await this.service.getCharacteristic(this.commandCharacteristicUUID);

        this.commandCharacteristic.removeEventListener(
            'characteristicvaluechanged', this.onCharacteristicValueChangedHandler);
        this.commandCharacteristic.addEventListener(
            'characteristicvaluechanged', this.onCharacteristicValueChangedHandler);

        await this.commandCharacteristic.startNotifications();

        this.device.removeEventListener('gattserverdisconnected', this.onDisconnectedHandler);
        this.device.addEventListener('gattserverdisconnected', this.onDisconnectedHandler);
    }

    async disconnect() {
        console.log('User initiated disconnect');
        this.intentionalDisconnect = true;

        // Cancel any pending automatic reconnect
        if (this.reconnectTimer) {
            clearTimeout(this.reconnectTimer);
            this.reconnectTimer = null;
        }
        this.reconnectAttempt = 0;

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
            this.scheduleReconnect();
        } else {
            console.log('Intentional disconnect - no automatic reconnection');
            this.intentionalDisconnect = false;
            this.reconnectAttempt = 0;
        }
    }

    // Keep retrying in the background with a backoff, rather than giving up
    // after a single attempt. Cancelled by a successful connect or an
    // intentional disconnect.
    scheduleReconnect() {
        if (this.reconnectTimer) {
            clearTimeout(this.reconnectTimer);
        }

        const delay = Math.min(3000 * Math.pow(2, this.reconnectAttempt), this.maxReconnectDelay);
        this.reconnectAttempt += 1;

        console.log(`Scheduling automatic reconnection in ${delay}ms (attempt ${this.reconnectAttempt})`);
        this.reconnectTimer = setTimeout(async () => {
            this.reconnectTimer = null;
            if (this.connected || this.intentionalDisconnect || !this.device) {
                return;
            }
            await this.handleReconnect(true);
            if (!this.connected && !this.intentionalDisconnect) {
                this.scheduleReconnect();
            }
        }, delay);
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

        await this.subscribeToCharacteristic();
        console.log('Notifications re-enabled');

        this.connected = true;
        this.reconnectAttempt = 0;
        this.updateConnectionStatus('Connected');

        await this.sendCommand('ras', null);
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
            // Use short command types directly
            const command = { type, value, ...additionalParams };
            const commandBuffer = window.msgpack.encode(command);
            await this.commandCharacteristic.writeValue(commandBuffer);
            console.log(`Command sent (MsgPack):`, command);
            
            // Track command for timeout handling (if not a status request)
            if (type !== 'ras') {
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

        if (command.retryCount < this.maxCommandRetries) {
            // Retry attempt
            const attemptNum = command.retryCount + 2; // 1st retry is attempt 2
            console.log(`Retrying command ${command.type} (attempt ${attemptNum}/${this.maxCommandRetries + 1})...`);
            command.retryCount += 1;

            // Retry the command, but do not create a new pendingCommand entry
            setTimeout(() => {
                if (this.commandCharacteristic) {
                    const commandObj = {
                        type: command.type,
                        value: command.value,
                        ...command.additionalParams
                    };
                    const commandBuffer = window.msgpack.encode(commandObj);
                    this.commandCharacteristic.writeValue(commandBuffer)
                        .then(() => {
                            setTimeout(() => {
                                this.handleCommandTimeout(commandId);
                            }, this.commandTimeout);
                        })
                        .catch((error) => {
                            console.error(`Failed to resend ${command.type} command:`, error);
                            this.pendingCommands.delete(commandId);
                        });
                }
            }, 500);
        } else {
            // Max retries reached - give up
            console.warn(`Command ${command.type} failed after ${this.maxCommandRetries + 1} attempts - giving up`);
            this.pendingCommands.delete(commandId);

            // Notify about communication failure
            if (this.onNotification) {
                this.onNotification({
                    level: 'warning',
                    message: `Command ${command.type} failed after ${this.maxCommandRetries + 1} attempts. Check connection.`
                });
            }
        }
    }

    // Message Handling
    handleMessage(event) {
        try {
            const value = event.target.value.buffer ? new Uint8Array(event.target.value.buffer) : new Uint8Array(event.target.value);
            const message = window.msgpack.decode(value);
            
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
            'sp': ['ss'],
            'cs': ['ss'],
            'dir': ['sd'],
            'en': ['en', 'es'],
            'cur': ['sc'],
            'acc': ['sa'],
            'sve': ['sve'],
            'svs': ['svs'],
            'svp': ['svp'],
            'sgt': ['st'],
            'pdns': ['stv', 'anh'],
            'tr': ['rc'],
            'rt': ['rc'],
            'sc': ['rs']
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

    // Inject the toast keyframes once. Called by every toast, so a warning
    // shown before any error still animates.
    ensureToastStyles() {
        if (document.getElementById('toast-styles')) return;

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

    showToast(message, { className, background, durationMs }) {
        this.ensureToastStyles();

        const toast = document.createElement('div');
        toast.className = className;
        toast.textContent = message;
        toast.style.cssText = `
            position: fixed;
            top: 20px;
            right: 20px;
            background: ${background};
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
        }, durationMs);
    }

    showError(message) {
        console.error(message);
        this.showToast(message, {
            className: 'error-toast',
            background: '#ef4444',
            durationMs: 5000
        });
    }

    showWarning(message) {
        console.warn(message);
        this.showToast(message, {
            className: 'warning-toast',
            background: '#f59e0b',
            durationMs: 4000
        });
    }

    // Getters
    isConnected() {
        return this.connected;
    }

    getDevice() {
        return this.device;
    }
}
