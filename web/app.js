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

        // Tell the user up front if their browser cannot do Bluetooth at all,
        // instead of leaving them with a UI whose Connect button does nothing.
        this.checkBluetoothSupport();

        console.log('BratenDreher Application initialized with new architecture');
    }

    checkBluetoothSupport() {
        if (navigator.bluetooth) return;

        this.connectBtn.disabled = true;
        this.reconnectBtn.disabled = true;

        const banner = document.createElement('div');
        banner.className = 'card unsupported-banner';
        banner.innerHTML =
            '<h2>⚠️ Bluetooth nicht verfügbar</h2>' +
            '<p>Dieser Browser unterstützt die Web Bluetooth API nicht, ' +
            'daher kann keine Verbindung zum BratenDreher aufgebaut werden.</p>' +
            '<p>Unter <strong>Android</strong> funktioniert Chrome (oder Edge). ' +
            'Unter <strong>iOS</strong> ist Web Bluetooth in keinem Browser verfügbar, ' +
            'da alle auf WebKit basieren müssen.</p>';

        const container = document.querySelector('.container');
        const header = container ? container.querySelector('.header') : null;
        if (header && header.nextSibling) {
            container.insertBefore(banner, header.nextSibling);
        } else if (container) {
            container.insertBefore(banner, container.firstChild);
        }
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
            debounceTime: 500
        }));
        this.controls.set('speedSliderFill', new SliderFillControl(this.speedSliderFill));
        this.controls.set('speedValueDisplay', new DisplayControl(this.speedValue));

        // Speed displays (formatting is applied by SpeedControlBinding)
        this.controls.set('setpointSpeedDisplay', new DisplayControl(this.setpointSpeed));
        this.controls.set('currentSpeedDisplay', new DisplayControl(this.currentSpeed));

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
            displayTransform: (enabled) => enabled ? 'Enabled' : 'Stopped'
        }));

        // Current control
        this.controls.set('currentSlider', new SliderControl(this.currentSlider, {
            debounceTime: 500
        }));
        this.controls.set('currentValueDisplay', new DisplayControl(this.currentValue));

        this.controls.set('currentDisplay', new DisplayControl(this.currentCurrent));

        // Acceleration control
        this.controls.set('accelerationSlider', new SliderControl(this.accelerationTimeSlider, {
            debounceTime: 500
        }));
        this.controls.set('accelerationTimeValueDisplay', new DisplayControl(this.accelerationTimeValue));

        this.controls.set('accelerationDisplay', new DisplayControl(this.currentAcceleration));

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
            debounceTime: 300
        }));
        this.controls.set('stallguardSliderFill', new SliderFillControl(this.stallguardSliderFill));
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

        // Statistics displays - using CompositeControl for coordinated management.
        // Formatting is applied by StatisticsControlBinding.
        const totalRevolutionsDisplay = new DisplayControl(this.totalRevolutions);
        const runTimeDisplay = new DisplayControl(this.runTime);
        const avgSpeedDisplay = new DisplayControl(this.avgSpeed);

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
            this.controls.get('speedSliderFill'),
            this.controls.get('setpointSpeedDisplay'),
            this.controls.get('presetButtons'),
            this.controls.get('speedValueDisplay'),
            this.controls.get('currentSpeedDisplay')
        ));

        // Direction control binding
        this.bindings.set('direction', new DirectionControlBinding(
            this.controls.get('directionButtons'),
            this.controls.get('directionDisplay')
        ));
        // Emergency stop binding. Takes the slider *fill* control - that is the
        // one with hideFill(), used to blank the speed indicator on stop.
        this.bindings.set('emergencyStop', new EmergencyStopControlBinding(
            this.controls.get('emergencyStopBtn'),
            this.controls.get('speedSliderFill')
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

        // Variable speed binding. The third argument is the DOM container that
        // gets dimmed when variation is off, not a registered control.
        this.bindings.set('variableSpeed', new VariableSpeedControlBinding(
            this.controls.get('variableSpeedToggle'),
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
            this.controls.get('stallguardSliderFill'),
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
            this.controls.get('avgSpeedDisplay')
        ));

        this.bindings.set('tmcStatus', new TmcStatusControlBinding(
            this.controls.get('tmcStatusDisplay'),
            this.controls.get('tmcTempDisplay'),
            this.controls.get('stallStatusDisplay'),
            this.controls.get('stallCountDisplay')
        ));

        /* currentSpeed binding merged into speed binding */
        /* The "last update" display is driven directly from onStatusUpdate,
           since the firmware sends no dedicated timestamp field. */

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
            this.controls.get('lastUpdateDisplay').setValue(new Date().toLocaleTimeString());
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
}
