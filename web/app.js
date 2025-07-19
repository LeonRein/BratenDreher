/**
 * BratenDreher Application - New Architecture
 * Uses UI-type based controls with separated command management
 */
class BratenDreherApp {
    constructor() {
        // Motor specifications for acceleration conversion
        this.MAX_SPEED_RPM = 30.0;
        this.GEAR_RATIO = 10;
        this.STEPS_PER_REVOLUTION = 200;
        this.MICROSTEPS = 16;
        
        // Initialize command manager
        this.commandManager = new CommandManager();
        
        // UI elements
        this.initializeUIElements();
        
        // Controls and bindings
        this.controls = new Map();
        this.bindings = new Map();
        
        // Initialize the application
        this.initializeControls();
        this.initializeBindings();
        this.bindEventListeners();
        this.setupCommandManagerCallbacks();
        
        // Ensure all controls start in disabled state
        this.updateUI();
        
        console.log('BratenDreher Application initialized with new architecture');
    }

    // Acceleration conversion methods (same as before)
    rpmToStepsPerSecond(rpm) {
        const motorRPM = rpm * this.GEAR_RATIO;
        const motorStepsPerSecond = (motorRPM * this.STEPS_PER_REVOLUTION * this.MICROSTEPS) / 60.0;
        return Math.floor(motorStepsPerSecond);
    }
    
    accelerationToTime(accelerationStepsPerSec2) {
        if (accelerationStepsPerSec2 === 0) {
            return 5.0;
        }
        const maxStepsPerSecond = this.rpmToStepsPerSecond(this.MAX_SPEED_RPM);
        const timeSeconds = maxStepsPerSecond / accelerationStepsPerSec2;
        return Math.max(1.0, Math.min(30.0, timeSeconds));
    }
    
    timeToAcceleration(timeSeconds) {
        const maxStepsPerSecond = this.rpmToStepsPerSecond(this.MAX_SPEED_RPM);
        const acceleration = maxStepsPerSecond / timeSeconds;
        return Math.floor(acceleration);
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
        this.currentSpeedIndicator = document.getElementById('currentSpeedIndicator');
        this.speedSliderFill = document.getElementById('speedSliderFill');
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
        this.setpointSpeed = document.getElementById('setpointSpeed');
        this.currentSpeed = document.getElementById('currentSpeed');
        this.currentAcceleration = document.getElementById('currentAcceleration');
        this.currentDirection = document.getElementById('currentDirection');
        this.currentCurrent = document.getElementById('currentCurrent');
        this.tmc2209Status = document.getElementById('tmc2209Status');
        this.tmc2209Temperature = document.getElementById('tmc2209Temperature');
        this.stallStatus = document.getElementById('stallStatus');
        this.stallCount = document.getElementById('stallCount');
        this.lastUpdate = document.getElementById('lastUpdate');
        
        // StallGuard elements
        this.stallguardThresholdSlider = document.getElementById('stallguardThresholdSlider');
        this.stallguardThresholdValue = document.getElementById('stallguardThresholdValue');
        this.stallguardResultValue = document.getElementById('stallguardResultValue');
        this.stallguardSliderFill = document.getElementById('stallguardSliderFill');
        
        // Variable speed status elements
        this.variableSpeedStatus = document.getElementById('variableSpeedStatus');
        
        // Statistics elements
        this.totalRevolutions = document.getElementById('totalRevolutions');
        this.runTime = document.getElementById('runTime');
        this.avgSpeed = document.getElementById('avgSpeed');
        
        // Power delivery elements
        this.voltageSelect = document.getElementById('voltageSelect');
        this.negotiateBtn = document.getElementById('negotiateBtn');
        this.autoNegotiateBtn = document.getElementById('autoNegotiateBtn');
        this.pdStatus = document.getElementById('pdStatus');
        this.pdPowerGood = document.getElementById('pdPowerGood');
        this.pdNegotiatedVoltage = document.getElementById('pdNegotiatedVoltage');
        this.pdCurrentVoltage = document.getElementById('pdCurrentVoltage');
        
        // Preset buttons
        this.presetBtns = document.querySelectorAll('.preset-btn');
    }

