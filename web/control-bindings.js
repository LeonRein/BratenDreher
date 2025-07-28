/**
 * Control Bindings - Maps UI events to commands and status updates to UI updates
 * Provides configuration-driven binding between controls and command manager
 */
class ControlBinding {
    constructor({
        statusKeys = [],
        debounceTime = 0 // ms, 0 disables debounce
    } = {}) {
        this.statusKeys = statusKeys;
        this.debounceTime = debounceTime;
        this.debounceTime = debounceTime;

        this.controls = [];
        this.commandManager = null;
        this._debounceTimer = null;
        this._lastDebounceArgs = null;
    }

    inputValueTransform(value) {
        return value;
    }

    statusValueTransform(value, key) {
        return value;
    }

    customStatusHandler(transformedValue, key) {
        // Default: update all controls with transformed value
        this.controls.forEach(control => {
            control.setValue(transformedValue);
        });
    }

    setCommandManager(commandManager) {
        this.commandManager = commandManager;
    }

    addControl(control) {
        this.controls.push(control);
    }

    // Handle value changes from UI controls
    async handleValueChange(value, commandType) {
        if (!this.commandManager || !commandType) {
            console.warn('ControlBinding: No command manager or command type configured');
            return false;
        }

        // Set all controls to outdated state
        this.controls.forEach(control => {
            control.setDisplayState(CONTROL_STATES.OUTDATED);
        });

        // Transform value
        const transformedValue = this.inputValueTransform(value);

        // Debounce only command manager calls, not display updates
        if (this.debounceTime > 0) {
            // Save latest args for debounce
            this._lastDebounceArgs = { commandType, transformedValue };
            if (this._debounceTimer) clearTimeout(this._debounceTimer);
            this._debounceTimer = setTimeout(async () => {
                const args = this._lastDebounceArgs;
                const success = await this.commandManager.sendCommand(
                    args.commandType,
                    args.transformedValue
                );
                if (success) {
                    this.controls.forEach(control => {
                        control.setDisplayState(CONTROL_STATES.RETRY);
                    });
                }
            }, this.debounceTime);
            return true;
        } else {
            // Immediate command
            const success = await this.commandManager.sendCommand(
                commandType,
                transformedValue
            );
            if (success) {
                this.controls.forEach(control => {
                    control.setDisplayState(CONTROL_STATES.RETRY);
                });
            }
            return success;
        }
    }

    // Handle status updates from the backend
    handleStatusUpdate(statusUpdate) {
        const relevantKeys = this.statusKeys.filter(key =>
            statusUpdate[key] !== undefined
        );

        if (relevantKeys.length === 0) {
            return;
        }

        this.controls.forEach(control => {
            control.setDisplayState(CONTROL_STATES.VALID);
        });

        relevantKeys.forEach(key => {
            const value = statusUpdate[key];
            const transformedValue = this.statusValueTransform(value, key);
            this.customStatusHandler(transformedValue, key);
        });
    }
}

/**
 * Specialized bindings for complex controls
 */

class AccelerationControlBinding extends ControlBinding {
    constructor(accelerationSlider, accelerationDisplay, accelerationTimeValueDisplay) {
        super({
            statusKeys: ['acc'],
            debounceTime: 150
        });

        // Set displayTransform for acceleration displays
        if (accelerationDisplay) {
            accelerationDisplay.displayTransform = (value) => `${Number(value).toFixed(1)}s to max`;
            accelerationDisplay.options.colorizer = (timeSeconds) => {
                if (timeSeconds <= 2) return '#8b5cf6';
                if (timeSeconds <= 5) return '#3b82f6';
                if (timeSeconds <= 10) return '#10b981';
                return '#1f2937';
            };
        }
        if (accelerationTimeValueDisplay) {
            accelerationTimeValueDisplay.displayTransform = (value) => `${Number(value).toFixed(1)}s`;
        }

        this.accelerationSlider = accelerationSlider;
        this.accelerationDisplay = accelerationDisplay;
        this.accelerationTimeValueDisplay = accelerationTimeValueDisplay;

        this.addControl(accelerationSlider);
        this.addControl(accelerationDisplay);

        // Wire up event handler
        this.accelerationSlider.onChange((value) => {
            this.accelerationTimeValueDisplay.setValue(value);
            this.handleValueChange(value, 'sa');
        });
    }

    static MAX_SPEED_RPM = 30.0;
    static GEAR_RATIO = 10;
    static STEPS_PER_REVOLUTION = 200;
    static MICROSTEPS = 16;

    rpmToStepsPerSecond(rpm) {
        const motorRPM = rpm * AccelerationControlBinding.GEAR_RATIO;
        const motorStepsPerSecond = (motorRPM * AccelerationControlBinding.STEPS_PER_REVOLUTION * AccelerationControlBinding.MICROSTEPS) / 60.0;
        return Math.floor(motorStepsPerSecond);
    }

