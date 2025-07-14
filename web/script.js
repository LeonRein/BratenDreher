class Control {
    constructor(element, valueElement, options = {}) {
        this.element = element;
        this.valueElement = valueElement;
        this.options = {
            debounceTime: 300,
            commandType: null,
            valueTransform: (value) => value, // Function to transform input value before sending
            displayTransform: (value) => value, // Function to transform value for display
            statusKey: null, // Key to look for in status updates
            ...options
        };
        this.timer = null;
        this.parent = null; // Will be set by BratenDreherBLE
    }

    setParent(parent) {
        this.parent = parent;
    }

    // Set up event listeners
    bindEvents() {
        // Skip binding if no element (for display-only controls)
        if (!this.element) {
            return;
        }
        
        if (this.element.type === 'range') {
            this.element.addEventListener('input', (e) => this.handleInput(e));
        } else if (this.element.type === 'checkbox') {
            this.element.addEventListener('change', (e) => this.handleChange(e));
        } else {
            this.element.addEventListener('click', (e) => this.handleClick(e));
        }
    }

    handleInput(event) {
        const rawValue = this.element.type === 'range' ? 
            parseFloat(event.target.value) : 
            event.target.value;
        
        const displayValue = this.options.displayTransform(rawValue);
        
        // Update display immediately
        if (this.valueElement) {
            this.valueElement.textContent = displayValue;
        }
        
        // Visual feedback that change is pending
        this.setPendingState(true);
        
        // Debounce the command sending
        if (this.timer) {
            clearTimeout(this.timer);
        }
        
        this.timer = setTimeout(() => {
            this.sendCommand(rawValue).then(() => {
                this.setPendingState(false);
            });
        }, this.options.debounceTime);
    }

    handleChange(event) {
        const value = event.target.checked;
        this.sendCommand(value);
    }

    handleClick(event) {
        if (this.options.clickValue !== undefined) {
            this.sendCommand(this.options.clickValue);
        }
    }

    async sendCommand(rawValue) {
        if (!this.parent || !this.options.commandType) {
            console.error('Control not properly configured for command sending');
            return false;
        }

        const transformedValue = this.options.valueTransform(rawValue);
        return await this.parent.sendCommand(this.options.commandType, transformedValue, this.options.additionalParams || {});
    }

    // Handle status updates - can be overridden by subclasses for complex logic
    handleStatusUpdate(statusUpdate) {
        // First, handle standard status key mapping (for simple controls)
        if (this.options.statusKey && statusUpdate[this.options.statusKey] !== undefined) {
            let value = statusUpdate[this.options.statusKey];
            
            // Apply status transform if available (for converting backend values to UI values)
            if (this.options.statusTransform) {
                value = this.options.statusTransform(value);
            }
            
            // Update UI element based on type
            if (this.element && this.element.type === 'range') {
                this.element.value = value;
                if (this.valueElement) {
                    this.valueElement.textContent = this.options.displayTransform(value);
                }
            } else if (this.element && this.element.type === 'checkbox') {
                this.element.checked = value;
            }
        }
        
        // Subclasses can override this method to add additional specialized handling
    }

    // Set pending visual state
    setPendingState(pending) {
        if (this.valueElement) {
            this.valueElement.style.opacity = pending ? '0.7' : '1';
        }
    }

    // Enable/disable control based on connection state
    setEnabled(enabled) {
        if (this.element) {
            this.element.disabled = !enabled;
        }
    }

    // Clear any pending timers
    clearTimer() {
        if (this.timer) {
            clearTimeout(this.timer);
            this.timer = null;
        }
    }
}

// Specialized control for speed with preset button management
class SpeedControl extends Control {
    constructor(element, valueElement, presetButtons, options = {}) {
        super(element, valueElement, options);
        this.presetButtons = presetButtons;
    }

    handleStatusUpdate(statusUpdate) {
        // Call parent to handle standard status key mapping
        super.handleStatusUpdate(statusUpdate);
        
        // Add specialized handling
        if (statusUpdate.speed !== undefined) {
            // Update current speed display
            const currentSpeedElement = this.parent.currentSpeed;
            if (currentSpeedElement) {
                currentSpeedElement.textContent = `${statusUpdate.speed.toFixed(1)} RPM`;
            }
            
            // Update preset button active state
            this.presetButtons.forEach(btn => {
                const presetSpeed = parseFloat(btn.dataset.speed);
                btn.classList.toggle('active', Math.abs(presetSpeed - statusUpdate.speed) < 0.05);
            });
        }
    }
}

