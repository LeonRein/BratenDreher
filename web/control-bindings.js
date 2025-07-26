/**
 * Control Bindings - Maps UI events to commands and status updates to UI updates
 * Provides configuration-driven binding between controls and command manager
 */
class ControlBinding {
    constructor({
        commandType = null,
        statusKeys = [],
        additionalParams = {}
    } = {}) {
        this.commandType = commandType;
        this.statusKeys = statusKeys;
        this.additionalParams = additionalParams;

        this.controls = [];
        this.commandManager = null;
    }

    inputValueTransform(value) {
        return value;
    }

    statusValueTransform(value, key) {
        return value;
    }

    displayTransform(value) {
        return value.toString();
    }

    customStatusHandler(statusUpdate, controls) {
        // Default: no custom handling
    }

    setCommandManager(commandManager) {
        this.commandManager = commandManager;
    }

    addControl(control) {
        this.controls.push(control);
    }

    // Handle value changes from UI controls
    async handleValueChange(value) {
        if (!this.commandManager || !this.commandType) {
            console.warn('ControlBinding: No command manager or command type configured');
            return false;
        }

        // Set all controls to outdated state
        this.controls.forEach(control => {
            control.setDisplayState(CONTROL_STATES.OUTDATED);
        });

        // Transform value and send command
        const transformedValue = this.inputValueTransform(value);
        const success = await this.commandManager.sendCommand(
            this.commandType, 
            transformedValue, 
            this.additionalParams || {}
        );

        if (success) {
            // Set controls to retry state briefly to show command was sent
            this.controls.forEach(control => {
                control.setDisplayState(CONTROL_STATES.RETRY);
            });
        }

        return success;
    }

    // Handle status updates from the backend
    handleStatusUpdate(statusUpdate) {
        // Check if this status update is relevant to this binding
        const relevantKeys = this.statusKeys.filter(key => 
            statusUpdate[key] !== undefined
        );

        if (relevantKeys.length === 0) {
            return; // No relevant status updates
        }

        // Set all controls to valid state since we received data
        this.controls.forEach(control => {
            control.setDisplayState(CONTROL_STATES.VALID);
        });

        // Use custom handler if overridden
        if (this.customStatusHandler !== ControlBinding.prototype.customStatusHandler) {
            this.customStatusHandler(statusUpdate, this.controls);
            return;
        }

        // Default handling for simple cases
        relevantKeys.forEach(key => {
            const value = statusUpdate[key];
            const transformedValue = this.statusValueTransform(value, key);
            
            // Update controls based on their type
            this.controls.forEach(control => {
                this.updateControlFromStatus(control, key, transformedValue);
            });
        });
    }

    updateControlFromStatus(control, statusKey, value) {
        if (control instanceof SliderControl) {
            control.setValue(value);
        } else if (control instanceof ToggleControl) {
            control.setValue(value);
        } else if (control instanceof SelectControl) {
            control.setValue(value);
        } else if (control instanceof DisplayControl) {
            control.updateValue(value);
        } else if (control instanceof ButtonControl) {
            // Handle button states based on value
            if (typeof value === 'boolean') {
                control.setActiveButton(value ? 0 : 1);
            } else {
                control.setActiveByValue(value);
            }
        }
    }
}

/**
 * Specialized bindings for complex controls
 */

class AccelerationControlBinding extends ControlBinding {
    constructor(accelerationSlider, accelerationDisplay) {
        super({
            commandType: 'sa',
            statusKeys: ['acc']
        });

        this.accelerationSlider = accelerationSlider;
        this.accelerationDisplay = accelerationDisplay;

        this.addControl(accelerationSlider);
        this.addControl(accelerationDisplay);
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
}

class StatisticsControlBinding extends ControlBinding {
    constructor(totalRevolutionsDisplay, runTimeDisplay, avgSpeedDisplay, updateAverageSpeed) {
        super({
            statusKeys: ['tr', 'rt']
        });

        this.totalRevolutionsDisplay = totalRevolutionsDisplay;
        this.runTimeDisplay = runTimeDisplay;
        this.avgSpeedDisplay = avgSpeedDisplay;
        this.updateAverageSpeed = updateAverageSpeed;

        this.addControl(totalRevolutionsDisplay);
        this.addControl(runTimeDisplay);
        this.addControl(avgSpeedDisplay);
    }

    statusValueTransform(value) {
        return value;
    }