    initializeControls() {
        // Speed control
        this.controls.set('speedSlider', new SliderControl(this.speedSlider, {
            valueElement: this.speedValue,
            fillElement: this.speedSliderFill,
            displayTransform: (value) => value.toFixed(1),
            debounceTime: 500
        }));

        // Speed displays
        this.controls.set('setpointSpeedDisplay', new DisplayControl(this.setpointSpeed, {
            formatter: (value) => `${value.toFixed(1)} RPM`,
            colorizer: (value) => {
                if (value === 0) return '#1f2937';
                if (value < 5) return '#10b981';
                if (value < 15) return '#3b82f6';
                return '#8b5cf6';
            }
        }));

        this.controls.set('currentSpeedDisplay', new DisplayControl(this.currentSpeed, {
            formatter: (value) => `${value.toFixed(1)} RPM`,
            colorizer: (value) => {
                if (value === 0) return '#1f2937';
                if (value < 5) return '#10b981';
                if (value < 15) return '#3b82f6';
                return '#8b5cf6';
            }
        }));

        // Preset buttons
        this.controls.set('presetButtons', new ButtonControl(Array.from(this.presetBtns), {
            type: 'radio-group',
            activeClass: 'active'
        }));

        // Direction buttons - create as radio group
        this.controls.set('directionButtons', new ButtonControl([this.clockwiseBtn, this.counterclockwiseBtn], {
            type: 'radio-group',
            activeClass: 'active'
        }));

        // Direction display
        this.controls.set('directionDisplay', new DisplayControl(this.currentDirection));

        // Motor toggle
        this.controls.set('motorToggle', new ToggleControl(this.motorToggle));
        this.controls.set('motorStatusDisplay', new DisplayControl(this.motorStatus, {
            formatter: (enabled) => enabled ? 'Enabled' : 'Stopped',
            colorizer: (enabled) => enabled ? '#10b981' : '#1f2937'
        }));

        // Current control
        this.controls.set('currentSlider', new SliderControl(this.currentSlider, {
            valueElement: this.currentValue,
            displayTransform: (value) => value.toString(),
            debounceTime: 500
        }));

        this.controls.set('currentDisplay', new DisplayControl(this.currentCurrent, {
            formatter: (value) => `${value}%`,
            colorizer: (value) => {
                if (value <= 20) return '#10b981';
                if (value <= 50) return '#3b82f6';
                if (value <= 80) return '#f59e0b';
                return '#8b5cf6';
            }
        }));

        // Acceleration control
        this.controls.set('accelerationSlider', new SliderControl(this.accelerationTimeSlider, {
            valueElement: this.accelerationTimeValue,
            displayTransform: (value) => Number(value).toFixed(1),
            debounceTime: 500
        }));

        this.controls.set('accelerationDisplay', new DisplayControl(this.currentAcceleration, {
            formatter: (acceleration) => {
                const time = this.accelerationToTime(acceleration).toFixed(1);
                return `${time}s to max`;
            },
            colorizer: (acceleration) => {
                const time = this.accelerationToTime(acceleration);
                if (time <= 2) return '#8b5cf6';
                if (time <= 5) return '#3b82f6';
                if (time <= 10) return '#10b981';
                return '#1f2937';
            }
        }));

        // Variable speed controls - using CompositeControl for coordinated management
        const variableSpeedToggle = new ToggleControl(this.variableSpeedToggle);
        const variableSpeedStatusDisplay = new DisplayControl(this.variableSpeedStatus);
        const strengthSlider = new SliderControl(this.strengthSlider, {
            valueElement: this.strengthValue,
            displayTransform: (value) => value.toString(),
            debounceTime: 500
        });
        const phaseSlider = new SliderControl(this.phaseSlider, {
            valueElement: this.phaseValue,
            displayTransform: (value) => value.toString(),
            debounceTime: 500
        });

        // Create composite control for variable speed
        const variableSpeedComposite = new CompositeControl();
        variableSpeedComposite.addChildControl(variableSpeedToggle);
        variableSpeedComposite.addChildControl(variableSpeedStatusDisplay);
        variableSpeedComposite.addChildControl(strengthSlider);
        variableSpeedComposite.addChildControl(phaseSlider);

        this.controls.set('variableSpeedToggle', variableSpeedToggle);
        this.controls.set('variableSpeedStatusDisplay', variableSpeedStatusDisplay);
        this.controls.set('strengthSlider', strengthSlider);
        this.controls.set('phaseSlider', phaseSlider);
        this.controls.set('variableSpeedComposite', variableSpeedComposite);

        // StallGuard controls
        this.controls.set('stallguardSlider', new SliderControl(this.stallguardThresholdSlider, {
            valueElement: this.stallguardThresholdValue,
            fillElement: this.stallguardSliderFill,
            displayTransform: (value) => {
                const percentage = (value / 255) * 100;
                return `${percentage.toFixed(1)}%`;
            },
            debounceTime: 300
        }));

        this.controls.set('stallguardResultDisplay', new DisplayControl(this.stallguardResultValue));

        // TMC status displays - using CompositeControl for coordinated management
        const tmcStatusDisplay = new DisplayControl(this.tmc2209Status);
        const tmcTempDisplay = new DisplayControl(this.tmc2209Temperature);
        const stallStatusDisplay = new DisplayControl(this.stallStatus);
        const stallCountDisplay = new DisplayControl(this.stallCount);

        // Create composite control for TMC status
        const tmcStatusComposite = new CompositeControl();
        tmcStatusComposite.addChildControl(tmcStatusDisplay);
        tmcStatusComposite.addChildControl(tmcTempDisplay);
        tmcStatusComposite.addChildControl(stallStatusDisplay);
        tmcStatusComposite.addChildControl(stallCountDisplay);

        this.controls.set('tmcStatusDisplay', tmcStatusDisplay);
        this.controls.set('tmcTempDisplay', tmcTempDisplay);
        this.controls.set('stallStatusDisplay', stallStatusDisplay);
        this.controls.set('stallCountDisplay', stallCountDisplay);
        this.controls.set('tmcStatusComposite', tmcStatusComposite);

        // Power delivery controls
        this.controls.set('voltageSelect', new SelectControl(this.voltageSelect));
        this.controls.set('negotiateBtn', new ButtonControl(this.negotiateBtn));
        this.controls.set('autoNegotiateBtn', new ButtonControl(this.autoNegotiateBtn));

        // Statistics displays - using CompositeControl for coordinated management
        const totalRevolutionsDisplay = new DisplayControl(this.totalRevolutions, {
            formatter: (value) => value.toFixed(3)
        });

        const runTimeDisplay = new DisplayControl(this.runTime, {
            formatter: (milliseconds) => this.formatTime(milliseconds)
        });

        const avgSpeedDisplay = new DisplayControl(this.avgSpeed, {
            formatter: (value) => value.toFixed(1)
        });

        // Create composite control for statistics
        const statisticsComposite = new CompositeControl();
        statisticsComposite.addChildControl(totalRevolutionsDisplay);
        statisticsComposite.addChildControl(runTimeDisplay);
        statisticsComposite.addChildControl(avgSpeedDisplay);

        this.controls.set('totalRevolutionsDisplay', totalRevolutionsDisplay);
        this.controls.set('runTimeDisplay', runTimeDisplay);
        this.controls.set('avgSpeedDisplay', avgSpeedDisplay);
        this.controls.set('statisticsComposite', statisticsComposite);

        // Last update display
        this.controls.set('lastUpdateDisplay', new DisplayControl(this.lastUpdate));
    }