    accelerationToTime(accelerationStepsPerSec2) {
        if (accelerationStepsPerSec2 === 0) {
            return 5.0;
        }
        const maxStepsPerSecond = this.rpmToStepsPerSecond(AccelerationControlBinding.MAX_SPEED_RPM);
        const timeSeconds = maxStepsPerSecond / accelerationStepsPerSec2;
        return Math.max(1.0, timeSeconds);
    }

    timeToAcceleration(timeSeconds) {
        const maxStepsPerSecond = this.rpmToStepsPerSecond(AccelerationControlBinding.MAX_SPEED_RPM);
        const acceleration = maxStepsPerSecond / timeSeconds;
        return Math.floor(acceleration);
    }

    inputValueTransform(sliderValue) {
        const time = parseFloat(sliderValue);
        const acceleration = this.timeToAcceleration(time);
        const minAcceleration = 100;
        if (acceleration < minAcceleration) {
            const minTime = this.accelerationToTime(minAcceleration).toFixed(1);
            if (this.accelerationSlider) {
                this.accelerationSlider.setValue(minTime);
            }
            return minAcceleration;
        }
        return acceleration;
    }

    statusValueTransform(accelerationValue) {
        const timeSeconds = this.accelerationToTime(accelerationValue);
        return parseFloat(timeSeconds.toFixed(1));
    }

    customStatusHandler(transformedValue, key) {
        if (key === 'acc') {
            this.accelerationSlider.setValue(transformedValue);
            this.accelerationDisplay.setValue(transformedValue);
            if (this.accelerationTimeValueDisplay) this.accelerationTimeValueDisplay.setValue(transformedValue.toFixed(1));
        }
    }
}

class StatisticsControlBinding extends ControlBinding {
    constructor(totalRevolutionsDisplay, runTimeDisplay, avgSpeedDisplay) {
        super({
            statusKeys: ['tr', 'rt']
        });

        totalRevolutionsDisplay.displayTransform = (value) => `${Number(value).toFixed(2)}`;
        runTimeDisplay.displayTransform = (milliseconds) => {
            // Format as hh:mm:ss.cc (centiseconds, two decimals)
            const ms = Number(milliseconds);
            const hours = Math.floor(ms / 3600000);
            const minutes = Math.floor((ms % 3600000) / 60000);
            const seconds = Math.floor((ms % 60000) / 1000);
            const centiseconds = ((ms % 1000) / 10).toFixed(2);
            return `${hours.toString().padStart(2, '0')}:${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}.${centiseconds}`;
        };
        avgSpeedDisplay.displayTransform = (value) => `${Number(value).toFixed(2)} rpm`;

        this.totalRevolutionsDisplay = totalRevolutionsDisplay;
        this.runTimeDisplay = runTimeDisplay;
        this.avgSpeedDisplay = avgSpeedDisplay;

        this.latestRevolutions = 0;
        this.latestRuntimeMs = 0;

        this.addControl(totalRevolutionsDisplay);
        this.addControl(runTimeDisplay);
        this.addControl(avgSpeedDisplay);
    }

    statusValueTransform(value) {
        return Number(value);
    }

    customStatusHandler(transformedValue, key) {
        if (key === 'tr') {
            this.latestRevolutions = transformedValue || 0;
            this.totalRevolutionsDisplay.setValue(transformedValue);
        }
        if (key === 'rt') {
            this.latestRuntimeMs = transformedValue || 0;
            this.runTimeDisplay.setValue(transformedValue);
        }
        // Calculate average speed if both are available
        if (this.latestRuntimeMs > 0 && this.latestRevolutions > 0) {
            const runtimeSeconds = this.latestRuntimeMs / 1000;
            const avgSpeed = (this.latestRevolutions * 60) / runtimeSeconds;
            this.avgSpeedDisplay.setValue(avgSpeed);
        } else {
            this.avgSpeedDisplay.setValue(0.0);
        }
    }
}