    customStatusHandler(statusUpdate, controls) {
        if (statusUpdate.tr !== undefined) {
            this.totalRevolutionsDisplay.updateValue(this.statusValueTransform(statusUpdate.tr));
        }
        if (statusUpdate.rt !== undefined) {
            this.runTimeDisplay.updateValue(this.statusValueTransform(statusUpdate.rt));
            if (typeof this.updateAverageSpeed === 'function') {
                this.updateAverageSpeed();
            }
        }
    }
}

class TmcStatusControlBinding extends ControlBinding {
    constructor(tmcStatusDisplay, tmcTempDisplay, stallStatusDisplay, stallCountDisplay) {
        super({
            statusKeys: ['tmcst', 'tmct', 'sd', 'sc']
        });

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
        if (key === 'tmcst') return value ? 'OK' : 'Error';
        if (key === 'tmct') {
            const tempLabels = ['Normal', 'Warm (>120°C)', 'Elevated (>143°C)', 'High (>150°C)', 'Critical (>157°C)'];
            const tempIdx = Math.max(0, Math.min(4, value));
            return { label: tempLabels[tempIdx], className: tempIdx === 0 ? 'status-success' : (tempIdx < 3 ? 'status-warning' : 'status-error') };
        }
        if (key === 'sd') return value ? 'STALL!' : 'OK';
        if (key === 'sc') return value;
        return value;
    }

    customStatusHandler(statusUpdate, controls) {
        if (statusUpdate.tmcst !== undefined) {
            const transformed = this.statusValueTransform(statusUpdate.tmcst, 'tmcst');
            this.tmcStatusDisplay.updateValue(transformed);
            this.tmcStatusDisplay.updateClass(transformed === 'OK' ? 'status-success' : 'status-error');
        }

        if (statusUpdate.tmct !== undefined) {
            const transformed = this.statusValueTransform(statusUpdate.tmct, 'tmct');
            this.tmcTempDisplay.updateValue(transformed.label);
            this.tmcTempDisplay.updateClass(transformed.className);
        }

        if (statusUpdate.sd !== undefined) {
            const transformed = this.statusValueTransform(statusUpdate.sd, 'sd');
            this.stallStatusDisplay.updateValue(transformed);
            const color = statusUpdate.sd ? '#e74c3c' : '#10b981';
            this.stallStatusDisplay.displays.forEach(element => {
                if (element) element.style.color = color;
                element.style.fontWeight = statusUpdate.sd ? 'bold' : 'normal';
            });
        }

        if (statusUpdate.sc !== undefined) {
            const transformed = this.statusValueTransform(statusUpdate.sc, 'sc');
            this.stallCountDisplay.updateValue(transformed);
            const color = statusUpdate.sc > 0 ? '#e74c3c' : '#10b981';
            this.stallCountDisplay.displays.forEach(element => {
                if (element) element.style.color = color;
            });
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
                    currentSpeedDisplay.updateValue(statusUpdate.cs);
                }
            }
        };
        super({ ...defaults, ...config });

        this.addControl(currentSpeedDisplay);
    }
}

// Timestamp control binding
class TimestampControlBinding extends ControlBinding {
    constructor(lastUpdateDisplay, config = {}) {
        const defaults = {
            statusKeys: [],
            customStatusHandler: () => {
                lastUpdateDisplay.updateValue(new Date().toLocaleTimeString());
            }
        };
        super({ ...defaults, ...config });

        this.addControl(lastUpdateDisplay);
    }
}

// Speed control binding with preset buttons and fill indicator
class SpeedControlBinding extends ControlBinding {
    constructor(speedSlider, speedDisplay, presetButtons) {
        super({
            commandType: 'ss',
            statusKeys: ['sp', 'cs']
        });

        this.speedSlider = speedSlider;
        this.speedDisplay = speedDisplay;
        this.presetButtons = presetButtons;

        this.addControl(speedSlider);
        this.addControl(speedDisplay);
        this.addControl(presetButtons);
    }

    displayTransform(value) {
        return value.toFixed(1);
    }

    inputValueTransform(value) {
        return Math.max(0.1, Math.min(30.0, value));
    }