    initializeBindings() {
        // Speed control binding
        this.bindings.set('speed', new SpeedControlBinding(
            this.controls.get('speedSlider'),
            this.controls.get('setpointSpeedDisplay'),
            this.controls.get('presetButtons'),
            this.speedSliderFill
        ));

        // Direction control binding
        this.bindings.set('direction', new DirectionControlBinding(
            this.controls.get('directionButtons'),
            this.controls.get('directionDisplay')
        ));

        // Motor control binding
        this.bindings.set('motor', new ControlBinding({
            commandType: 'enable',
            statusKeys: ['enabled'],
            debounceTime: 0
        }));
        this.bindings.get('motor').addControl(this.controls.get('motorToggle'));
        this.bindings.get('motor').addControl(this.controls.get('motorStatusDisplay'));

        // Current control binding
        this.bindings.set('current', new ControlBinding({
            commandType: 'current',
            statusKeys: ['current'],
            valueTransform: (value) => parseInt(value)
        }));
        this.bindings.get('current').addControl(this.controls.get('currentSlider'));
        this.bindings.get('current').addControl(this.controls.get('currentDisplay'));

        // Acceleration control binding
        this.bindings.set('acceleration', new ControlBinding({
            commandType: 'acceleration',
            statusKeys: ['acceleration'],
            valueTransform: (timeValue) => {
                const acceleration = this.timeToAcceleration(parseInt(timeValue));
                const minAcceleration = 100;
                if (acceleration < minAcceleration) {
                    const minTime = this.accelerationToTime(minAcceleration).toFixed(1);
                    const control = this.controls.get('accelerationSlider');
                    if (control) {
                        control.setValue(minTime);
                    }
                    if (this.commandManager) {
                        this.commandManager.showWarning('Acceleration too low. Set to minimum allowed.');
                    }
                    return minAcceleration;
                }
                return acceleration;
            },
            statusTransform: (accelerationValue) => {
                // Convert from microsteps/s² (backend) to time seconds (UI slider)
                const timeSeconds = this.accelerationToTime(accelerationValue);
                return parseFloat(timeSeconds.toFixed(1));
            }
        }));
        this.bindings.get('acceleration').addControl(this.controls.get('accelerationSlider'));
        this.bindings.get('acceleration').addControl(this.controls.get('accelerationDisplay'));

        // Variable speed binding
        this.bindings.set('variableSpeed', new VariableSpeedControlBinding(
            this.controls.get('variableSpeedToggle'),
            this.controls.get('strengthSlider'),
            this.controls.get('phaseSlider'),
            this.controls.get('variableSpeedStatusDisplay'),
            this.variableSpeedControls
        ));

        // Strength binding
        this.bindings.set('strength', new ControlBinding({
            commandType: 'speed_variation_strength',
            statusKeys: ['speedVariationStrength'],
            valueTransform: (value) => parseInt(value) / 100.0,
            statusTransform: (value) => Math.round(value * 100)
        }));
        this.bindings.get('strength').addControl(this.controls.get('strengthSlider'));

        // Phase binding
        this.bindings.set('phase', new ControlBinding({
            commandType: 'speed_variation_phase',
            statusKeys: ['speedVariationPhase'],
            valueTransform: (value) => {
                const phase = parseInt(value);
                let phaseForRadians = phase;
                if (phaseForRadians < 0) {
                    phaseForRadians += 360;
                }
                return (phaseForRadians * Math.PI) / 180;
            },
            statusTransform: (value) => {
                let phaseDegrees = Math.round((value * 180) / Math.PI);
                if (phaseDegrees > 180) {
                    phaseDegrees -= 360;
                }
                return phaseDegrees;
            }
        }));
        this.bindings.get('phase').addControl(this.controls.get('phaseSlider'));

        // StallGuard binding
        this.bindings.set('stallguard', new StallGuardControlBinding(
            this.controls.get('stallguardSlider'),
            this.controls.get('stallguardResultDisplay'),
            this.stallguardSliderFill
        ));

        // Power delivery binding
        this.bindings.set('powerDelivery', new PowerDeliveryControlBinding(
            this.controls.get('voltageSelect'),
            this.controls.get('negotiateBtn'),
            this.controls.get('autoNegotiateBtn'),
            {
                status: this.pdStatus,
                powerGood: this.pdPowerGood,
                negotiatedVoltage: this.pdNegotiatedVoltage,
                currentVoltage: this.pdCurrentVoltage
            }
        ));

        // Statistics bindings
        this.bindings.set('statistics', new ControlBinding({
            statusKeys: ['totalRevolutions', 'runtime'],
            customStatusHandler: (statusUpdate, controls, config) => {
                if (statusUpdate.totalRevolutions !== undefined) {
                    this.controls.get('totalRevolutionsDisplay').updateValue(statusUpdate.totalRevolutions);
                }
                if (statusUpdate.runtime !== undefined) {
                    this.controls.get('runTimeDisplay').updateValue(statusUpdate.runtime);
                    // Calculate average speed
                    this.updateAverageSpeed();
                }
            }
        }));
        this.bindings.get('statistics').addControl(this.controls.get('totalRevolutionsDisplay'));
        this.bindings.get('statistics').addControl(this.controls.get('runTimeDisplay'));
        this.bindings.get('statistics').addControl(this.controls.get('avgSpeedDisplay'));

        // TMC status bindings
        this.bindings.set('tmcStatus', new ControlBinding({
            statusKeys: ['tmc2209Status', 'tmc2209Temperature', 'stallDetected', 'stallCount'],
            customStatusHandler: (statusUpdate, controls, config) => {
                if (statusUpdate.tmc2209Status !== undefined) {
                    const display = this.controls.get('tmcStatusDisplay');
                    display.updateValue(statusUpdate.tmc2209Status ? 'OK' : 'Error');
                    display.updateClass(statusUpdate.tmc2209Status ? 'status-success' : 'status-error');
                }
                
                if (statusUpdate.tmc2209Temperature !== undefined) {
                    const tempLabels = ['Normal', 'Warm (>120°C)', 'Elevated (>143°C)', 'High (>150°C)', 'Critical (>157°C)'];
                    const tempIdx = Math.max(0, Math.min(4, statusUpdate.tmc2209Temperature));
                    const display = this.controls.get('tmcTempDisplay');
                    display.updateValue(tempLabels[tempIdx]);
                    const className = tempIdx === 0 ? 'status-success' : (tempIdx < 3 ? 'status-warning' : 'status-error');
                    display.updateClass(className);
                }
                
                if (statusUpdate.stallDetected !== undefined) {
                    const display = this.controls.get('stallStatusDisplay');
                    display.updateValue(statusUpdate.stallDetected ? 'STALL!' : 'OK');
                    const color = statusUpdate.stallDetected ? '#e74c3c' : '#10b981';
                    display.displays.forEach(element => {
                        if (element) {
                            element.style.color = color;
                            element.style.fontWeight = statusUpdate.stallDetected ? 'bold' : 'normal';
                        }
                    });
                }
                
                if (statusUpdate.stallCount !== undefined) {
                    const display = this.controls.get('stallCountDisplay');
                    display.updateValue(statusUpdate.stallCount);
                    const color = statusUpdate.stallCount > 0 ? '#e74c3c' : '#10b981';
                    display.displays.forEach(element => {
                        if (element) element.style.color = color;
                    });
                }
            }
        }));

        // Current speed display binding
        this.bindings.set('currentSpeed', new ControlBinding({
            statusKeys: ['currentSpeed'],
            customStatusHandler: (statusUpdate, controls, config) => {
                if (statusUpdate.currentSpeed !== undefined) {
                    controls[0].updateValue(statusUpdate.currentSpeed);
                }
            }
        }));
        this.bindings.get('currentSpeed').addControl(this.controls.get('currentSpeedDisplay'));
        
        // Timestamp binding
        this.bindings.set('timestamp', new ControlBinding({
            statusKeys: [], // Always update on any status update
            customStatusHandler: () => {
                this.controls.get('lastUpdateDisplay').updateValue(new Date().toLocaleTimeString());
            }
        }));
        this.bindings.get('timestamp').addControl(this.controls.get('lastUpdateDisplay'));
        

        // Set command manager for all bindings
        this.bindings.forEach(binding => {
            binding.setCommandManager(this.commandManager);
        });
    }