class TmcStatusControlBinding extends ControlBinding {
    constructor(tmcStatusDisplay, tmcTempDisplay, stallStatusDisplay, stallCountDisplay) {
        super({
            statusKeys: ['tmcst', 'tmct', 'sd', 'sc']
        });

        // Set displayTransform for each DisplayControl
        if (tmcStatusDisplay) {
            tmcStatusDisplay.displayTransform = (value) => value ? 'OK' : 'Error';
        }
        if (tmcTempDisplay) {
            tmcTempDisplay.displayTransform = (value) => {
                const tempLabels = ['Normal', 'Warm (>120°C)', 'Elevated (>143°C)', 'High (>150°C)', 'Critical (>157°C)'];
                const tempIdx = Math.max(0, Math.min(4, value));
                return tempLabels[tempIdx];
            };
        }
        if (stallStatusDisplay) {
            stallStatusDisplay.displayTransform = (value) => value ? 'STALL!' : 'OK';
            stallStatusDisplay.options.colorizer = (value) => value ? '#e74c3c' : '#10b981';
        }
        if (stallCountDisplay) {
            stallCountDisplay.displayTransform = (value) => value.toString();
            stallCountDisplay.options.colorizer = (value) => value > 0 ? '#e74c3c' : '#10b981';
        }

        this.tmcStatusDisplay = tmcStatusDisplay;
        this.tmcTempDisplay = tmcTempDisplay;
        this.stallStatusDisplay = stallStatusDisplay;
        this.stallCountDisplay = stallCountDisplay;

        this.addControl(tmcStatusDisplay);
        this.addControl(tmcTempDisplay);
        this.addControl(stallStatusDisplay);
        this.addControl(stallCountDisplay);
    }

    statusValueTransform(value, key) {
        return value;
    }

    customStatusHandler(transformedValue, key) {
        if (key === 'tmcst') {
            this.tmcStatusDisplay.setValue(transformedValue);
            this.tmcStatusDisplay.updateClass(transformedValue ? 'status-success' : 'status-error');
        }
        if (key === 'tmct') {
            this.tmcTempDisplay.setValue(transformedValue);
            const tempIdx = Math.max(0, Math.min(4, transformedValue));
            const className = tempIdx === 0 ? 'status-success' : (tempIdx < 3 ? 'status-warning' : 'status-error');
            this.tmcTempDisplay.updateClass(className);
        }
        if (key === 'sd') {
            this.stallStatusDisplay.setValue(transformedValue);
            this.stallStatusDisplay.displays.forEach(element => {
                element.style.fontWeight = transformedValue ? 'bold' : 'normal';
            });
        }
        if (key === 'sc') {
            this.stallCountDisplay.setValue(transformedValue);
        }
    }
}

// Current speed control binding
class CurrentSpeedControlBinding extends ControlBinding {
    constructor(currentSpeedDisplay, config = {}) {
        const defaults = {
            statusKeys: ['cs'],
            customStatusHandler: (statusUpdate, controls, config) => {
                if (statusUpdate.cs !== undefined) {
                    currentSpeedDisplay.setValue(statusUpdate.cs);
                }
            }
        };
        super({ ...defaults, ...config });

        currentSpeedDisplay.displayTransform = (value) => `${Number(value).toFixed(2)} rpm`;

        this.addControl(currentSpeedDisplay);
    }
}

// Timestamp control binding
class TimestampControlBinding extends ControlBinding {
    constructor(lastUpdateDisplay, config = {}) {
        const defaults = {
            statusKeys: [],
            customStatusHandler: () => {
                lastUpdateDisplay.setValue(new Date().toLocaleTimeString());
            }
        };
        super({ ...defaults, ...config });

        this.addControl(lastUpdateDisplay);
    }
}

// Speed control binding with preset buttons and fill indicator
class SpeedControlBinding extends ControlBinding {
    /**
     * @param {SliderControl} speedSlider
     * @param {DisplayControl} speedDisplay
     * @param {RadioGroupControl} presetButtons
     * @param {DisplayControl} speedValueDisplay
     */
    constructor(speedSlider, speedDisplay, presetButtons, speedValueDisplay) {
        super({
            statusKeys: ['sp', 'cs'],
            debounceTime: 150
        });

        // Set displayTransform for speed displays
        speedDisplay.displayTransform = (value) => `${Number(value).toFixed(2)} rpm`;
        speedDisplay.options.colorizer = (value) => {
            if (value === 0) return '#1f2937';
            if (value < 5) return '#10b981';
            if (value < 15) return '#3b82f6';
            return '#8b5cf6';
        };
        speedValueDisplay.displayTransform = (value) => `${Number(value).toFixed(2)} rpm`;

        this.speedSlider = speedSlider;
        this.speedDisplay = speedDisplay;
        this.presetButtons = presetButtons;
        this.speedValueDisplay = speedValueDisplay;

        this.addControl(speedSlider);
        this.addControl(speedDisplay);
        this.addControl(presetButtons);

        // Wire up event handlers
        this.speedSlider.onChange((value) => {
            this.speedValueDisplay.setValue(value);
            this.handleValueChange(value, 'ss');
        });

        this.presetButtons.onChange((value) => {
            const speed = parseFloat(value);
            this.speedSlider.setValue(speed);
            this.handleValueChange(speed, 'ss');
        });
    }

    displayTransform(value) {
        return Number(value).toFixed(1);
    }

    inputValueTransform(value) {
        return Math.max(0.1, Math.min(30.0, value));
    }