    customStatusHandler(statusUpdate, controls) {
        if (statusUpdate.sp !== undefined) {
            // Update setpoint speed
            const speed = statusUpdate.sp;
            this.speedSlider.setValue(speed);
            this.speedDisplay.updateValue(speed);

            // Update preset button active state
            this.updatePresetButtonState(speed);
        }

        if (statusUpdate.cs !== undefined) {
            // Update fill indicator to show current speed
            if (this.speedSlider && this.speedSlider.slider) {
                const min = parseFloat(this.speedSlider.slider.min);
                const max = parseFloat(this.speedSlider.slider.max);
                const clampedValue = Math.max(min, Math.min(max, statusUpdate.cs));
                const percentage = ((clampedValue - min) / (max - min)) * 100;
                this.speedSlider.updateFillWidth(percentage);
            }
        }
    }

    // Override handleValueChange to update preset buttons when slider moves
    async handleValueChange(value) {
        // Update preset buttons immediately when slider moves
        this.updatePresetButtonState(value);
        
        // Call parent implementation
        return await super.handleValueChange(value);
    }

    // Update preset button active state based on current speed
    updatePresetButtonState(currentSpeed) {
        if (!this.presetButtons || !this.presetButtons.buttons) return;
        
        // Find the closest preset button to the current speed
        let closestButton = null;
        let closestDifference = Infinity;
        
        this.presetButtons.buttons.forEach(button => {
            if (button && button.dataset.speed) {
                const presetSpeed = parseFloat(button.dataset.speed);
                const difference = Math.abs(presetSpeed - currentSpeed);
                
                if (difference < closestDifference && difference < 0.1) { // Within 0.1 RPM tolerance
                    closestDifference = difference;
                    closestButton = button;
                }
            }
        });
        
        // Update button states
        this.presetButtons.buttons.forEach(button => {
            if (button) {
                if (button === closestButton) {
                    button.classList.add(this.presetButtons.options.activeClass);
                } else {
                    button.classList.remove(this.presetButtons.options.activeClass);
                }
            }
        });
    }
}

// Direction control binding with button coordination
class DirectionControlBinding extends ControlBinding {
    constructor(directionButtons, directionDisplay) {
        super({
            commandType: 'sd',
            statusKeys: ['dir']
        });

        this.directionButtons = directionButtons;
        this.directionDisplay = directionDisplay;

        this.addControl(directionButtons);
        this.addControl(directionDisplay);
    }

    customStatusHandler(statusUpdate, controls) {
        if (statusUpdate.dir !== undefined) {
            const clockwise = statusUpdate.dir === 'cw';

            // Update button states - index 0 is clockwise, index 1 is counterclockwise
            this.directionButtons.setActiveButton(clockwise ? 0 : 1);

            // Update direction display
            this.directionDisplay.updateValue(clockwise ? 'Clockwise' : 'Counter-clockwise');

            // Apply color coding
            const color = clockwise ? '#3b82f6' : '#8b5cf6';
            this.directionDisplay.displays.forEach(element => {
                if (element) element.style.color = color;
            });
        }
    }

    async setDirection(clockwise) {
        return await this.handleValueChange(clockwise);
    }
}

// Variable speed control binding with UI coordination
class VariableSpeedControlBinding extends ControlBinding {
    constructor(toggle, strengthSlider, phaseSlider, statusDisplay, controlsContainer) {
        super({
            commandType: null, // Multiple command types
            statusKeys: ['sve', 'svs', 'svp']
        });

        this.toggle = toggle;
        this.strengthSlider = strengthSlider;
        this.phaseSlider = phaseSlider;
        this.statusDisplay = statusDisplay;
        this.controlsContainer = controlsContainer;

        this.addControl(toggle);
        this.addControl(strengthSlider);
        this.addControl(phaseSlider);
        this.addControl(statusDisplay);
    }

