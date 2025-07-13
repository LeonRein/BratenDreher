class BratenDreherBLE {
    constructor() {
        // BLE Service and Characteristic UUIDs (must match ESP32)
        this.serviceUUID = '12345678-1234-1234-1234-123456789abc';
        this.speedCharacteristicUUID = '12345678-1234-1234-1234-123456789ab1';
        this.directionCharacteristicUUID = '12345678-1234-1234-1234-123456789ab2';
        this.enableCharacteristicUUID = '12345678-1234-1234-1234-123456789ab3';
        this.statusCharacteristicUUID = '12345678-1234-1234-1234-123456789ab4';
        this.microstepsCharacteristicUUID = '12345678-1234-1234-1234-123456789ab5';
        this.currentCharacteristicUUID = '12345678-1234-1234-1234-123456789ab6';
        this.resetCharacteristicUUID = '12345678-1234-1234-1234-123456789ab7';
        
        // BLE objects
        this.device = null;
        this.server = null;
        this.service = null;
        this.characteristics = {};
        
        // State
        this.connected = false;
        this.motorSpeed = 1.0;
        this.motorDirection = true; // true = clockwise
        this.motorEnabled = false;
        this.microsteps = 32;
        this.current = 30;
        
        // UI elements
        this.initializeUIElements();
        this.bindEventListeners();
        
        // Check Web Bluetooth support
        if (!navigator.bluetooth) {
            this.showError('Web Bluetooth is not supported in this browser.');
            return;
        }
        
        console.log('BratenDreher BLE Controller initialized');
    }
    
    initializeUIElements() {
        // Connection elements
        this.statusIndicator = document.getElementById('statusIndicator');
        this.connectionStatus = document.getElementById('connectionStatus');
        this.connectBtn = document.getElementById('connectBtn');
        this.disconnectBtn = document.getElementById('disconnectBtn');
        
        // Control elements
        this.motorToggle = document.getElementById('motorToggle');
        this.speedSlider = document.getElementById('speedSlider');
        this.speedValue = document.getElementById('speedValue');
        this.clockwiseBtn = document.getElementById('clockwiseBtn');
        this.counterclockwiseBtn = document.getElementById('counterclockwiseBtn');
        this.emergencyStopBtn = document.getElementById('emergencyStopBtn');
        this.microstepsSelect = document.getElementById('microstepsSelect');
        this.currentSlider = document.getElementById('currentSlider');
        this.currentValue = document.getElementById('currentValue');
        this.resetStatsBtn = document.getElementById('resetStatsBtn');
        
        // Status elements
        this.motorStatus = document.getElementById('motorStatus');
        this.currentSpeed = document.getElementById('currentSpeed');
        this.currentDirection = document.getElementById('currentDirection');
        this.currentMicrosteps = document.getElementById('currentMicrosteps');
        this.currentCurrent = document.getElementById('currentCurrent');
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
        this.disconnectBtn.addEventListener('click', () => this.disconnect());
        
        // Motor controls
        this.motorToggle.addEventListener('change', (e) => {
            this.setMotorEnabled(e.target.checked);
        });
        
        this.speedSlider.addEventListener('input', (e) => {
            this.setSpeed(parseFloat(e.target.value));
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
        this.microstepsSelect.addEventListener('change', (e) => {
            this.setMicrosteps(parseInt(e.target.value));
        });
        
        this.currentSlider.addEventListener('input', (e) => {
            const current = parseInt(e.target.value);
            this.currentValue.textContent = current;
            this.setCurrent(current);
        });
        
        this.resetStatsBtn.addEventListener('click', () => {
            this.resetStatistics();
        });
    }
    
    async connect() {
        try {
            console.log('Requesting Bluetooth device...');
            this.updateConnectionStatus('Connecting...');
            
            // Request device
            this.device = await navigator.bluetooth.requestDevice({
                filters: [{ name: 'BratenDreher' }],
                optionalServices: [this.serviceUUID]
            });
            
            console.log('Device selected:', this.device.name);
            
            // Connect to GATT server
            this.server = await this.device.gatt.connect();
            console.log('Connected to GATT server');
            
            // Get service
            this.service = await this.server.getPrimaryService(this.serviceUUID);
            console.log('Service found');
            
            // Get characteristics
            this.characteristics.speed = await this.service.getCharacteristic(this.speedCharacteristicUUID);
            this.characteristics.direction = await this.service.getCharacteristic(this.directionCharacteristicUUID);
            this.characteristics.enable = await this.service.getCharacteristic(this.enableCharacteristicUUID);
            this.characteristics.status = await this.service.getCharacteristic(this.statusCharacteristicUUID);
            this.characteristics.microsteps = await this.service.getCharacteristic(this.microstepsCharacteristicUUID);
            this.characteristics.current = await this.service.getCharacteristic(this.currentCharacteristicUUID);
            this.characteristics.reset = await this.service.getCharacteristic(this.resetCharacteristicUUID);
            
            console.log('All characteristics found');
            
            // Subscribe to status notifications
            await this.characteristics.status.startNotifications();
            this.characteristics.status.addEventListener('characteristicvaluechanged', (event) => {
                this.handleStatusUpdate(event);
            });
            
            // Handle disconnection
            this.device.addEventListener('gattserverdisconnected', () => {
                this.onDisconnected();
            });
            
            this.connected = true;
            this.updateConnectionStatus('Connected');
            this.updateUI();
            
            console.log('Successfully connected to BratenDreher');
            
        } catch (error) {
            console.error('Connection failed:', error);
            this.showError(`Connection failed: ${error.message}`);
            this.updateConnectionStatus('Disconnected');
        }
    }
    
    async disconnect() {
        if (this.device && this.device.gatt.connected) {
            await this.device.gatt.disconnect();
        }
        this.onDisconnected();
    }
    
    onDisconnected() {
        this.connected = false;
        this.device = null;
        this.server = null;
        this.service = null;
        this.characteristics = {};
        
        this.updateConnectionStatus('Disconnected');
        this.updateUI();
        
        console.log('Disconnected from BratenDreher');
    }
    
    async setSpeed(speed) {
        speed = Math.max(0.1, Math.min(30.0, speed)); // Clamp to valid range
        this.motorSpeed = speed;
        this.speedValue.textContent = speed.toFixed(1);
        
        if (this.connected && this.characteristics.speed) {
            try {
                const value = speed.toString();
                await this.characteristics.speed.writeValue(new TextEncoder().encode(value));
                console.log(`Speed set to ${speed} RPM`);
            } catch (error) {
                console.error('Failed to set speed:', error);
                this.showError('Failed to set speed');
            }
        }
    }
    
    async setMicrosteps(microsteps) {
        this.microsteps = microsteps;
        
        if (this.connected && this.characteristics.microsteps) {
            try {
                const value = microsteps.toString();
                await this.characteristics.microsteps.writeValue(new TextEncoder().encode(value));
                console.log(`Microsteps set to ${microsteps}`);
            } catch (error) {
                console.error('Failed to set microsteps:', error);
                this.showError('Failed to set microsteps');
            }
        }
    }
    
    async setCurrent(current) {
        this.current = current;
        
        if (this.connected && this.characteristics.current) {
            try {
                const value = current.toString();
                await this.characteristics.current.writeValue(new TextEncoder().encode(value));
                console.log(`Current set to ${current}%`);
            } catch (error) {
                console.error('Failed to set current:', error);
                this.showError('Failed to set current');
            }
        }
    }
    
    async resetStatistics() {
        if (this.connected && this.characteristics.reset) {
            try {
                await this.characteristics.reset.writeValue(new TextEncoder().encode('1'));
                console.log('Statistics reset');
                
                // Visual feedback
                this.resetStatsBtn.textContent = '📊 Reset Successful';
                setTimeout(() => {
                    this.resetStatsBtn.textContent = '📊 Reset Statistics';
                }, 2000);
            } catch (error) {
                console.error('Failed to reset statistics:', error);
                this.showError('Failed to reset statistics');
            }
        }
    }
    
    async setDirection(clockwise) {
        this.motorDirection = clockwise;
        
        // Update UI
        this.clockwiseBtn.classList.toggle('active', clockwise);
        this.counterclockwiseBtn.classList.toggle('active', !clockwise);
        
        if (this.connected && this.characteristics.direction) {
            try {
                const value = clockwise ? '1' : '0';
                await this.characteristics.direction.writeValue(new TextEncoder().encode(value));
                console.log(`Direction set to ${clockwise ? 'clockwise' : 'counter-clockwise'}`);
            } catch (error) {
                console.error('Failed to set direction:', error);
                this.showError('Failed to set direction');
            }
        }
    }
    
    async setMotorEnabled(enabled) {
        this.motorEnabled = enabled;
        this.motorToggle.checked = enabled;
        
        if (this.connected && this.characteristics.enable) {
            try {
                const value = enabled ? '1' : '0';
                await this.characteristics.enable.writeValue(new TextEncoder().encode(value));
                console.log(`Motor ${enabled ? 'enabled' : 'disabled'}`);
            } catch (error) {
                console.error('Failed to set motor state:', error);
                this.showError('Failed to set motor state');
            }
        }
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
    
    handleStatusUpdate(event) {
        try {
            const value = new TextDecoder().decode(event.target.value);
            const status = JSON.parse(value);
            
            console.log('Status update:', status);
            
            // Update status display
            this.motorStatus.textContent = status.enabled ? 
                (status.running ? 'Running' : 'Enabled') : 'Stopped';
            this.currentSpeed.textContent = `${status.speed.toFixed(1)} RPM`;
            this.currentDirection.textContent = status.direction === 'cw' ? 'Clockwise' : 'Counter-clockwise';
            this.currentMicrosteps.textContent = status.microsteps || this.microsteps;
            this.currentCurrent.textContent = `${status.current || this.current}%`;
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
            
            // Update UI state if needed
            if (this.motorEnabled !== status.enabled) {
                this.motorEnabled = status.enabled;
                this.motorToggle.checked = status.enabled;
            }
            
            // Update microsteps and current if different
            if (status.microsteps && status.microsteps !== this.microsteps) {
                this.microsteps = status.microsteps;
                this.microstepsSelect.value = status.microsteps;
            }
            
            if (status.current && status.current !== this.current) {
                this.current = status.current;
                this.currentSlider.value = status.current;
                this.currentValue.textContent = status.current;
            }
            
        } catch (error) {
            console.error('Failed to parse status update:', error);
        }
    }
    
    formatTime(seconds) {
        const hours = Math.floor(seconds / 3600);
        const minutes = Math.floor((seconds % 3600) / 60);
        const secs = seconds % 60;
        return `${hours.toString().padStart(2, '0')}:${minutes.toString().padStart(2, '0')}:${secs.toString().padStart(2, '0')}`;
    }
    
    updateConnectionStatus(status) {
        this.connectionStatus.textContent = status;
        
        if (status === 'Connected') {
            this.statusIndicator.classList.add('connected');
            this.connectBtn.disabled = true;
            this.disconnectBtn.disabled = false;
        } else {
            this.statusIndicator.classList.remove('connected');
            this.connectBtn.disabled = false;
            this.disconnectBtn.disabled = true;
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
            this.microstepsSelect,
            this.currentSlider,
            this.resetStatsBtn
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
        // Simple error display - you could enhance this with a toast or modal
        console.error(message);
        alert(message);
    }
}

// Initialize the application when the page loads
document.addEventListener('DOMContentLoaded', () => {
    console.log('Initializing BratenDreher Web Interface');
    new BratenDreherBLE();
});