// Specialized control for direction with button state management
class DirectionControl extends Control {
    constructor(clockwiseBtn, counterclockwiseBtn, currentDirectionElement, options = {}) {
        super(null, null, options); // No main element for this control
        this.clockwiseBtn = clockwiseBtn;
        this.counterclockwiseBtn = counterclockwiseBtn;
        this.currentDirectionElement = currentDirectionElement;
    }

    bindEvents() {
        this.clockwiseBtn.addEventListener('click', () => this.setDirection(true));
        this.counterclockwiseBtn.addEventListener('click', () => this.setDirection(false));
    }

    async setDirection(clockwise) {
        // Update UI immediately for responsive feedback
        this.clockwiseBtn.classList.toggle('active', clockwise);
        this.counterclockwiseBtn.classList.toggle('active', !clockwise);
        
        return await this.parent.sendCommand('direction', clockwise);
    }

    handleStatusUpdate(statusUpdate) {
        if (statusUpdate.direction !== undefined) {
            const clockwise = statusUpdate.direction === 'cw';
            this.clockwiseBtn.classList.toggle('active', clockwise);
            this.counterclockwiseBtn.classList.toggle('active', !clockwise);
            this.currentDirectionElement.textContent = clockwise ? 'Clockwise' : 'Counter-clockwise';
        }
    }

    setEnabled(enabled) {
        this.clockwiseBtn.disabled = !enabled;
        this.counterclockwiseBtn.disabled = !enabled;
    }
}

// Specialized control for motor status with multiple status indicators
class MotorStatusControl extends Control {
    constructor(element, motorStatusElement, options = {}) {
        super(element, null, options);
        this.motorStatusElement = motorStatusElement;
    }

    handleStatusUpdate(statusUpdate) {
        // Call parent to handle standard status key mapping
        super.handleStatusUpdate(statusUpdate);
        
        // Add specialized handling
        if (statusUpdate.enabled !== undefined) {
            const enabled = statusUpdate.enabled;
            // Update motor status (will be refined if running status is also provided)
            this.motorStatusElement.textContent = enabled ? 'Enabled' : 'Stopped';
        }
        
        if (statusUpdate.running !== undefined) {
            // Refine motor status if we have running information
            const enabled = this.element.checked; // Get from UI state
            if (enabled) {
                this.motorStatusElement.textContent = statusUpdate.running ? 'Running' : 'Enabled';
            }
        }
    }
}

// Specialized control for acceleration with time conversion
class AccelerationControl extends Control {
    constructor(element, valueElement, currentAccelerationElement, options = {}) {
        super(element, valueElement, options);
        this.currentAccelerationElement = currentAccelerationElement;
    }

    handleStatusUpdate(statusUpdate) {
        if (statusUpdate.acceleration !== undefined) {
            const accelerationTimeDisplay = this.parent.accelerationToTime(statusUpdate.acceleration).toFixed(1);
            this.currentAccelerationElement.textContent = `${accelerationTimeDisplay}s to max`;
            
            // Update acceleration slider if significantly different
            const currentSliderTime = parseInt(this.element.value);
            const deviceTime = Math.round(this.parent.accelerationToTime(statusUpdate.acceleration));
            if (Math.abs(currentSliderTime - deviceTime) > 1) {
                this.element.value = deviceTime;
                this.valueElement.textContent = deviceTime;
            }
        }
    }
}

// Specialized control for variable speed with UI management
class VariableSpeedControl extends Control {
    constructor(element, variableSpeedControls, variableSpeedStatus, currentVariableSpeedItem, currentVariableSpeed, options = {}) {
        super(element, null, options);
        this.variableSpeedControls = variableSpeedControls;
        this.variableSpeedStatus = variableSpeedStatus;
        this.currentVariableSpeedItem = currentVariableSpeedItem;
        this.currentVariableSpeed = currentVariableSpeed;
    }

    bindEvents() {
        this.element.addEventListener('change', (e) => {
            this.setVariableSpeedEnabled(e.target.checked);
        });
    }