    customStatusHandler(statusUpdate, controls) {
        if (statusUpdate.sve !== undefined) {
            const enabled = statusUpdate.sve;
            this.toggle.setValue(enabled);
            this.updateVariableSpeedUI(enabled);
            this.statusDisplay.updateValue(enabled ? 'ON' : 'OFF');

            // Color coding
            const color = enabled ? '#10b981' : '#1f2937';
            this.statusDisplay.displays.forEach(element => {
                if (element) element.style.color = color;
            });
        }

        if (statusUpdate.svs !== undefined) {
            const strength = this.statusValueTransform(statusUpdate.svs, 'svs');
            this.strengthSlider.setValue(strength);
        }

        if (statusUpdate.svp !== undefined) {
            const phaseDegrees = this.statusValueTransform(statusUpdate.svp, 'svp');
            this.phaseSlider.setValue(phaseDegrees);
        }
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

    async setVariableSpeedEnabled(enabled) {
        const commandType = enabled ? 'esv' : 'dsv';
        
        // Update UI immediately
        this.toggle.setValue(enabled);
        this.updateVariableSpeedUI(enabled);
        
        // Set controls to outdated state
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

    customStatusHandler(statusUpdate, controls) {
        // Get total rotations, current speed, set speed
        this.lastCa = statusUpdate.ca !== undefined ? statusUpdate.ca : this.lastCa;
        this.lastCs = statusUpdate.cs !== undefined ? statusUpdate.cs : this.lastCs;
        this.lastSp = statusUpdate.sp !== undefined ? statusUpdate.sp : this.lastSp;

        if (this.lastCa !== null && this.lastCs !== null && this.lastSp !== null) {
            this.graphControl.addSample(this.lastCa, this.lastCs, this.lastSp);
        }
    }
}

// Power delivery control binding with complex state management
class PowerDeliveryControlBinding extends ControlBinding {
    constructor(voltageSelect, negotiateBtn, autoNegotiateBtn, pdStatusDisplay, pdPowerGoodDisplay, pdNegotiatedVoltageDisplay, pdCurrentVoltageDisplay) {
        super({
            commandType: null, // Multiple command types
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

        // State mapping for negotiation status
        this.negotiationStates = {
            0: { text: 'Idle', class: 'status-unknown' },
            1: { text: 'Negotiating...', class: 'status-warning' },
            2: { text: 'Success', class: 'status-success' },
            3: { text: 'Failed (No PD Adapter)', class: 'status-error' },
            4: { text: 'Auto-Negotiating...', class: 'status-warning' }
        };
    }

    customStatusHandler(statusUpdate, controls) {
        this.handlePowerDeliveryStatus(statusUpdate);
    }

    async negotiateVoltage() {
        const selectedVoltage = parseInt(this.voltageSelect.getValue());
        if (selectedVoltage && this.commandManager) {
            this.showNegotiationStarted(false);
            return await this.commandManager.sendCommand('stv', selectedVoltage);
        }
        return false;
    }

    async autoNegotiate() {
        if (this.commandManager) {
            this.showNegotiationStarted(true);
            return await this.commandManager.sendCommand('anh', 1);
        }
        return false;
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

    handlePowerDeliveryStatus(statusUpdate) {
        if (statusUpdate.pdns !== undefined) {
            const statusValue = Math.round(statusUpdate.pdns);
            const status = this.negotiationStates[statusValue] ||
                          { text: `Unknown (${statusUpdate.pdns})`, class: 'status-error' };

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
            if (statusValue === 2 && statusUpdate.pdnv > 0) {
                this.voltageSelect.setValue(statusUpdate.pdnv);
            }
        }

        if (statusUpdate.pdpg !== undefined) {
            if (this.pdPowerGoodDisplay && this.pdPowerGoodDisplay.displays && this.pdPowerGoodDisplay.displays[0]) {
                this.pdPowerGoodDisplay.displays[0].textContent = statusUpdate.pdpg ? 'Good' : 'Bad';
                this.pdPowerGoodDisplay.displays[0].className = statusUpdate.pdpg ?
                    'power-value status-success' : 'power-value status-error';
                this.pdPowerGoodDisplay.displays[0].style.opacity = '1.0';
            }
        }

        if (statusUpdate.pdnv !== undefined) {
            if (this.pdNegotiatedVoltageDisplay && this.pdNegotiatedVoltageDisplay.displays && this.pdNegotiatedVoltageDisplay.displays[0]) {
                this.pdNegotiatedVoltageDisplay.displays[0].textContent = statusUpdate.pdnv > 0 ?
                    `${statusUpdate.pdnv}V` : '- V';
                this.pdNegotiatedVoltageDisplay.displays[0].style.opacity = '1.0';
            }
        }

        if (statusUpdate.pdcv !== undefined) {
            if (this.pdCurrentVoltageDisplay && this.pdCurrentVoltageDisplay.displays && this.pdCurrentVoltageDisplay.displays[0]) {
                this.pdCurrentVoltageDisplay.displays[0].textContent = `${statusUpdate.pdcv.toFixed(1)}V`;
                this.pdCurrentVoltageDisplay.displays[0].style.opacity = '1.0';
            }
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
    constructor(thresholdSlider, resultDisplay) {
        super({ commandType: 'st', statusKeys: ['sgt', 'sgr'] });
        this.thresholdSlider = thresholdSlider;
        this.resultDisplay = resultDisplay;
        this.addControl(thresholdSlider);
        this.addControl(resultDisplay);
    }

    statusValueTransform(value, key) {
        if (key === 'sgt') {
            // Threshold as percentage
            return ((value / 255) * 100).toFixed(1);
        }
        if (key === 'sgr') {
            // Load percentage
            return (((510 - value) / 510) * 100).toFixed(1);
        }
        return value;
    }

    inputValueTransform(percent) {
        // Convert percent (0-100) to backend value (0-255)
        return Math.round(percent * 2.55);
    }

    handleStatusUpdate(statusUpdate) {
        // Use base class logic for relevant keys
        const relevantKeys = this.statusKeys.filter(key => statusUpdate[key] !== undefined);
        if (relevantKeys.length === 0) return;

        // Update controls for each key
        relevantKeys.forEach(key => {
            const value = statusUpdate[key];
            const transformedValue = this.statusValueTransform(value, key);

            if (key === 'sgt') {
                this.thresholdSlider.setValue(transformedValue);
            }

            if (key === 'sgr') {
                // Update result display and fill
                this.resultDisplay.updateValue(`${transformedValue}`);
                if (this.thresholdSlider.fillElement) {
                    this.thresholdSlider.updateFillWidth(parseFloat(transformedValue));
                    this.thresholdSlider.fillElement.style.opacity = "1.0";
                }
                // Color fill based on stall threshold
                const sliderValue = parseInt(this.thresholdSlider.slider.value);
                const actualThresholdValue = 255 - sliderValue;
                const stallThreshold = actualThresholdValue * 2;
                if (this.thresholdSlider.fillElement) {
                    if (statusUpdate.sgr < stallThreshold) {
                        this.thresholdSlider.setFillColor('#ef4444');
                    } else if (statusUpdate.sgr < (stallThreshold * 1.2)) {
                        this.thresholdSlider.setFillColor('#f59e0b');
                    } else {
                        this.thresholdSlider.setFillColor('#10b981');
                    }
                }
            }
        });
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
    constructor(emergencyStopBtn, speedSlider = null, config = {}) {
        super({
            commandType: 'es',
            ...config
        });
        this.emergencyStopBtn = emergencyStopBtn;
        this.speedSlider = speedSlider;
        this.addControl(emergencyStopBtn);
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
    constructor(resetStatsBtn, config = {}) {
        super({
            commandType: 'rc',
            ...config
        });
        this.resetStatsBtn = resetStatsBtn;
        this.addControl(resetStatsBtn);
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
    constructor(resetStallBtn, config = {}) {
        super({
            commandType: 'rs',
            ...config
        });
        this.resetStallBtn = resetStallBtn;
        this.addControl(resetStallBtn);
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
            commandType: 'en',
            statusKeys: ['en'],
            debounceTime: 0
        });
        this.addControl(motorToggle);
        this.addControl(motorStatusDisplay);
    }
}

/**
 * Current control binding
 */
class CurrentControlBinding extends ControlBinding {
    constructor(currentSlider, currentDisplay) {
        super({
            commandType: 'sc',
            statusKeys: ['cur'],
            inputValueTransform: (value) => parseInt(value)
        });
        this.addControl(currentSlider);
        this.addControl(currentDisplay);
    }
}

/**
 * Strength control binding
 */
class StrengthControlBinding extends ControlBinding {
    constructor(strengthSlider) {
        super({
            commandType: 'sv',
            statusKeys: ['svs'],
            inputValueTransform: (value) => parseInt(value) / 100.0,
            statusValueTransform: (value) => Math.round(value * 100)
        });
        this.addControl(strengthSlider);
    }
}

/**
 * Phase control binding
 */
class PhaseControlBinding extends ControlBinding {
    constructor(phaseSlider) {
        super({
            commandType: 'svp',
            statusKeys: ['svp'],
            inputValueTransform: (value) => {
                const phase = parseInt(value);
                let phaseForRadians = phase;
                if (phaseForRadians < 0) {
                    phaseForRadians += 360;
                }
                return (phaseForRadians * Math.PI) / 180;
            },
            statusValueTransform: (value) => {
                let phaseDegrees = Math.round((value * 180) / Math.PI);
                if (phaseDegrees > 180) {
                    phaseDegrees -= 360;
                }
                return phaseDegrees;
            }
        });
        this.addControl(phaseSlider);
    }
}