    customStatusHandler(transformedValue, key) {
        if (key === 'sp') {
            this.speedSlider.setValue(transformedValue);
            this.speedDisplay.setValue(transformedValue);
            this.speedValueDisplay.setValue(transformedValue);
            this.updatePresetButtonState(transformedValue);
        }
        if (key === 'cs') {
            if (this.speedSlider && this.speedSlider.slider) {
                const min = parseFloat(this.speedSlider.slider.min);
                const max = parseFloat(this.speedSlider.slider.max);
                const clampedValue = Math.max(min, Math.min(max, transformedValue));
                const percentage = ((clampedValue - min) / (max - min)) * 100;
                this.speedSlider.updateFillWidth(percentage);
            }
        }
    }

    // Override handleValueChange to update preset buttons when slider moves
    async handleValueChange(value, commandType) {
        // Update preset buttons immediately when slider moves
        this.updatePresetButtonState(value);

        // Call parent implementation
        return await super.handleValueChange(value, commandType);
    }

    // Update preset button active state based on current speed
    updatePresetButtonState(currentSpeed) {
        if (!this.presetButtons || !this.presetButtons.buttons) return;

        // Find the closest preset button to the current speed
        let closestButton = null;
        let closestDifference = Infinity;

        this.presetButtons.buttons.forEach(button => {
            if (button && button.dataset.value) {
                const presetSpeed = parseFloat(button.dataset.value);
                const difference = Math.abs(presetSpeed - currentSpeed);

                if (difference < closestDifference && difference < 0.1) { // Within 0.1 RPM tolerance
                    closestDifference = difference;
                    closestButton = button;
                }
            }
        });

        if (closestButton && closestButton.dataset.value !== undefined) {
            this.presetButtons.setValue(closestButton.dataset.value);
        }
    }
}

// Direction control binding with button coordination
class DirectionControlBinding extends ControlBinding {
    /**
     * @param {RadioGroupControl} directionButtons
     * @param {DisplayControl} directionDisplay
     */
    constructor(directionButtons, directionDisplay) {
        super({
            statusKeys: ['dir']
        });

        this.directionButtons = directionButtons;
        this.directionDisplay = directionDisplay;

        if (directionDisplay) {
            directionDisplay.options.colorizer = (value) => value === 'Clockwise' ? '#3b82f6' : '#8b5cf6';
        }

        this.addControl(directionButtons);
        this.addControl(directionDisplay);

        // Wire up event handler
        this.directionButtons.onChange((value) => {
            // value is either "cw" or "ccw"
            const clockwise = value === "cw";
            this.handleValueChange(clockwise, 'sd');
        });
    }

    customStatusHandler(transformedValue, key) {
        if (key === 'dir') {
            const clockwise = transformedValue === 'cw';
            this.directionButtons.setValue(clockwise ? "cw" : "ccw");
            this.directionDisplay.setValue(clockwise ? 'Clockwise' : 'Counter-clockwise');
        }
    }

    async setDirection(clockwise) {
        return await this.handleValueChange(clockwise);
    }
}

// Variable speed control binding with UI coordination
class VariableSpeedControlBinding extends ControlBinding {
    constructor(toggle, statusDisplay, controlsContainer) {
        super({ statusKeys: ['sve'] });
        this.addControl(toggle);
        this.addControl(statusDisplay);
        this.toggle = toggle;
        this.statusDisplay = statusDisplay;
        this.controlsContainer = controlsContainer;

        toggle.onChange((enabled) => {
            this.handleValueChange(enabled, enabled ? 'esv' : 'dsv');
        });
    }
    customStatusHandler(transformedValue, key) {
        if (key === 'sve') {
            const enabled = transformedValue;
            this.toggle.setValue(enabled);
            this.updateVariableSpeedUI(enabled);
            this.statusDisplay.setValue(enabled ? 'ON' : 'OFF');
            const color = enabled ? '#10b981' : '#1f2937';
            this.statusDisplay.displays.forEach(element => {
                if (element) element.style.color = color;
            });
        }
    }
    async handleValueChange(enabled) {
        const commandType = enabled ? 'esv' : 'dsv';
        this.toggle.setValue(enabled);
        this.updateVariableSpeedUI(enabled);
        this.controls.forEach(control => {
            control.setDisplayState(CONTROL_STATES.OUTDATED);
        });
        return await this.commandManager.sendCommand(commandType, true);
    }
    updateVariableSpeedUI(enabled) {
        if (this.controlsContainer) {
            if (enabled) {
                this.controlsContainer.classList.remove('disabled');
            } else {
                this.controlsContainer.classList.add('disabled');
            }
        }
    }
}

/**
 * VariableSpeedGraphControlBinding for plotting speed vs. rotation remainder
 */