    bindEventListeners() {
        // Connection buttons
        this.connectBtn.addEventListener('click', () => this.commandManager.connect());
        this.reconnectBtn.addEventListener('click', () => this.commandManager.handleReconnect());
        this.disconnectBtn.addEventListener('click', () => this.commandManager.disconnect());

        // Bind all control events
        this.controls.forEach(control => {
            control.bindEvents();
        });

        // Speed slider event
        this.controls.get('speedSlider').options.onValueChange = (value) => {
            this.bindings.get('speed').handleValueChange(value);
        };

        // Preset button events
        this.controls.get('presetButtons').options.onClick = (value, index, button) => {
            const speed = parseFloat(button.dataset.speed);
            this.controls.get('speedSlider').setValue(speed);
            this.bindings.get('speed').handleValueChange(speed);
        };

        // Direction button events
        this.controls.get('directionButtons').options.onClick = (value, index, button) => {
            const clockwise = index === 0; // First button is clockwise
            this.bindings.get('direction').setDirection(clockwise);
        };

        // Motor toggle event
        this.controls.get('motorToggle').options.onChange = (enabled) => {
            this.bindings.get('motor').handleValueChange(enabled);
        };

        // Current slider event
        this.controls.get('currentSlider').options.onValueChange = (value) => {
            this.bindings.get('current').handleValueChange(value);
        };

        // Acceleration slider event
        this.controls.get('accelerationSlider').options.onValueChange = (value) => {
            this.bindings.get('acceleration').handleValueChange(value);
        };

        // Variable speed toggle event
        this.controls.get('variableSpeedToggle').options.onChange = (enabled) => {
            this.bindings.get('variableSpeed').setVariableSpeedEnabled(enabled);
        };

        // Strength slider event
        this.controls.get('strengthSlider').options.onValueChange = (value) => {
            this.bindings.get('strength').handleValueChange(value);
        };

        // Phase slider event
        this.controls.get('phaseSlider').options.onValueChange = (value) => {
            this.bindings.get('phase').handleValueChange(value);
        };

        // StallGuard slider event
        this.controls.get('stallguardSlider').options.onValueChange = (value) => {
            this.bindings.get('stallguard').handleValueChange(value);
        };

        // Power delivery events
        this.controls.get('negotiateBtn').options.onClick = () => {
            this.bindings.get('powerDelivery').negotiateVoltage();
        };

        this.controls.get('autoNegotiateBtn').options.onClick = () => {
            this.bindings.get('powerDelivery').autoNegotiate();
        };

        // Emergency stop
        this.emergencyStopBtn.addEventListener('click', () => {
            this.emergencyStop();
        });

        // Reset buttons
        this.resetStatsBtn.addEventListener('click', () => {
            this.resetStatistics();
        });

        this.resetStallBtn.addEventListener('click', () => {
            this.resetStallCount();
        });
    }