    async setVariableSpeedEnabled(enabled) {
        // Update UI immediately for responsive feedback
        this.element.checked = enabled;
        
        // Update UI controls
        this.updateVariableSpeedUI();
        
        if (enabled) {
            return await this.parent.sendCommand('enable_speed_variation', true);
        } else {
            return await this.parent.sendCommand('disable_speed_variation', true);
        }
    }

    updateVariableSpeedUI() {
        const enabled = this.element.checked;
        if (enabled) {
            this.variableSpeedControls.classList.remove('disabled');
        } else {
            this.variableSpeedControls.classList.add('disabled');
        }
    }

    handleStatusUpdate(statusUpdate) {
        // No parent call needed - this control doesn't use standard status key mapping
        
        if (statusUpdate.speedVariationEnabled !== undefined) {
            const enabled = statusUpdate.speedVariationEnabled;
            this.updateVariableSpeedUI();
            
            this.variableSpeedStatus.textContent = enabled ? 'ON' : 'OFF';
            this.variableSpeedStatus.style.color = enabled ? '#2ecc71' : '#6b7280';
            
            // Hide variable speed display if disabled
            if (!enabled) {
                this.currentVariableSpeedItem.style.display = 'none';
            }
        }

        if (statusUpdate.currentVariableSpeed !== undefined) {
            const variableSpeedEnabled = this.element.checked;
            if (variableSpeedEnabled) {
                this.currentVariableSpeedItem.style.display = 'flex';
                this.currentVariableSpeed.textContent = `${statusUpdate.currentVariableSpeed.toFixed(1)} RPM`;
            }
        }
    }
}

// Specialized control for TMC2209 status
class TMCStatusControl extends Control {
    constructor(statusElement, stallStatusElement, stallCountElement, options = {}) {
        super(null, null, options); // No main element
        this.statusElement = statusElement;
        this.stallStatusElement = stallStatusElement;
        this.stallCountElement = stallCountElement;
    }

    handleStatusUpdate(statusUpdate) {
        if (statusUpdate.tmc2209Status !== undefined) {
            this.statusElement.textContent = statusUpdate.tmc2209Status ? 'OK' : 'Error';
            this.statusElement.style.color = statusUpdate.tmc2209Status ? '#2ecc71' : '#e74c3c';
        }
        
        if (statusUpdate.stallDetected !== undefined) {
            this.stallStatusElement.textContent = statusUpdate.stallDetected ? 'STALL!' : 'OK';
            this.stallStatusElement.style.color = statusUpdate.stallDetected ? '#e74c3c' : '#2ecc71';
            this.stallStatusElement.style.fontWeight = statusUpdate.stallDetected ? 'bold' : 'normal';
        }
        
        if (statusUpdate.stallCount !== undefined) {
            this.stallCountElement.textContent = statusUpdate.stallCount;
            this.stallCountElement.style.color = statusUpdate.stallCount > 0 ? '#e67e22' : '#2ecc71';
        }
    }
}

// Specialized control for statistics with calculation logic
class StatisticsControl extends Control {
    constructor(totalRevolutionsElement, runTimeElement, avgSpeedElement, options = {}) {
        super(null, null, options); // No main element
        this.totalRevolutionsElement = totalRevolutionsElement;
        this.runTimeElement = runTimeElement;
        this.avgSpeedElement = avgSpeedElement;
    }

    handleStatusUpdate(statusUpdate) {
        if (statusUpdate.totalRevolutions !== undefined) {
            this.totalRevolutionsElement.textContent = statusUpdate.totalRevolutions.toFixed(3);
        }
        
        if (statusUpdate.runtime !== undefined) {
            this.runTimeElement.textContent = this.formatTime(statusUpdate.runtime);
        }
        
        // Calculate average speed if we have both runtime and revolutions
        if (statusUpdate.runtime !== undefined || statusUpdate.totalRevolutions !== undefined) {
            // Get current values from UI (our state)
            const currentRevolutions = parseFloat(this.totalRevolutionsElement.textContent) || 0;
            const currentRuntimeText = this.runTimeElement.textContent;
            let currentRuntime = 0;
            
            // Parse runtime from HH:MM:SS format
            if (currentRuntimeText && currentRuntimeText !== '00:00:00') {
                const timeParts = currentRuntimeText.split(':');
                currentRuntime = parseInt(timeParts[0]) * 3600 + parseInt(timeParts[1]) * 60 + parseInt(timeParts[2]);
            }
            
            if (currentRuntime > 0 && currentRevolutions > 0) {
                const avgSpeed = (currentRevolutions * 60) / currentRuntime; // RPM
                this.avgSpeedElement.textContent = avgSpeed.toFixed(1);
            } else {
                this.avgSpeedElement.textContent = '0.0';
            }
        }
    }