class VariableSpeedGraphControlBinding extends ControlBinding {
    /**
     * @param {GraphControl} graphControl
     */
    constructor(graphControl) {
        super({
            statusKeys: ['ca', 'cs', 'sp']
        });

        this.graphControl = graphControl;

        this.addControl(graphControl);

        // Buffer for last sample values
        this.lastCa = null;
        this.lastCs = null;
        this.lastSp = null;
    }

    statusValueTransform(value, key) {
        if (key === 'svs') return Math.round(value * 100);
        if (key === 'svp') {
            let phaseDegrees = Math.round((value * 180) / Math.PI);
            if (phaseDegrees > 180) {
                phaseDegrees -= 360;
            }
            return phaseDegrees;
        }
        return value;
    }

    customStatusHandler(transformedValue, key) {
        // Store latest value for each key
        if (key === 'ca') this.lastCa = transformedValue;
        if (key === 'cs') this.lastCs = transformedValue;
        if (key === 'sp') this.lastSp = transformedValue;

        if (this.lastCa !== null && this.lastCs !== null && this.lastSp !== null) {
            this.graphControl.addSample(this.lastCa, this.lastCs, this.lastSp);
        }
    }
}

// Power delivery control binding with complex state management
class PowerDeliveryControlBinding extends ControlBinding {
    constructor(voltageSelect, negotiateBtn, autoNegotiateBtn, pdStatusDisplay, pdPowerGoodDisplay, pdNegotiatedVoltageDisplay, pdCurrentVoltageDisplay) {
        super({
            statusKeys: ['pdns', 'pdpg', 'pdnv', 'pdcv']
        });

        this.voltageSelect = voltageSelect;
        this.negotiateBtn = negotiateBtn;
        this.autoNegotiateBtn = autoNegotiateBtn;
        this.pdStatusDisplay = pdStatusDisplay;
        this.pdPowerGoodDisplay = pdPowerGoodDisplay;
        this.pdNegotiatedVoltageDisplay = pdNegotiatedVoltageDisplay;
        this.pdCurrentVoltageDisplay = pdCurrentVoltageDisplay;
        this.negotiationTimeout = null;

        this.addControl(voltageSelect);
        this.addControl(negotiateBtn);
        this.addControl(autoNegotiateBtn);
        this.addControl(pdStatusDisplay);
        this.addControl(pdPowerGoodDisplay);
        this.addControl(pdNegotiatedVoltageDisplay);
        this.addControl(pdCurrentVoltageDisplay);

        // Wire up event handlers
        this.negotiateBtn.onChange(() => {
            this.handleValueChange(parseInt(this.voltageSelect.getValue()), 'stv');
        });

        this.autoNegotiateBtn.onChange(() => {
            this.handleValueChange(1, 'anh');
        });

        // State mapping for negotiation status
        this.negotiationStates = {
            0: { text: 'Idle', class: 'status-unknown' },
            1: { text: 'Negotiating...', class: 'status-warning' },
            2: { text: 'Success', class: 'status-success' },
            3: { text: 'Failed (No PD Adapter)', class: 'status-error' },
            4: { text: 'Auto-Negotiating...', class: 'status-warning' }
        };
    }

    customStatusHandler(transformedValue, key) {
        if (key === 'pdns') {
            const statusValue = Math.round(transformedValue);
            const status = this.negotiationStates[statusValue] ||
                { text: `Unknown (${transformedValue})`, class: 'status-error' };

            if (this.pdStatusDisplay && this.pdStatusDisplay.displays && this.pdStatusDisplay.displays[0]) {
                this.pdStatusDisplay.displays[0].textContent = status.text;
                this.pdStatusDisplay.displays[0].className = `power-value ${status.class}`;
                if (statusValue === 1 || statusValue === 4) {
                    this.pdStatusDisplay.displays[0].classList.add('negotiating');
                }
                this.pdStatusDisplay.displays[0].style.opacity = '1.0';
            }

            // Reset buttons when negotiation is complete
            if (statusValue !== 1 && statusValue !== 4) {
                this.resetNegotiateButtons();
            }

            // Update voltage selector on success
            if (statusValue === 2 && this.voltageSelect && this.voltageSelect.setValue) {
                this.voltageSelect.setValue(transformedValue);
            }
        }

        if (key === 'pdpg') {
            if (this.pdPowerGoodDisplay && this.pdPowerGoodDisplay.displays && this.pdPowerGoodDisplay.displays[0]) {
                this.pdPowerGoodDisplay.displays[0].textContent = transformedValue ? 'Good' : 'Bad';
                this.pdPowerGoodDisplay.displays[0].className = transformedValue ?
                    'power-value status-success' : 'power-value status-error';
                this.pdPowerGoodDisplay.displays[0].style.opacity = '1.0';
            }
        }

        if (key === 'pdnv') {
            if (this.pdNegotiatedVoltageDisplay && this.pdNegotiatedVoltageDisplay.displays && this.pdNegotiatedVoltageDisplay.displays[0]) {
                this.pdNegotiatedVoltageDisplay.displays[0].textContent = transformedValue > 0 ?
                    `${transformedValue} V` : '-';
                this.pdNegotiatedVoltageDisplay.displays[0].style.opacity = '1.0';
            }
        }

        if (key === 'pdcv') {
            if (this.pdCurrentVoltageDisplay && this.pdCurrentVoltageDisplay.displays && this.pdCurrentVoltageDisplay.displays[0]) {
                this.pdCurrentVoltageDisplay.displays[0].textContent = `${transformedValue.toFixed(1)} V`;
                this.pdCurrentVoltageDisplay.displays[0].style.opacity = '1.0';
            }
        }
    }

