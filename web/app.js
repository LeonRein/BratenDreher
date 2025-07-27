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
        this.presetBtns = document.querySelectorAll('.preset');
    }

    initializeControls() {
        // Speed control
        this.controls.set('speedSlider', new SliderControl(this.speedSlider, {
            fillElement: this.speedSliderFill,
            debounceTime: 500
        }));
        this.controls.set('speedValueDisplay', new DisplayControl(this.speedValue));

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
        this.controls.set('presetButtons', new RadioGroupControl(Array.from(this.presetBtns), {
            activeClass: 'active'
        }));

        // Emergency stop button
        this.controls.set('emergencyStopBtn', new SingleButtonControl(this.emergencyStopBtn));

        // Statistics reset button
        this.controls.set('resetStatsBtn', new SingleButtonControl(this.resetStatsBtn));

        // Stall reset button
        this.controls.set('resetStallBtn', new SingleButtonControl(this.resetStallBtn));

        // Direction buttons - create as radio group
        this.controls.set('directionButtons', new RadioGroupControl([this.clockwiseBtn, this.counterclockwiseBtn], {
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
            debounceTime: 500
        }));
        this.controls.set('currentValueDisplay', new DisplayControl(this.currentValue));

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
            debounceTime: 500
        }));
        this.controls.set('accelerationTimeValueDisplay', new DisplayControl(this.accelerationTimeValue));

        this.controls.set('accelerationDisplay', new DisplayControl(this.currentAcceleration, {
            formatter: (timeSeconds) => `${timeSeconds.toFixed(1)}s to max`,
            colorizer: (timeSeconds) => {
                if (timeSeconds <= 2) return '#8b5cf6';
                if (timeSeconds <= 5) return '#3b82f6';
                if (timeSeconds <= 10) return '#10b981';
                return '#1f2937';
            }
        }));

        // Variable speed controls - using CompositeControl for coordinated management
        const variableSpeedToggle = new ToggleControl(this.variableSpeedToggle);
        const variableSpeedStatusDisplay = new DisplayControl(this.variableSpeedStatus);
        const strengthSlider = new SliderControl(this.strengthSlider, {
            debounceTime: 500
        });
        const phaseSlider = new SliderControl(this.phaseSlider, {
            debounceTime: 500
        });
        this.controls.set('strengthValueDisplay', new DisplayControl(this.strengthValue));
        this.controls.set('phaseValueDisplay', new DisplayControl(this.phaseValue));

        // Variable speed graph control
        this.variableSpeedGraphCanvas = document.getElementById('variableSpeedGraph');
        const graphControl = new GraphControl(this.variableSpeedGraphCanvas);
        this.controls.set('variableSpeedGraph', graphControl);

        // Create composite control for variable speed
        const variableSpeedComposite = new CompositeControl();
        variableSpeedComposite.addChildControl(variableSpeedToggle);
        variableSpeedComposite.addChildControl(variableSpeedStatusDisplay);
        variableSpeedComposite.addChildControl(strengthSlider);
        variableSpeedComposite.addChildControl(phaseSlider);
        variableSpeedComposite.addChildControl(graphControl);

        this.controls.set('variableSpeedToggle', variableSpeedToggle);
        this.controls.set('variableSpeedStatusDisplay', variableSpeedStatusDisplay);
        this.controls.set('strengthSlider', strengthSlider);
        this.controls.set('phaseSlider', phaseSlider);
        this.controls.set('variableSpeedComposite', variableSpeedComposite);

        // StallGuard controls
        this.controls.set('stallguardSlider', new SliderControl(this.stallguardThresholdSlider, {
            fillElement: this.stallguardSliderFill,
            debounceTime: 300
        }));
        this.controls.set('stallguardThresholdValueDisplay', new DisplayControl(this.stallguardThresholdValue));

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
        this.controls.set('negotiateBtn', new SingleButtonControl(this.negotiateBtn));
        this.controls.set('autoNegotiateBtn', new SingleButtonControl(this.autoNegotiateBtn));
        this.controls.set('pdStatusDisplay', new DisplayControl(this.pdStatus));
        this.controls.set('pdPowerGoodDisplay', new DisplayControl(this.pdPowerGood));
        this.controls.set('pdNegotiatedVoltageDisplay', new DisplayControl(this.pdNegotiatedVoltage));
        this.controls.set('pdCurrentVoltageDisplay', new DisplayControl(this.pdCurrentVoltage));

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
            this.controls.get('speedValueDisplay')
        ));

        // Direction control binding
        this.bindings.set('direction', new DirectionControlBinding(
            this.controls.get('directionButtons'),
            this.controls.get('directionDisplay')
        ));
        // Emergency stop binding
        this.bindings.set('emergencyStop', new EmergencyStopControlBinding(
            this.controls.get('emergencyStopBtn'),
            this.controls.get('speedSlider')
        ));
        /* Removed redundant addControl for emergencyStop; handled in EmergencyStopControlBinding */

        // Statistics reset binding
        this.bindings.set('statisticsReset', new StatisticsResetControlBinding(
            this.controls.get('resetStatsBtn')
        ));
        /* Removed redundant addControl for statisticsReset; handled in StatisticsResetControlBinding */

        // Stall reset binding
        this.bindings.set('stallReset', new StallResetControlBinding(
            this.controls.get('resetStallBtn')
        ));
        /* Removed redundant addControl for stallReset; handled in StallResetControlBinding */

        // Motor control binding
        this.bindings.set('motor', new MotorControlBinding(
            this.controls.get('motorToggle'),
            this.controls.get('motorStatusDisplay')
        ));

        // Current control binding
        this.bindings.set('current', new CurrentControlBinding(
            this.controls.get('currentSlider'),
            this.controls.get('currentDisplay'),
            this.controls.get('currentValueDisplay')
        ));

        this.bindings.set('acceleration', new AccelerationControlBinding(
            this.controls.get('accelerationSlider'),
            this.controls.get('accelerationDisplay'),
            this.controls.get('accelerationTimeValueDisplay')
        ));

        // Variable speed binding
        this.bindings.set('variableSpeed', new VariableSpeedControlBinding(
            this.controls.get('variableSpeedToggle'),
            this.controls.get('strengthSlider'),
            this.controls.get('phaseSlider'),
            this.controls.get('variableSpeedStatusDisplay'),
            this.variableSpeedControls
        ));

        // Variable speed graph binding
        this.bindings.set('variableSpeedGraph', new VariableSpeedGraphControlBinding(
            this.controls.get('variableSpeedGraph')
        ));

        // Strength binding
        this.bindings.set('strength', new StrengthControlBinding(
            this.controls.get('strengthSlider'),
            this.controls.get('strengthValueDisplay')
        ));

        // Phase binding
        this.bindings.set('phase', new PhaseControlBinding(
            this.controls.get('phaseSlider'),
            this.controls.get('phaseValueDisplay')
        ));

        // StallGuard binding
        this.bindings.set('stallguard', new StallGuardControlBinding(
            this.controls.get('stallguardSlider'),
            this.controls.get('stallguardResultDisplay'),
            this.controls.get('stallguardThresholdValueDisplay')
        ));

        // Power delivery binding
        this.bindings.set('powerDelivery', new PowerDeliveryControlBinding(
            this.controls.get('voltageSelect'),
            this.controls.get('negotiateBtn'),
            this.controls.get('autoNegotiateBtn'),
            this.controls.get('pdStatusDisplay'),
            this.controls.get('pdPowerGoodDisplay'),
            this.controls.get('pdNegotiatedVoltageDisplay'),
            this.controls.get('pdCurrentVoltageDisplay')
        ));

        this.bindings.set('statistics', new StatisticsControlBinding(
            this.controls.get('totalRevolutionsDisplay'),
            this.controls.get('runTimeDisplay'),
            this.controls.get('avgSpeedDisplay'),
            this.updateAverageSpeed.bind(this)
        ));

        this.bindings.set('tmcStatus', new TmcStatusControlBinding(
            this.controls.get('tmcStatusDisplay'),
            this.controls.get('tmcTempDisplay'),
            this.controls.get('stallStatusDisplay'),
            this.controls.get('stallCountDisplay')
        ));

        this.bindings.set('currentSpeed', new CurrentSpeedControlBinding(
            this.controls.get('currentSpeedDisplay')
        ));

        this.bindings.set('timestamp', new TimestampControlBinding(
            this.controls.get('lastUpdateDisplay')
        ));


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

        // All control event handlers are now set in their respective ControlBindings classes.
    }

    setupCommandManagerCallbacks() {
        // Connection status updates
        this.commandManager.onConnectionChange = (status, info) => {
            this.updateConnectionStatus(status, info);
        };

        // Status updates from backend
        this.commandManager.onStatusUpdate = (statusUpdate) => {
            // Update last update time on every status message
            this.controls.get('lastUpdateDisplay').updateValue(new Date().toLocaleTimeString());
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

        const opacity = this.commandManager.isConnected() ? '1' : '0.4';
        const disabled = !this.commandManager.isConnected();
        // emergencyStop, resetStatistics, and resetStallCount logic now handled by bindings.

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
        const tenths = Math.floor((milliseconds % 1000) / 100);
        const hours = Math.floor(totalSeconds / 3600);
        const minutes = Math.floor((totalSeconds % 3600) / 60);
        const secs = totalSeconds % 60;
        return `${hours.toString().padStart(2, '0')}:${minutes.toString().padStart(2, '0')}:${secs.toString().padStart(2, '0')}.${tenths}`;
    }
}
