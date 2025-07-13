class BratenDreherBLE {
    constructor() {
        // BLE Service and Characteristic UUIDs (must match ESP32)
        this.serviceUUID = '12345678-1234-1234-1234-123456789abc';
        this.commandCharacteristicUUID = '12345678-1234-1234-1234-123456789ab1';
        
        // BLE objects
        this.device = null;
        this.server = null;
        this.service = null;
        this.commandCharacteristic = null;
        
        // State
        this.connected = false;
        this.intentionalDisconnect = false; // Track if user intentionally disconnected
        this.motorSpeed = 1.0;
        this.motorDirection = true; // true = clockwise
        this.motorEnabled = false;
        this.current = 30;
        
        // Debouncing timers for sliders
        this.speedSliderTimer = null;
        this.currentSliderTimer = null;
        this.accelerationSliderTimer = null;
        
        // Bind the disconnect handler so we can add/remove it
        this.onDisconnectedHandler = () => {
            console.log('GATT server disconnected event received');
            this.onDisconnected();
        };
        
        // UI elements
        this.initializeUIElements();
        this.bindEventListeners();
        
        // Check Web Bluetooth support
        if (!navigator.bluetooth) {
            this.showError('Web Bluetooth is not supported in this browser. Please use Chrome, Edge, or Opera.');
            this.connectBtn.disabled = true;
            this.reconnectBtn.disabled = true;
            return;
        }
        
        console.log('BratenDreher BLE Controller initialized');
    }
    
    initializeUIElements() {
        // Connection elements
        this.statusIndicator = document.getElementById('statusIndicator');
        this.connectionStatus = document.getElementById('connectionStatus');
        this.connectionInfo = document.getElementById('connectionInfo');
        this.connectBtn = document.getElementById('connectBtn');
        this.reconnectBtn = document.getElementById('reconnectBtn');
        this.disconnectBtn = document.getElementById('disconnectBtn');
        
        // Control elements
        this.motorToggle = document.getElementById('motorToggle');
        this.speedSlider = document.getElementById('speedSlider');
        this.speedValue = document.getElementById('speedValue');
        this.clockwiseBtn = document.getElementById('clockwiseBtn');
        this.counterclockwiseBtn = document.getElementById('counterclockwiseBtn');
        this.emergencyStopBtn = document.getElementById('emergencyStopBtn');
        this.currentSlider = document.getElementById('currentSlider');
        this.currentValue = document.getElementById('currentValue');
        this.accelerationTimeSlider = document.getElementById('accelerationTimeSlider');
        this.accelerationTimeValue = document.getElementById('accelerationTimeValue');
        this.resetStatsBtn = document.getElementById('resetStatsBtn');
        this.resetStallBtn = document.getElementById('resetStallBtn');
        
        // Status elements
        this.motorStatus = document.getElementById('motorStatus');
        this.currentSpeed = document.getElementById('currentSpeed');
        this.currentDirection = document.getElementById('currentDirection');
        this.currentCurrent = document.getElementById('currentCurrent');
        this.tmc2209Status = document.getElementById('tmc2209Status');
        this.stallStatus = document.getElementById('stallStatus');
        this.stallCount = document.getElementById('stallCount');
        this.lastUpdate = document.getElementById('lastUpdate');
        
        // Statistics elements
        this.totalRevolutions = document.getElementById('totalRevolutions');
        this.runTime = document.getElementById('runTime');
        this.avgSpeed = document.getElementById('avgSpeed');
        
        // Preset buttons
        this.presetBtns = document.querySelectorAll('.preset-btn');
    }
    
    bindEventListeners() {
        // Connection buttons
        this.connectBtn.addEventListener('click', () => this.connect());
        this.reconnectBtn.addEventListener('click', () => this.handleReconnect());
        this.disconnectBtn.addEventListener('click', () => this.disconnect());
        
        // Motor controls
        this.motorToggle.addEventListener('change', (e) => {
            this.setMotorEnabled(e.target.checked);
        });
        
        this.speedSlider.addEventListener('input', (e) => {
            const speed = parseFloat(e.target.value);
            this.speedValue.textContent = speed.toFixed(1); // Update display immediately
            
            // Visual feedback that change is pending
            this.speedValue.style.opacity = '0.7';
            
            // Debounce the actual command sending
            if (this.speedSliderTimer) {
                clearTimeout(this.speedSliderTimer);
            }
            this.speedSliderTimer = setTimeout(() => {
                this.setSpeed(speed).then(() => {
                    // Restore full opacity when command is sent
                    this.speedValue.style.opacity = '1';
                });
            }, 300); // Wait 300ms after user stops moving slider
        });
        
        this.clockwiseBtn.addEventListener('click', () => {
            this.setDirection(true);
        });
        
        this.counterclockwiseBtn.addEventListener('click', () => {
            this.setDirection(false);
        });
        
        this.emergencyStopBtn.addEventListener('click', () => {
            this.emergencyStop();
        });
        
        // Preset buttons
        this.presetBtns.forEach(btn => {
            btn.addEventListener('click', () => {
                const speed = parseFloat(btn.dataset.speed);
                this.setSpeed(speed);
                this.speedSlider.value = speed;
                
                // Update active preset
                this.presetBtns.forEach(b => b.classList.remove('active'));
                btn.classList.add('active');
            });
        });
        
        // Advanced settings
        this.currentSlider.addEventListener('input', (e) => {
            const current = parseInt(e.target.value);
            this.currentValue.textContent = current; // Update display immediately
            
            // Visual feedback that change is pending
            this.currentValue.style.opacity = '0.7';
            
            // Debounce the actual command sending
            if (this.currentSliderTimer) {
                clearTimeout(this.currentSliderTimer);
            }
            this.currentSliderTimer = setTimeout(() => {
                this.setCurrent(current).then(() => {
                    // Restore full opacity when command is sent
                    this.currentValue.style.opacity = '1';
                });
            }, 300); // Wait 300ms after user stops moving slider
        });
        
        this.accelerationTimeSlider.addEventListener('input', (e) => {
            const time = parseInt(e.target.value);
            this.accelerationTimeValue.textContent = time; // Update display immediately
            
            // Visual feedback that change is pending
            this.accelerationTimeValue.style.opacity = '0.7';
            
            // Debounce the actual command sending
            if (this.accelerationSliderTimer) {
                clearTimeout(this.accelerationSliderTimer);
            }
            this.accelerationSliderTimer = setTimeout(() => {
                this.setAccelerationTime(time).then(() => {
                    // Restore full opacity when command is sent
                    this.accelerationTimeValue.style.opacity = '1';
                });
            }, 300); // Wait 300ms after user stops moving slider
        });
        
        this.resetStatsBtn.addEventListener('click', () => {
            this.resetStatistics();
        });
        
        this.resetStallBtn.addEventListener('click', () => {
            this.resetStallCount();
        });
    }
    
    async connect() {
        try {
            console.log('Starting connection process...');
            console.log('Web Bluetooth available:', !!navigator.bluetooth);
            this.intentionalDisconnect = false; // Reset flag when connecting
            this.updateConnectionStatus('Connecting...');
            
            // If we already have a device, try to reconnect to it first
            if (this.device) {
                try {
                    console.log('Attempting to reconnect to existing device:', this.device.name);
                    await this.reconnect();
                    return;
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
            try {
                this.commandCharacteristic = await this.service.getCharacteristic(this.commandCharacteristicUUID);
                console.log('✓ Command characteristic found');
            } catch (error) {
                console.error('✗ Failed to get command characteristic:', error);
                throw new Error(`Missing command characteristic (${this.commandCharacteristicUUID}). Make sure ESP32 firmware is up to date.`);
            }
            
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
            this.updateUI();
            
            // Request initial status
            setTimeout(() => {
                this.requestStatus();
            }, 1000);
            
            console.log('Successfully connected to BratenDreher');
            
        } catch (error) {
            console.error('Connection failed:', error);
            console.error('Error name:', error.name);
            console.error('Error message:', error.message);
            
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
        }
    }
    
    async disconnect() {
        console.log('User initiated disconnect');
        this.intentionalDisconnect = true; // Mark as intentional
        
        // Remove the disconnect event listener temporarily to prevent it from firing
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
        // Don't null the device - keep it for reconnection attempts
        // this.device = null;
        this.server = null;
        this.service = null;
        this.commandCharacteristic = null;

        // Clear any pending slider timers
        this.clearSliderTimers();

        this.updateConnectionStatus('Disconnected');
        this.updateUI();

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
                    this.handleReconnect(true); // Pass true to indicate automatic reconnection
                }
            }, 3000);
        } else {
            console.log('Intentional disconnect - no automatic reconnection');
            this.intentionalDisconnect = false; // Reset flag
        }
    }

    async ensureConnected() {
        // Check if device exists and is connected
        if (!this.device || !this.device.gatt || !this.device.gatt.connected) {
            console.log('Device is not connected, updating state...');
            this.connected = false;
            this.updateConnectionStatus('Disconnected');
            this.updateUI();
            return false;
        }

        // Check if we have a valid server connection
        if (!this.server || !this.service || !this.commandCharacteristic) {
            console.log('Invalid connection state, attempting to reconnect...');
            try {
                await this.reconnect();
                return this.connected;
            } catch (error) {
                console.error('Reconnection failed:', error);
                return false;
            }
        }

        return this.connected;
    }

    async reconnect() {
        if (!this.device) {
            throw new Error('No device available for reconnection');
        }

        console.log('Attempting to reconnect...');
        this.updateConnectionStatus('Reconnecting...');

        try {
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
            this.device.addEventListener('gattserverdisconnected', this.onDisconnectedHandler);
            
            this.connected = true;
            this.updateConnectionStatus('Connected');
            this.updateUI();
            
            console.log('Successfully reconnected to BratenDreher');
            
        } catch (error) {
            console.error('Reconnection failed:', error);
            this.onDisconnected();
            throw error;
        }
    }

    async handleReconnect(automatic = false) {
        if (!this.device) {
            // If no device, try a fresh connection
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
            this.intentionalDisconnect = false; // Reset flag when reconnecting
            this.updateConnectionStatus('Reconnecting...', 'Attempting to reconnect to existing device...');
            await this.reconnect();
        } catch (error) {
            console.error('Reconnection failed:', error);
            // Only show error for manual reconnections
            if (!automatic) {
                this.showError(`Reconnection failed: ${error.message}. Try connecting again.`);
                // Clear the device if reconnection fails completely
                this.device = null;
                this.updateConnectionStatus('Disconnected', 'Reconnection failed - Click Connect to try again');
            } else {
                console.log('Automatic reconnection failed, will try again later');
            }
        }
    }

    clearSliderTimers() {
        if (this.speedSliderTimer) {
            clearTimeout(this.speedSliderTimer);
            this.speedSliderTimer = null;
        }
        if (this.currentSliderTimer) {
            clearTimeout(this.currentSliderTimer);
            this.currentSliderTimer = null;
        }
        if (this.accelerationSliderTimer) {
            clearTimeout(this.accelerationSliderTimer);
            this.accelerationSliderTimer = null;
        }
    }

    handleBLEError(error, userMessage) {
        console.error('BLE Error:', error);
        
        // Check if it's a connection-related error
        if (error.message && error.message.includes('GATT Server is disconnected')) {
            console.log('GATT disconnection detected, updating connection state');
            this.onDisconnected();
            this.showError(`${userMessage}. Device disconnected. Please reconnect.`);
        } else {
            this.showError(userMessage);
        }
    }
    
    // Send command using JSON protocol
    async sendCommand(type, value, additionalParams = {}) {
        // Check if we're actually connected
        if (!this.device || !this.device.gatt || !this.device.gatt.connected) {
            console.error(`Cannot send ${type} command: Device not connected`);
            this.onDisconnected(); // Update state
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
            return true;
        } catch (error) {
            console.error(`Failed to send ${type} command:`, error);
            this.handleBLEError(error, `Failed to send ${type} command`);
            return false;
        }
    }
    
    // Request status update
    async requestStatus() {
        return await this.sendCommand('status_request', true);
    }
    
    async setSpeed(speed) {
        speed = Math.max(0.1, Math.min(30.0, speed)); // Clamp to valid range
        this.motorSpeed = speed;
        
        return await this.sendCommand('speed', speed);
    }
    
    async setCurrent(current) {
        this.current = current;
        return await this.sendCommand('current', current);
    }
    
    async setAccelerationTime(timeSeconds) {
        // Send acceleration time command with target RPM of 30
        return await this.sendCommand('acceleration_time', timeSeconds, { target_rpm: 30.0 });
    }
    
    async resetStatistics() {
        const success = await this.sendCommand('reset', true);
        if (success) {
            // Visual feedback
            this.resetStatsBtn.textContent = '📊 Reset Successful';
            setTimeout(() => {
                this.resetStatsBtn.textContent = '📊 Reset Statistics';
            }, 2000);
        }
        return success;
    }
    
    async resetStallCount() {
        const success = await this.sendCommand('reset_stall', true);
        if (success) {
            // Visual feedback
            this.resetStallBtn.textContent = '⚠️ Reset Successful';
            setTimeout(() => {
                this.resetStallBtn.textContent = '⚠️ Reset Stall Count';
            }, 2000);
        }
        return success;
    }
    
    async setDirection(clockwise) {
        this.motorDirection = clockwise;
        
        // Update UI
        this.clockwiseBtn.classList.toggle('active', clockwise);
        this.counterclockwiseBtn.classList.toggle('active', !clockwise);
        
        return await this.sendCommand('direction', clockwise);
    }
    
    async setMotorEnabled(enabled) {
        this.motorEnabled = enabled;
        this.motorToggle.checked = enabled;
        
        return await this.sendCommand('enable', enabled);
    }
    
    async emergencyStop() {
        console.log('Emergency stop triggered');
        await this.setMotorEnabled(false);
        
        // Visual feedback
        this.emergencyStopBtn.style.background = '#dc2626';
        this.emergencyStopBtn.textContent = '🛑 STOPPED';
        
        setTimeout(() => {
            this.emergencyStopBtn.style.background = '#ef4444';
            this.emergencyStopBtn.textContent = '🛑 Emergency Stop';
        }, 2000);
    }
    
    // Handle incoming messages (status updates and responses)
    handleMessage(event) {
        try {
            const value = new TextDecoder().decode(event.target.value);
            const message = JSON.parse(value);
            
            console.log('Message received:', message);
            
            if (message.type === 'status') {
                this.handleStatusUpdate(message);
            } else if (message.type === 'command_result') {
                this.handleCommandResult(message);
            } else {
                console.log('Unknown message type:', message.type);
            }
        } catch (error) {
            console.error('Failed to parse message:', error);
        }
    }
    
    handleStatusUpdate(status) {
        console.log('Status update:', status);
        
        // Update internal state from device status
        this.motorEnabled = status.enabled;
        this.motorSpeed = status.speed;
        this.motorDirection = status.direction === 'cw';
        
        // Update UI controls to match device state
        this.motorToggle.checked = status.enabled;
        this.speedSlider.value = status.speed;
        this.speedValue.textContent = status.speed.toFixed(1);
        
        // Update direction buttons
        this.clockwiseBtn.classList.toggle('active', status.direction === 'cw');
        this.counterclockwiseBtn.classList.toggle('active', status.direction !== 'cw');
        
        // Update status display
        this.motorStatus.textContent = status.enabled ? 
            (status.running ? 'Running' : 'Enabled') : 'Stopped';
        this.currentSpeed.textContent = `${status.speed.toFixed(1)} RPM`;
        this.currentDirection.textContent = status.direction === 'cw' ? 'Clockwise' : 'Counter-clockwise';
        this.currentCurrent.textContent = `${status.current || this.current}%`;
        
        // Update TMC2209 status with fallback for backward compatibility
        if (status.tmc2209Status !== undefined) {
            this.tmc2209Status.textContent = status.tmc2209Status ? 'OK' : 'Error';
            this.tmc2209Status.style.color = status.tmc2209Status ? '#2ecc71' : '#e74c3c';
        } else {
            this.tmc2209Status.textContent = 'Unknown';
            this.tmc2209Status.style.color = '#f39c12';
        }
        
        // Update stall status with fallback for backward compatibility
        if (status.stallDetected !== undefined) {
            this.stallStatus.textContent = status.stallDetected ? 'STALL!' : 'OK';
            this.stallStatus.style.color = status.stallDetected ? '#e74c3c' : '#2ecc71';
            this.stallStatus.style.fontWeight = status.stallDetected ? 'bold' : 'normal';
        } else {
            this.stallStatus.textContent = 'Unknown';
            this.stallStatus.style.color = '#f39c12';
        }
        
        // Update stall count
        if (status.stallCount !== undefined) {
            this.stallCount.textContent = status.stallCount;
            this.stallCount.style.color = status.stallCount > 0 ? '#e67e22' : '#2ecc71';
        } else {
            this.stallCount.textContent = 'N/A';
            this.stallCount.style.color = '#95a5a6';
        }
        
        this.lastUpdate.textContent = new Date().toLocaleTimeString();
        
        // Update statistics
        if (status.totalRevolutions !== undefined) {
            this.totalRevolutions.textContent = status.totalRevolutions.toFixed(3);
        }
        
        if (status.runtime !== undefined) {
            this.runTime.textContent = this.formatTime(status.runtime);
        }
        
        // Calculate average speed
        if (status.runtime > 0 && status.totalRevolutions > 0) {
            const avgSpeed = (status.totalRevolutions * 60) / status.runtime; // RPM
            this.avgSpeed.textContent = avgSpeed.toFixed(1);
        } else {
            this.avgSpeed.textContent = '0.0';
        }
        
        // Update current if different
        if (status.current && status.current !== this.current) {
            this.current = status.current;
            this.currentSlider.value = status.current;
            this.currentValue.textContent = status.current;
        }
        
        // Update preset button active state based on current speed
        this.presetBtns.forEach(btn => {
            const presetSpeed = parseFloat(btn.dataset.speed);
            btn.classList.toggle('active', Math.abs(presetSpeed - status.speed) < 0.05);
        });
    }
    
    handleCommandResult(result) {
        console.log('Command result:', result);
        
        const commandId = result.command_id;
        const status = result.status;
        const message = result.message || '';
        
        if (status === 'success') {
            // Command was successful - could show brief success indicator
            console.log(`Command ${commandId} executed successfully`);
        } else {
            // Command failed - show error to user
            let userMessage = '';
            
            switch (status) {
                case 'hardware_error':
                    if (message.includes('Stepper not initialized')) {
                        userMessage = 'Hardware Error: Stepper motor system not properly initialized';
                    } else if (message.includes('Failed to start stepper movement')) {
                        userMessage = 'Movement Error: Unable to start motor movement. Check motor connections and power.';
                    } else if (message.includes('Failed to set speed')) {
                        userMessage = 'Speed Error: Unable to set motor speed. Check stepper driver configuration.';
                    } else {
                        userMessage = 'Hardware Error: ' + (message || 'Stepper motor hardware issue');
                    }
                    break;
                case 'driver_not_responding':
                    userMessage = 'Driver Error: TMC2209 stepper driver is not responding. Check connections and power.';
                    break;
                case 'invalid_parameter':
                    if (message.includes('Speed out of range')) {
                        userMessage = 'Speed Error: ' + message;
                    } else if (message.includes('Current out of range')) {
                        userMessage = 'Current Error: ' + message;
                    } else {
                        userMessage = 'Invalid Setting: ' + (message || 'The parameter value is out of range');
                    }
                    break;
                case 'communication_error':
                    if (message.includes('verification failed')) {
                        userMessage = 'Configuration Error: ' + message + '. TMC2209 may not be responding properly.';
                    } else if (message.includes('not responding after')) {
                        userMessage = 'Communication Error: TMC2209 driver stopped responding during configuration.';
                    } else {
                        userMessage = 'Communication Error: Failed to communicate with the stepper driver. Check wiring.';
                    }
                    break;
                default:
                    userMessage = 'Command Failed: ' + (message || 'Unknown error occurred');
                    break;
            }
            
            this.showError(userMessage);
            
            // Log technical details for debugging
            console.error(`Command ${commandId} failed: ${status} - ${message}`);
        }
    }
    
    formatTime(seconds) {
        const hours = Math.floor(seconds / 3600);
        const minutes = Math.floor((seconds % 3600) / 60);
        const secs = seconds % 60;
        return `${hours.toString().padStart(2, '0')}:${minutes.toString().padStart(2, '0')}:${secs.toString().padStart(2, '0')}`;
    }
    
    updateConnectionStatus(status, info = '') {
        this.connectionStatus.textContent = status;
        if (this.connectionInfo) {
            this.connectionInfo.textContent = info || this.getStatusInfo(status);
        }
        
        if (status === 'Connected') {
            this.statusIndicator.classList.add('connected');
            this.connectBtn.disabled = true;
            this.reconnectBtn.disabled = false;
            this.disconnectBtn.disabled = false;
        } else if (status === 'Connecting...' || status === 'Reconnecting...') {
            this.statusIndicator.classList.remove('connected');
            this.connectBtn.disabled = true;
            this.reconnectBtn.disabled = true;
            this.disconnectBtn.disabled = true;
        } else {
            // Disconnected
            this.statusIndicator.classList.remove('connected');
            this.connectBtn.disabled = false;
            // Enable reconnect button - it will handle both reconnection and fresh connection
            this.reconnectBtn.disabled = false;
            this.disconnectBtn.disabled = true;
        }
    }

    getStatusInfo(status) {
        switch (status) {
            case 'Connected':
                return this.device ? `Connected to ${this.device.name}` : 'Connected to BratenDreher';
            case 'Connecting...':
                return 'Searching for BratenDreher device...';
            case 'Reconnecting...':
                return 'Attempting to reconnect...';
            case 'Disconnected':
                return this.device ? 'Disconnected - Use Reconnect/Retry button' : 'Click Connect to start';
            default:
                return '';
        }
    }
    
    updateUI() {
        // Enable/disable controls based on connection state
        const controls = [
            this.motorToggle,
            this.speedSlider,
            this.clockwiseBtn,
            this.counterclockwiseBtn,
            this.emergencyStopBtn,
            this.currentSlider,
            this.accelerationTimeSlider,
            this.resetStatsBtn,
            this.resetStallBtn
        ];
        
        controls.forEach(control => {
            if (control) {
                control.disabled = !this.connected;
            }
        });
        
        this.presetBtns.forEach(btn => {
            btn.disabled = !this.connected;
        });
    }
    
    showError(message) {
        console.error(message);
        
        // Create a toast notification instead of alert
        const toast = document.createElement('div');
        toast.className = 'error-toast';
        toast.textContent = message;
        
        // Add toast styles inline
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
        
        // Add animation keyframes
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
}

// Initialize the application when the page loads
document.addEventListener('DOMContentLoaded', () => {
    console.log('Initializing BratenDreher Web Interface');
    new BratenDreherBLE();
});