    async handleValueChange(action) {
        if (action && action.type === 'negotiate') {
            const selectedVoltage = parseInt(this.voltageSelect.getValue());
            if (selectedVoltage && this.commandManager) {
                this.showNegotiationStarted(false);
                return await this.commandManager.sendCommand('stv', selectedVoltage);
            }
            return false;
        }
        if (action && action.type === 'autoNegotiate') {
            if (this.commandManager) {
                this.showNegotiationStarted(true);
                return await this.commandManager.sendCommand('anh', 1);
            }
            return false;
        }
        // fallback to base implementation if needed
        return super.handleValueChange(action);
    }

    showNegotiationStarted(isAutoNegotiation = false) {
        // Update status display
        if (this.pdStatusDisplay && this.pdStatusDisplay.displays && this.pdStatusDisplay.displays[0]) {
            this.pdStatusDisplay.displays[0].textContent = isAutoNegotiation ? 'Auto-Negotiating...' : 'Negotiating...';
            this.pdStatusDisplay.displays[0].className = 'power-value status-warning negotiating';
            this.pdStatusDisplay.displays[0].style.opacity = '1.0';
        }

        // Disable buttons temporarily
        this.negotiateBtn.setDisplayState(CONTROL_STATES.RETRY);
        this.autoNegotiateBtn.setDisplayState(CONTROL_STATES.RETRY);

        // Set timeout fallback
        if (this.negotiationTimeout) {
            clearTimeout(this.negotiationTimeout);
        }
        this.negotiationTimeout = setTimeout(() => {
            this.resetNegotiateButtons();
        }, 15000);
    }

    resetNegotiateButtons() {
        this.negotiateBtn.setDisplayState(CONTROL_STATES.VALID);
        this.autoNegotiateBtn.setDisplayState(CONTROL_STATES.VALID);

        if (this.negotiationTimeout) {
            clearTimeout(this.negotiationTimeout);
            this.negotiationTimeout = null;
        }
    }

}

// StallGuard control binding with load visualization
/**
 * StallGuardControlBinding
 * Implements ControlBinding for StallGuard threshold (sgt) and result (sgr).
 * - Uses new ControlBinding constructor: super(commandType, statusKeys)
 * - Registers controls via addControl
 * - Implements transformation functions as class methods
 */
class StallGuardControlBinding extends ControlBinding {
    constructor(thresholdSlider, resultDisplay, thresholdValueDisplay) {
        super({ statusKeys: ['sgt', 'sgr'], debounceTime: 150 });
        this.thresholdSlider = thresholdSlider;
        this.resultDisplay = resultDisplay;
        this.thresholdValueDisplay = thresholdValueDisplay;
        this.addControl(thresholdSlider);
        this.addControl(resultDisplay);
        if (thresholdValueDisplay) this.addControl(thresholdValueDisplay);

        // Wire up event handler
        this.thresholdSlider.onChange((value) => {
            this.thresholdValueDisplay.setValue(value);
            this.handleValueChange(value, 'st');
        });
    }

    customStatusHandler(transformedValue, key) {
        if (key === 'sgt') {
            this.thresholdSlider.setValue(transformedValue);
            if (this.thresholdValueDisplay) this.thresholdValueDisplay.setValue(transformedValue.toFixed(1));
        }
        if (key === 'sgr') {
            this.resultDisplay.setValue(this.displayTransform(transformedValue, 'sgr'));
            if (this.thresholdSlider.fillElement) {
                this.thresholdSlider.updateFillWidth(transformedValue);
                this.thresholdSlider.fillElement.style.opacity = "1.0";
            }
            const sliderPercent = parseFloat(this.thresholdSlider.slider.value);
            if (this.thresholdSlider.fillElement) {
                if (transformedValue < sliderPercent * 0.8) {
                    this.thresholdSlider.setFillColor('#10b981');
                } else if (transformedValue < sliderPercent) {
                    this.thresholdSlider.setFillColor('#f59e0b');
                } else {
                    this.thresholdSlider.setFillColor('#ef4444');
                }
            }
        }
    }