    setupCommandManagerCallbacks() {
        // Connection status updates
        this.commandManager.onConnectionChange = (status, info) => {
            this.updateConnectionStatus(status, info);
        };

        // Status updates from backend
        this.commandManager.onStatusUpdate = (statusUpdate) => {
            this.handleStatusUpdate(statusUpdate);
        };

        // Notifications (warnings and errors)
        this.commandManager.onNotification = (notification) => {
            this.handleNotification(notification);
        };
    }

    handleStatusUpdate(statusUpdate) {
        // Delegate to all bindings
        this.bindings.forEach(binding => {
            binding.handleStatusUpdate(statusUpdate);
        });
    }

    handleNotification(notification) {
        const level = notification.level;
        const message = notification.message || '';
        
        if (level === 'warning') {
            this.commandManager.showWarning(message);
        } else if (level === 'error') {
            this.commandManager.showError(message);
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
            this.reconnectBtn.disabled = false;
            this.disconnectBtn.disabled = true;
        }

        // Update all controls based on connection state
        this.updateUI();
    }

    getStatusInfo(status) {
        switch (status) {
            case 'Connected':
                return this.commandManager.getDevice() ? 
                    `Connected to ${this.commandManager.getDevice().name}` : 
                    'Connected to BratenDreher';
            case 'Connecting...':
                return 'Searching for BratenDreher device...';
            case 'Reconnecting...':
                return 'Attempting to reconnect...';
            case 'Disconnected':
                return this.commandManager.getDevice() ? 
                    'Disconnected - Use Reconnect/Retry button' : 
                    'Click Connect to start';
            default:
                return '';
        }
    }