    formatTime(seconds) {
        const hours = Math.floor(seconds / 3600);
        const minutes = Math.floor((seconds % 3600) / 60);
        const secs = seconds % 60;
        return `${hours.toString().padStart(2, '0')}:${minutes.toString().padStart(2, '0')}:${secs.toString().padStart(2, '0')}`;
    }
}

class BratenDreherBLE {
    constructor() {
        // Motor specifications for acceleration conversion
        this.MAX_SPEED_RPM = 30.0; // Maximum available speed - easily changeable
        this.GEAR_RATIO = 10; // Motor gear ratio - must match StepperController.h
        this.STEPS_PER_REVOLUTION = 200; // Motor steps per revolution
        this.MICROSTEPS = 16; // Microsteps setting
        
        // BLE Service and Characteristic UUIDs (must match ESP32)
        this.serviceUUID = '12345678-1234-1234-1234-123456789abc';
        this.commandCharacteristicUUID = '12345678-1234-1234-1234-123456789ab1';
        
        // BLE Connection objects
        this.device = null;
        this.server = null;
        this.service = null;
        this.commandCharacteristic = null;
        
        // Connection state (only track what we need for connection management)
        this.connected = false;
        this.intentionalDisconnect = false; // Track if user intentionally disconnected
        
        // Controls - using Control class for common functionality
        this.controls = new Map();
        
        // Bind the disconnect handler so we can add/remove it
        this.onDisconnectedHandler = () => {
            console.log('GATT server disconnected event received');
            this.onDisconnected();
        };
        
        // UI elements
        this.initializeUIElements();
        this.initializeControls();
        this.bindEventListeners();
        
        // Initialize variable speed UI state
        this.updateVariableSpeedUI();
        
        // Check Web Bluetooth support
        if (!navigator.bluetooth) {
            this.showError('Web Bluetooth is not supported in this browser. Please use Chrome, Edge, or Opera.');
            this.connectBtn.disabled = true;
            this.reconnectBtn.disabled = true;
            return;
        }
        
        console.log('BratenDreher BLE Controller initialized');
    }
    
    // Acceleration conversion methods
    rpmToStepsPerSecond(rpm) {
        // Calculate steps per second for the motor (before gear reduction)
        // Must match exactly with backend calculation in StepperController::rpmToStepsPerSecond()
        const motorRPM = rpm * this.GEAR_RATIO;
        const motorStepsPerSecond = (motorRPM * this.STEPS_PER_REVOLUTION * this.MICROSTEPS) / 60.0;
        return Math.floor(motorStepsPerSecond); // Use Math.floor to match backend's static_cast<uint32_t>
    }
    
    accelerationToTime(accelerationStepsPerSec2) {
        // Convert acceleration (steps/s²) to time to reach max speed
        if (accelerationStepsPerSec2 === 0) {
            return 5.0; // Default fallback
        }
        
        const maxStepsPerSecond = this.rpmToStepsPerSecond(this.MAX_SPEED_RPM);
        const timeSeconds = maxStepsPerSecond / accelerationStepsPerSec2;
        
        // Clamp to reasonable range (1-30 seconds)
        return Math.max(1.0, Math.min(30.0, timeSeconds));
    }
    