    statusValueTransform(value, key) {
        if (key === 'sgt') {
            // Invert threshold for UI (float)
            return 100 - ((value / 255) * 100);
        }
        if (key === 'sgr') {
            // Load percentage (float)
            return ((510 - value) / 510) * 100;
        }
        return value;
    }

    displayTransform(value, key) {
        if (key === 'sgt' || key === 'sgr') {
            return `${value.toFixed(1)} %`;
        }
        return value.toString();
    }

    inputValueTransform(percent) {
        // Invert percent for backend value (0-100 becomes 100-0)
        return Math.round((100 - percent) * 2.55);
    }

    customStatusHandler(transformedValue, key) {
        if (key === 'sgt') {
            this.thresholdSlider.setValue(transformedValue);
            if (this.thresholdValueDisplay) this.thresholdValueDisplay.setValue(transformedValue.toFixed(1));
        }
        if (key === 'sgr') {
            this.resultDisplay.setValue(this.displayTransform(transformedValue, 'sgr'));
            if (this.thresholdSlider.fillElement) {
                this.thresholdSlider.updateFillWidth(transformedValue);
                this.thresholdSlider.fillElement.style.opacity = "1.0";
            }
            // Color fill based on stall threshold
            const sliderPercent = parseFloat(this.thresholdSlider.slider.value); // invert slider
            if (this.thresholdSlider.fillElement) {
                if (transformedValue < sliderPercent * 0.8) {
                    this.thresholdSlider.setFillColor('#10b981');
                } else if (transformedValue < sliderPercent) {
                    this.thresholdSlider.setFillColor('#f59e0b');
                } else {
                    this.thresholdSlider.setFillColor('#ef4444');
                }
            }
        }
    }

    hideFill() {
        if (this.thresholdSlider && this.thresholdSlider.fillElement) {
            this.thresholdSlider.hideFill();
        }
    }
}

/**
 * Emergency stop control binding
 */
class EmergencyStopControlBinding extends ControlBinding {
    /**
     * @param {SingleButtonControl} emergencyStopBtn
     * @param {SliderControl} speedSlider
     * @param {Object} config
     */
    constructor(emergencyStopBtn, speedSlider = null, config = {}) {
        super({
            ...config
        });
        this.emergencyStopBtn = emergencyStopBtn;
        this.speedSlider = speedSlider;
        this.addControl(emergencyStopBtn);

        // Wire up event handler
        this.emergencyStopBtn.onChange(() => {
            this.handleValueChange(true, 'es');
        });
    }

    async handleValueChange() {
        // Hide speed fill if available
        if (this.speedSlider && typeof this.speedSlider.hideFill === 'function') {
            this.speedSlider.hideFill();
        }
        // Optionally: send emergency stop command
        if (this.commandManager) {
            await this.commandManager.sendCommand('es', true);
        }
        // Visual feedback
        if (this.emergencyStopBtn && this.emergencyStopBtn.elements && this.emergencyStopBtn.elements[0]) {
            this.emergencyStopBtn.elements[0].textContent = '🛑 STOPPED';
            this.emergencyStopBtn.elements[0].style.background = '#dc2626';
            setTimeout(() => {
                this.emergencyStopBtn.elements[0].textContent = '🛑 Emergency Stop';
                this.emergencyStopBtn.elements[0].style.background = '#ef4444';
            }, 2000);
        }
        return true;
    }
}

/**
 * Statistics reset control binding
 */
class StatisticsResetControlBinding extends ControlBinding {
    /**
     * @param {SingleButtonControl} resetStatsBtn
     * @param {Object} config
     */
    constructor(resetStatsBtn, config = {}) {
        super({
            ...config
        });
        this.resetStatsBtn = resetStatsBtn;
        this.addControl(resetStatsBtn);

        // Wire up event handler
        this.resetStatsBtn.onChange(() => {
            this.handleValueChange(true, 'rc');
        });
    }

    async handleValueChange() {
        if (this.commandManager) {
            const success = await this.commandManager.sendCommand('rc', true);
            if (success && this.resetStatsBtn && this.resetStatsBtn.elements && this.resetStatsBtn.elements[0]) {
                this.resetStatsBtn.elements[0].textContent = '📊 Reset Successful';
                setTimeout(() => {
                    this.resetStatsBtn.elements[0].textContent = '📊 Reset Statistics';
                }, 2000);
            }
            return success;
        }
        return false;
    }
}

/**
 * Stall reset control binding
 */
class StallResetControlBinding extends ControlBinding {
    /**
     * @param {SingleButtonControl} resetStallBtn
     * @param {Object} config
     */
    constructor(resetStallBtn, config = {}) {
        super({
            ...config
        });
        this.resetStallBtn = resetStallBtn;
        this.addControl(resetStallBtn);

        // Wire up event handler
        this.resetStallBtn.onChange(() => {
            this.handleValueChange(true, 'rs');
        });
    }