    updateUI() {
        // Determine the appropriate state based on connection status
        let state;
        if (!this.commandManager.isConnected()) {
            state = CONTROL_STATES.DISABLED;
        } else {
            // When connected, set to OUTDATED initially - status updates will set to VALID
            state = CONTROL_STATES.OUTDATED;
        }
        
        // Update all controls
        this.controls.forEach(control => {
            control.setDisplayState(state, true);
        });

        // Handle other UI elements
        const otherControls = [
            this.emergencyStopBtn,
            this.resetStatsBtn,
            this.resetStallBtn
        ];
        
        const opacity = this.commandManager.isConnected() ? '0.7' : '0.4';
        const disabled = !this.commandManager.isConnected();
        
        otherControls.forEach(control => {
            if (control) {
                control.disabled = disabled;
                control.style.opacity = opacity;
                if (disabled) {
                    control.classList.add('disabled');
                } else {
                    control.classList.remove('disabled');
                }
            }
        });

        // Hide fill indicators when disconnected
        if (!this.commandManager.isConnected()) {
            this.controls.get('speedSlider').hideFill();
            this.bindings.get('stallguard').hideFill();
        }
    }

    async emergencyStop() {
        console.log('Emergency stop triggered');
        
        // Hide speed fill during emergency stop
        this.controls.get('speedSlider').hideFill();
        
        await this.bindings.get('motor').handleValueChange(false);
        
        // Visual feedback
        this.emergencyStopBtn.style.background = '#dc2626';
        this.emergencyStopBtn.textContent = '🛑 STOPPED';
        
        setTimeout(() => {
            this.emergencyStopBtn.style.background = '#ef4444';
            this.emergencyStopBtn.textContent = '🛑 Emergency Stop';
        }, 2000);
    }