    timeToAcceleration(timeSeconds) {
        // Convert time (to reach max speed) to acceleration (steps/s²)
        // Must match exactly with backend calculation in StepperController::calculateAccelerationForTime()
        const maxStepsPerSecond = this.rpmToStepsPerSecond(this.MAX_SPEED_RPM);
        const acceleration = maxStepsPerSecond / timeSeconds;
        return Math.floor(acceleration); // Use Math.floor to match backend's static_cast<uint32_t>
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
        
        // Variable speed elements
        this.variableSpeedToggle = document.getElementById('variableSpeedToggle');
        this.variableSpeedControls = document.getElementById('variableSpeedControls');
        this.strengthSlider = document.getElementById('strengthSlider');
        this.strengthValue = document.getElementById('strengthValue');
        this.phaseSlider = document.getElementById('phaseSlider');
        this.phaseValue = document.getElementById('phaseValue');
        
        // Status elements
        this.motorStatus = document.getElementById('motorStatus');
        this.currentSpeed = document.getElementById('currentSpeed');
        this.currentAcceleration = document.getElementById('currentAcceleration');
        this.currentDirection = document.getElementById('currentDirection');
        this.currentCurrent = document.getElementById('currentCurrent');
        this.tmc2209Status = document.getElementById('tmc2209Status');
        this.stallStatus = document.getElementById('stallStatus');
        this.stallCount = document.getElementById('stallCount');
        this.lastUpdate = document.getElementById('lastUpdate');
        
        // Variable speed status elements
        this.variableSpeedStatus = document.getElementById('variableSpeedStatus');
        this.currentVariableSpeedItem = document.getElementById('currentVariableSpeedItem');
        this.currentVariableSpeed = document.getElementById('currentVariableSpeed');
        
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
        
        // Direction buttons - delegate to direction control
        // (The actual event binding is handled in the DirectionControl class)
        
        this.emergencyStopBtn.addEventListener('click', () => {
            this.emergencyStop();
        });
        
        // Preset buttons
        this.presetBtns.forEach(btn => {
            btn.addEventListener('click', () => {
                const speed = parseFloat(btn.dataset.speed);
                
                // Update slider value and trigger the control system
                this.speedSlider.value = speed;
                this.speedSlider.dispatchEvent(new Event('input'));
                
                // Update active preset
                this.presetBtns.forEach(b => b.classList.remove('active'));
                btn.classList.add('active');
            });
        });
        
        // Reset buttons
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
            
            // Initialize variable speed UI
            this.updateVariableSpeedUI();
            
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

        // Clear any pending control timers
        this.clearControlTimers();

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
            
            // Re-add disconnect event listener (remove first to prevent duplicates)
            this.device.removeEventListener('gattserverdisconnected', this.onDisconnectedHandler);
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

    clearControlTimers() {
        this.controls.forEach(control => {
            control.clearTimer();
        });
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
    
    async setVariableSpeedEnabled(enabled) {
        // Update UI immediately for responsive feedback
        this.variableSpeedToggle.checked = enabled;
        
        // Update UI controls
        this.updateVariableSpeedUI();
        
        if (enabled) {
            return await this.sendCommand('enable_speed_variation', true);
        } else {
            return await this.sendCommand('disable_speed_variation', true);
        }
    }
    
    updateVariableSpeedUI() {
        const enabled = this.variableSpeedToggle.checked;
        if (enabled) {
            this.variableSpeedControls.classList.remove('disabled');
        } else {
            this.variableSpeedControls.classList.add('disabled');
        }
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
        // Delegate to direction control
        const directionControl = this.controls.get('direction');
        if (directionControl) {
            return await directionControl.setDirection(clockwise);
        }
        return false;
    }
    
    async setMotorEnabled(enabled) {
        // Update UI immediately for responsive feedback
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
            
            if (message.type === 'status_update') {
                // Granular status update (new system)
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
    
    handleStatusUpdate(statusUpdate) {
        console.log('Status update:', statusUpdate);
        
        // Update timestamp
        this.lastUpdate.textContent = new Date().toLocaleTimeString();
        
        // Restore visual feedback opacity since we're getting updates from device
        this.restoreOpacity();
        
        // Update all controls using the unified handleStatusUpdate method
        this.controls.forEach(control => {
            control.handleStatusUpdate(statusUpdate);
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
        // Enable/disable all controls based on connection state
        this.controls.forEach(control => {
            control.setEnabled(this.connected);
        });
        
        // Handle other UI elements that aren't managed by Control system
        const otherControls = [
            this.emergencyStopBtn,
            this.resetStatsBtn,
            this.resetStallBtn
        ];
        
        otherControls.forEach(control => {
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
    
    // Helper method to restore visual feedback opacity after device updates
    restoreOpacity() {
        // Restore opacity for all controls that might have pending feedback
        this.controls.forEach(control => {
            control.setPendingState(false);
        });
    }

    initializeControls() {
        // Speed control with preset button management
        this.controls.set('speed', new SpeedControl(
            this.speedSlider,
            this.speedValue,
            this.presetBtns,
            {
                commandType: 'speed',
                statusKey: 'speed',
                displayTransform: (value) => value.toFixed(1),
                valueTransform: (value) => Math.max(0.1, Math.min(30.0, value))
            }
        ));

        // Current control
        this.controls.set('current', new Control(
            this.currentSlider,
            this.currentValue,
            {
                commandType: 'current',
                statusKey: 'current',
                displayTransform: (value) => value.toString(),
                valueTransform: (value) => parseInt(value)
            }
        ));

        // Acceleration time control with time conversion
        this.controls.set('acceleration', new AccelerationControl(
            this.accelerationTimeSlider,
            this.accelerationTimeValue,
            this.currentAcceleration,
            {
                commandType: 'acceleration',
                statusKey: null, // Handled specially in the control
                displayTransform: (value) => value.toString(),
                valueTransform: (value) => this.timeToAcceleration(parseInt(value))
            }
        ));

        // Variable speed strength control
        this.controls.set('strength', new Control(
            this.strengthSlider,
            this.strengthValue,
            {
                commandType: 'speed_variation_strength',
                statusKey: 'speedVariationStrength',
                displayTransform: (value) => value.toString(),
                valueTransform: (value) => parseInt(value) / 100.0,
                statusTransform: (value) => Math.round(value * 100) // Convert from 0-1 to 0-100 for UI
            }
        ));

        // Variable speed phase control
        this.controls.set('phase', new Control(
            this.phaseSlider,
            this.phaseValue,
            {
                commandType: 'speed_variation_phase',
                statusKey: 'speedVariationPhase',
                displayTransform: (value) => value.toString(),
                valueTransform: (value) => {
                    const phase = parseInt(value);
                    // Convert degrees to radians
                    // Convert from -180 to 180 range to 0 to 2π range
                    let phaseForRadians = phase;
                    if (phaseForRadians < 0) {
                        phaseForRadians += 360;
                    }
                    return (phaseForRadians * Math.PI) / 180;
                },
                statusTransform: (value) => {
                    // Convert from radians to degrees and then to -180 to 180 range
                    let phaseDegrees = Math.round((value * 180) / Math.PI);
                    if (phaseDegrees > 180) {
                        phaseDegrees -= 360;
                    }
                    return phaseDegrees;
                }
            }
        ));

        // Motor toggle control with status display
        this.controls.set('motor', new MotorStatusControl(
            this.motorToggle,
            this.motorStatus,
            {
                commandType: 'enable',
                statusKey: 'enabled',
                debounceTime: 0 // No debouncing for toggles
            }
        ));

        // Direction control with button management
        this.controls.set('direction', new DirectionControl(
            this.clockwiseBtn,
            this.counterclockwiseBtn,
            this.currentDirection
        ));

        // Variable speed control with full UI management
        this.controls.set('variableSpeed', new VariableSpeedControl(
            this.variableSpeedToggle,
            this.variableSpeedControls,
            this.variableSpeedStatus,
            this.currentVariableSpeedItem,
            this.currentVariableSpeed,
            {
                statusKey: 'speedVariationEnabled',
                debounceTime: 0
            }
        ));

        // TMC2209 status control
        this.controls.set('tmcStatus', new TMCStatusControl(
            this.tmc2209Status,
            this.stallStatus,
            this.stallCount
        ));

        // Statistics control with calculation logic
        this.controls.set('statistics', new StatisticsControl(
            this.totalRevolutions,
            this.runTime,
            this.avgSpeed
        ));

        // Current display control (simple display-only)
        this.controls.set('currentDisplay', new Control(
            null,
            this.currentCurrent,
            {
                statusKey: 'current',
                displayTransform: (value) => `${value}%`
            }
        ));

        // Set parent reference and bind events for all controls
        this.controls.forEach(control => {
            control.setParent(this);
            control.bindEvents();
        });
    }
}

// Initialize the application when the page loads
document.addEventListener('DOMContentLoaded', () => {
    console.log('Initializing BratenDreher Web Interface');
    new BratenDreherBLE();
});