    async handleValueChange() {
        if (this.commandManager) {
            const success = await this.commandManager.sendCommand('rs', true);
            if (success && this.resetStallBtn && this.resetStallBtn.elements && this.resetStallBtn.elements[0]) {
                this.resetStallBtn.elements[0].textContent = '⚠️ Reset Successful';
                setTimeout(() => {
                    this.resetStallBtn.elements[0].textContent = '⚠️ Reset Stall Count';
                }, 2000);
            }
            return success;
        }
        return false;
    }
}

/**
 * Motor control binding
 */
class MotorControlBinding extends ControlBinding {
    constructor(motorToggle, motorStatusDisplay) {
        super({
            statusKeys: ['en'],
            debounceTime: 0
        });
        this.addControl(motorToggle);
        this.addControl(motorStatusDisplay);

        // Wire up event handler
        motorToggle.onChange((enabled) => {
            this.handleValueChange(enabled, 'en');
        });
    }
}

/**
 * Current control binding
 */
class CurrentControlBinding extends ControlBinding {
    constructor(currentSlider, currentDisplay, currentValueDisplay) {
        super({
            statusKeys: ['cur'],
            inputValueTransform: (value) => parseInt(value),
            debounceTime: 150
        });

        // Set displayTransform for current displays
        currentDisplay.displayTransform = (value) => `${Number(value)} %`;
        currentDisplay.options.colorizer = (value) => {
            if (value <= 20) return '#10b981';
            if (value <= 50) return '#3b82f6';
            if (value <= 80) return '#f59e0b';
            return '#8b5cf6';
        };
        currentValueDisplay.displayTransform = (value) => `${Number(value).toFixed(1)} %`;

        this.currentSlider = currentSlider;
        this.currentDisplay = currentDisplay;
        this.currentValueDisplay = currentValueDisplay;
        this.addControl(currentSlider);
        this.addControl(currentDisplay);
        if (currentValueDisplay) this.addControl(currentValueDisplay);

        // Wire up event handler
        this.currentSlider.onChange((value) => {
            this.currentValueDisplay.setValue(value);
            this.handleValueChange(value, 'sc');
        });
    }

    customStatusHandler(transformedValue, key) {
        if (key === 'cur') {
            this.currentSlider.setValue(transformedValue);
            this.currentDisplay.setValue(transformedValue);
            if (this.currentValueDisplay) this.currentValueDisplay.setValue(transformedValue);
        }
    }
}

/**
 * Strength control binding
 */
class StrengthControlBinding extends ControlBinding {
    constructor(strengthSlider, strengthValueDisplay, variableSpeedActive = false) {
        super({ statusKeys: ['svs'], debounceTime: 150 });
        if (!variableSpeedActive) {
            this.addControl(strengthSlider);
            if (strengthValueDisplay) this.addControl(strengthValueDisplay);
        }

        if (strengthValueDisplay) {
            strengthValueDisplay.displayTransform = (value) => `${Number(value).toFixed(1)} %`;
        }

        if (!variableSpeedActive) {
            strengthSlider.onChange((value) => {
                strengthValueDisplay.setValue(value);
                this.handleValueChange(value, 'sv');
            });
        }
    }
    inputValueTransform(value) { return parseInt(value) / 100.0; }
    statusValueTransform(value) { return Math.round(value * 100); }
    // No customStatusHandler needed; base class implementation suffices.
}

/**
 * Phase control binding
 */
class PhaseControlBinding extends ControlBinding {
    constructor(phaseSlider, phaseValueDisplay, variableSpeedActive = false) {
        super({ statusKeys: ['svp'], debounceTime: 150 });
        if (!variableSpeedActive) {
            this.addControl(phaseSlider);
            if (phaseValueDisplay) this.addControl(phaseValueDisplay);
        }

        if (phaseValueDisplay) {
            phaseValueDisplay.displayTransform = (value) => `${Number(value).toFixed(1)}°`;
        }

        if (!variableSpeedActive) {
            phaseSlider.onChange((value) => {
                phaseValueDisplay.setValue(value);
                this.handleValueChange(value, 'svp');
            });
        }
    }
    inputValueTransform(value) {
        let phase = parseInt(value);
        if (phase < 0) phase += 360;
        return (phase * Math.PI) / 180;
    }
    statusValueTransform(value) {
        let phaseDegrees = Math.round((value * 180) / Math.PI);
        if (phaseDegrees > 180) phaseDegrees -= 360;
        return phaseDegrees;
    }
    // No customStatusHandler needed; base class implementation suffices.
    // No customStatusHandler needed; base class implementation suffices.
}