    async resetStatistics() {
        const success = await this.commandManager.sendCommand('reset', true);
        if (success) {
            this.resetStatsBtn.textContent = '📊 Reset Successful';
            setTimeout(() => {
                this.resetStatsBtn.textContent = '📊 Reset Statistics';
            }, 2000);
        }
        return success;
    }

    async resetStallCount() {
        const success = await this.commandManager.sendCommand('reset_stall', true);
        if (success) {
            this.resetStallBtn.textContent = '⚠️ Reset Successful';
            setTimeout(() => {
                this.resetStallBtn.textContent = '⚠️ Reset Stall Count';
            }, 2000);
        }
        return success;
    }

    updateAverageSpeed() {
        const revolutionsElement = this.totalRevolutions;
        const runtimeElement = this.runTime;
        
        if (!revolutionsElement || !runtimeElement) return;
        
        const currentRevolutions = parseFloat(revolutionsElement.textContent) || 0;
        const currentRuntimeText = runtimeElement.textContent;
        let currentRuntimeSeconds = 0;
        
        // Parse runtime from HH:MM:SS.mmm format
        if (currentRuntimeText && currentRuntimeText !== '00:00:00.000') {
            const [timePart, millisPart = '0'] = currentRuntimeText.split('.');
            const timeParts = timePart.split(':');
            currentRuntimeSeconds = parseInt(timeParts[0]) * 3600 + parseInt(timeParts[1]) * 60 + parseInt(timeParts[2]);
            if (millisPart) {
                currentRuntimeSeconds += parseInt(millisPart) / 1000;
            }
        }
        
        if (currentRuntimeSeconds > 0 && currentRevolutions > 0) {
            const avgSpeed = (currentRevolutions * 60) / currentRuntimeSeconds;
            this.controls.get('avgSpeedDisplay').updateValue(avgSpeed);
        } else {
            this.controls.get('avgSpeedDisplay').updateValue(0.0);
        }
    }

    formatTime(milliseconds) {
        const totalSeconds = Math.floor(milliseconds / 1000);
        const millis = milliseconds % 1000;
        const hours = Math.floor(totalSeconds / 3600);
        const minutes = Math.floor((totalSeconds % 3600) / 60);
        const secs = totalSeconds % 60;
        return `${hours.toString().padStart(2, '0')}:${minutes.toString().padStart(2, '0')}:${secs.toString().padStart(2, '0')}.${millis.toString().padStart(3, '0')}`;
    }
}
