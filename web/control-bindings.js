/**
 * Control Bindings - Maps UI events to commands and status updates to UI updates
 * Provides configuration-driven binding between controls and command manager
 */
class ControlBinding {
    constructor({
        debounceTime = 0 // ms, 0 disables debounce
    } = {}) {
        this.debounceTime = debounceTime;

        this.statusKeyToControls = {};
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
        // Default: update only controls associated with the key
        const controls = this.statusKeyToControls[key] || [];
        controls.forEach(control => {
            control.setValue(transformedValue);
        });
    }

    setCommandManager(commandManager) {
        this.commandManager = commandManager;
    }

    addControl(statusKey, control) {
        if (!this.statusKeyToControls[statusKey]) {
            this.statusKeyToControls[statusKey] = [];
        }
        this.statusKeyToControls[statusKey].push(control);
    }

    // Handle value changes from UI controls
    async handleValueChange(value, commandType) {
        if (!this.commandManager || !commandType) {
            console.warn('ControlBinding: No command manager or command type configured');
            return false;
        }

        // Set all controls to outdated state
        Object.values(this.statusKeyToControls).flat().forEach(control => {
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
                    Object.values(this.statusKeyToControls).flat().forEach(control => {
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
                Object.values(this.statusKeyToControls).flat().forEach(control => {
                    control.setDisplayState(CONTROL_STATES.RETRY);
                });
            }
            return success;
        }
    }

    // Handle status updates from the backend
    handleStatusUpdate(statusUpdate) {
        const relevantKeys = Object.keys(this.statusKeyToControls).filter(key =>
            statusUpdate[key] !== undefined
        );

        if (relevantKeys.length === 0) {
            return;
        }

        relevantKeys.forEach(key => {
            const controls = this.statusKeyToControls[key] || [];
            controls.forEach(control => {
                control.setDisplayState(CONTROL_STATES.VALID);
            });
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

        this.addControl('acc', accelerationSlider);
        this.addControl('acc', accelerationDisplay);
        if (accelerationTimeValueDisplay) this.addControl('acc', accelerationTimeValueDisplay);

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
    // Matches DEFAULT_ACCELERATION_TIME_S in StepperController.h
    static DEFAULT_TIME_S = 15.0;

    rpmToStepsPerSecond(rpm) {
        const motorRPM = rpm * AccelerationControlBinding.GEAR_RATIO;
        const motorStepsPerSecond = (motorRPM * AccelerationControlBinding.STEPS_PER_REVOLUTION * AccelerationControlBinding.MICROSTEPS) / 60.0;
        return Math.floor(motorStepsPerSecond);
    }

    accelerationToTime(accelerationStepsPerSec2) {
        if (accelerationStepsPerSec2 === 0) {
            return AccelerationControlBinding.DEFAULT_TIME_S;
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
        });

        totalRevolutionsDisplay.displayTransform = (value) => `${Number(value).toFixed(2)}`;
        runTimeDisplay.displayTransform = (milliseconds) => {
            // Format as hh:mm:ss.cc (centiseconds, two decimals)
            const ms = Number(milliseconds);
            const hours = Math.floor(ms / 3600000);
            const minutes = Math.floor((ms % 3600000) / 60000);
            const seconds = Math.floor((ms % 60000) / 1000);
            const centiseconds = Math.floor((ms % 1000) / 10);
            return `${hours.toString().padStart(2, '0')}:${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}.${centiseconds.toString().padStart(2, '0')}`;
        };
        avgSpeedDisplay.displayTransform = (value) => `${Number(value).toFixed(2)} rpm`;

        this.totalRevolutionsDisplay = totalRevolutionsDisplay;
        this.runTimeDisplay = runTimeDisplay;
        this.avgSpeedDisplay = avgSpeedDisplay;

        this.latestRevolutions = 0;
        this.latestRuntimeMs = 0;

        this.addControl('tr', totalRevolutionsDisplay);
        this.addControl('rt', runTimeDisplay);
        this.addControl('tr', avgSpeedDisplay);
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

        if (tmcStatusDisplay) this.addControl('tmcst', tmcStatusDisplay);
        if (tmcTempDisplay) this.addControl('tmct', tmcTempDisplay);
        if (stallStatusDisplay) this.addControl('sd', stallStatusDisplay);
        if (stallCountDisplay) this.addControl('sc', stallCountDisplay);
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



// Speed control binding with preset buttons and fill indicator
class SpeedControlBinding extends ControlBinding {
    /**
     * @param {SliderControl} speedSlider
     * @param {SliderFillControl} speedFillControl
     * @param {DisplayControl} speedDisplay
     * @param {RadioGroupControl} presetButtons
     * @param {DisplayControl} speedValueDisplay
     * @param {DisplayControl} currentSpeedDisplay
     */
    // The slider is linear in *seconds per rotation*, not in RPM. A linear RPM
    // axis wastes almost all of its travel on speeds nobody uses: the useful
    // 0.5-5 rpm band is only ~15% of a 0.1-30 rpm slider, whereas on a period
    // axis it covers ~92%.
    //
    // The axis is mirrored (SLIDER = PERIOD_SUM - period) so that moving right
    // still means faster, which is what people expect from a speed control.
    static MIN_PERIOD_S = 2;    // 30 rpm, slider hard right
    static MAX_PERIOD_S = 120;  // 0.5 rpm, slider hard left
    static PERIOD_SUM = SpeedControlBinding.MIN_PERIOD_S + SpeedControlBinding.MAX_PERIOD_S;
    static SLIDER_MIN = SpeedControlBinding.PERIOD_SUM - SpeedControlBinding.MAX_PERIOD_S;
    static SLIDER_MAX = SpeedControlBinding.PERIOD_SUM - SpeedControlBinding.MIN_PERIOD_S;

    // Firmware limits, used to clamp what we put on the wire.
    static MIN_RPM = 0.1;
    static MAX_RPM = 30.0;

    static rpmToSlider(rpm) {
        const value = Number(rpm);
        if (!Number.isFinite(value) || value <= 0) return SpeedControlBinding.SLIDER_MIN;
        const period = Math.min(
            Math.max(60 / value, SpeedControlBinding.MIN_PERIOD_S),
            SpeedControlBinding.MAX_PERIOD_S
        );
        return SpeedControlBinding.PERIOD_SUM - period;
    }

    static sliderToRpm(sliderValue) {
        const period = SpeedControlBinding.PERIOD_SUM - Number(sliderValue);
        return 60 / period;
    }

    // "8.5 s" / "23 s" / "1:30 min" - the unit switches at one minute, which is
    // where reading a bare seconds count starts to get awkward.
    static formatDuration(seconds) {
        if (!Number.isFinite(seconds) || seconds <= 0) return '–';
        if (seconds < 59.5) {
            return seconds < 10 ? `${seconds.toFixed(1)} s` : `${Math.round(seconds)} s`;
        }
        const total = Math.round(seconds);
        const minutes = Math.floor(total / 60);
        const rest = total % 60;
        return `${minutes}:${String(rest).padStart(2, '0')} min`;
    }

    // Speed is shown in both units: rpm is the machine's unit, seconds per
    // rotation is the one you can actually picture while watching the spit.
    static formatSpeed(rpm) {
        const value = Number(rpm);
        if (!Number.isFinite(value) || value <= 0.005) return '0.00 rpm';
        return `${value.toFixed(2)} rpm · ${SpeedControlBinding.formatDuration(60 / value)}`;
    }

    constructor(speedSlider, speedFillControl, speedDisplay, presetButtons, speedValueDisplay, currentSpeedDisplay) {
        super({
            debounceTime: 150
        });

        this.speedSlider = speedSlider;
        this.speedFillControl = speedFillControl;
        this.speedDisplay = speedDisplay;
        this.presetButtons = presetButtons;
        this.speedValueDisplay = speedValueDisplay;
        this.currentSpeedDisplay = currentSpeedDisplay;

        // Set displayTransform for speed displays if present
        if (speedDisplay) {
            speedDisplay.displayTransform = SpeedControlBinding.formatSpeed;
            if (speedDisplay.options) {
                speedDisplay.options.colorizer = (value) => {
                    if (value === 0) return '#1f2937';
                    if (value < 5) return '#10b981';
                    if (value < 15) return '#3b82f6';
                    return '#8b5cf6';
                };
            }
        }
        if (currentSpeedDisplay) {
            currentSpeedDisplay.displayTransform = SpeedControlBinding.formatSpeed;
        }
        if (speedValueDisplay) {
            speedValueDisplay.displayTransform = SpeedControlBinding.formatSpeed;
        }

        // Register controls if present
        if (speedSlider) this.addControl('sp', speedSlider);
        if (presetButtons) this.addControl('sp', presetButtons);
        if (speedValueDisplay) this.addControl('sp', speedValueDisplay);
        if (speedSlider) this.addControl('cs', speedSlider);
        if (speedDisplay) this.addControl('cs', speedDisplay);
        if (currentSpeedDisplay) this.addControl('cs', currentSpeedDisplay);

        // Wire up event handlers if present. The slider hands out mirrored
        // period values, so everything downstream converts to rpm first.
        if (speedSlider && speedValueDisplay) {
            speedSlider.onChange((sliderValue) => {
                speedValueDisplay.setValue(SpeedControlBinding.sliderToRpm(sliderValue));
                this.handleValueChange(sliderValue, 'ss');
            });
        }
        if (presetButtons && speedSlider) {
            presetButtons.onChange((value) => {
                const rpm = parseFloat(value);
                const sliderValue = SpeedControlBinding.rpmToSlider(rpm);
                speedSlider.setValue(sliderValue);
                if (speedValueDisplay) speedValueDisplay.setValue(rpm);
                this.handleValueChange(sliderValue, 'ss');
            });
        }
    }

    // Slider position -> rpm for the wire, clamped to what the firmware accepts
    // so it never has to answer with an "auto-adjusted" warning.
    inputValueTransform(sliderValue) {
        const rpm = SpeedControlBinding.sliderToRpm(sliderValue);
        return Math.max(SpeedControlBinding.MIN_RPM, Math.min(SpeedControlBinding.MAX_RPM, rpm));
    }

    customStatusHandler(transformedValue, key) {
        if (key === 'sp') {
            this.speedSlider.setValue(SpeedControlBinding.rpmToSlider(transformedValue));
            this.speedDisplay.setValue(transformedValue);
            this.speedValueDisplay.setValue(transformedValue);
            this.updatePresetButtonState(transformedValue);
        }
        if (key === 'cs') {
            if (this.speedSlider && this.speedSlider.slider && this.speedFillControl) {
                const min = parseFloat(this.speedSlider.slider.min);
                const max = parseFloat(this.speedSlider.slider.max);
                // A stopped motor maps to the slow end, so the fill empties out.
                const sliderValue = SpeedControlBinding.rpmToSlider(transformedValue);
                const clampedValue = Math.max(min, Math.min(max, sliderValue));
                const percentage = ((clampedValue - min) / (max - min)) * 100;
                this.speedFillControl.setValue(percentage);
            }
            if (this.currentSpeedDisplay) {
                this.currentSpeedDisplay.setValue(transformedValue);
            }
        }
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
                // Relative tolerance: one slider step is a very different
                // number of rpm at 0.5 rpm than it is at 30 rpm, so a fixed
                // window would keep the slow presets lit across half the range.
                const tolerance = Math.max(0.02, presetSpeed * 0.02);

                if (difference < closestDifference && difference < tolerance) {
                    closestDifference = difference;
                    closestButton = button;
                }
            }
        });

        if (closestButton && closestButton.dataset.value !== undefined) {
            this.presetButtons.setValue(closestButton.dataset.value);
        }
        else {
            // No close preset found, reset to default state
            this.presetButtons.setValue(null);
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
        });

        this.directionButtons = directionButtons;
        this.directionDisplay = directionDisplay;

        if (directionDisplay) {
            directionDisplay.options.colorizer = (value) => value === 'Clockwise' ? '#3b82f6' : '#8b5cf6';
        }

        if (directionButtons) this.addControl('dir', directionButtons);
        if (directionDisplay) this.addControl('dir', directionDisplay);

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
        super({});
        if (toggle) this.addControl('sve', toggle);
        if (statusDisplay) this.addControl('sve', statusDisplay);
        this.toggle = toggle;
        this.statusDisplay = statusDisplay;
        this.controlsContainer = controlsContainer;

        toggle.onChange((enabled) => {
            this.handleValueChange(enabled, 'sve');
        });
    }

    customStatusHandler(transformedValue, key) {
        if (key === 'sve') {
            const enabled = transformedValue;
            this.toggle.setValue(enabled);
            if (this.controlsContainer) {
                if (enabled) {
                    this.controlsContainer.classList.remove('disabled');
                } else {
                    this.controlsContainer.classList.add('disabled');
                }
            }
            this.statusDisplay.setValue(enabled ? 'ON' : 'OFF');
            const color = enabled ? '#10b981' : '#1f2937';
            if (this.statusDisplay.displays) {
                this.statusDisplay.displays.forEach(element => {
                    if (element) element.style.color = color;
                });
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
        super({});

        this.graphControl = graphControl;
        this.lastCa = null;
        this.lastCs = null;
        this.lastSp = null;
        this.lastSve = null;

        // 'sve' is registered first on purpose: keys of a batched status
        // message are handled in registration order, so the graph is cleared
        // before any sample from the new angle origin is added.
        this.addControl('sve', graphControl);
        this.addControl('ca', graphControl);
        this.addControl('cs', graphControl);
        this.addControl('sp', graphControl);
    }

    customStatusHandler(transformedValue, key) {
        if (key === 'sve') {
            // Enabling speed variation re-bases the angle origin in the
            // firmware, so the existing trace no longer lines up with incoming
            // samples. Drop it, and the buffered angle along with it.
            if (this.lastSve !== null && transformedValue !== this.lastSve) {
                this.graphControl.reset();
                this.lastCa = null;
            }
            this.lastSve = transformedValue;
            return;
        }

        // Buffer latest values for each key
        if (key === 'ca') this.lastCa = transformedValue;
        if (key === 'cs') this.lastCs = transformedValue;
        if (key === 'sp') this.lastSp = transformedValue;

        // Add sample to graph when all are available
        if (this.lastCa !== null && this.lastCs !== null && this.lastSp !== null) {
            this.graphControl.addSample(this.lastCa, this.lastCs, this.lastSp);
        }
    }
}

// Power delivery control binding with complex state management
class PowerDeliveryControlBinding extends ControlBinding {
    constructor(voltageSelect, negotiateBtn, autoNegotiateBtn, pdStatusDisplay, pdPowerGoodDisplay, pdNegotiatedVoltageDisplay, pdCurrentVoltageDisplay) {
        super({});

        this.voltageSelect = voltageSelect;
        this.negotiateBtn = negotiateBtn;
        this.autoNegotiateBtn = autoNegotiateBtn;
        this.pdStatusDisplay = pdStatusDisplay;
        this.pdPowerGoodDisplay = pdPowerGoodDisplay;
        this.pdNegotiatedVoltageDisplay = pdNegotiatedVoltageDisplay;
        this.pdCurrentVoltageDisplay = pdCurrentVoltageDisplay;
        // this.negotiationTimeout = null;

        this.addControl('pdnv', voltageSelect);
        this.addControl('pdns', pdStatusDisplay);
        this.addControl('pdpg', pdPowerGoodDisplay);
        this.addControl('pdnv', pdNegotiatedVoltageDisplay);
        this.addControl('pdcv', pdCurrentVoltageDisplay);

        // Wire up event handlers with correct signature
        this.negotiateBtn.onChange(() => {
            const selectedVoltage = parseInt(this.voltageSelect.getValue());
            this.handleValueChange(selectedVoltage, 'stv');
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

    async handleValueChange(value, commandType) {
        if (commandType === 'stv') {
            if (value && this.commandManager) {
                this.showNegotiationStarted(false);
                return await this.commandManager.sendCommand('stv', value);
            }
            return false;
        }
        if (commandType === 'anh') {
            if (this.commandManager) {
                this.showNegotiationStarted(true);
                return await this.commandManager.sendCommand('anh', value);
            }
            return false;
        }
        // fallback to base implementation for other commands
        return super.handleValueChange(value, commandType);
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
            // (Do not set voltageSelect to status code; handled in 'pdnv' below)
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
            // Update voltage selector to negotiated voltage
            if (this.voltageSelect && this.voltageSelect.setValue) {
                this.voltageSelect.setValue(transformedValue);
            }
        }

        if (key === 'pdcv') {
            if (this.pdCurrentVoltageDisplay && this.pdCurrentVoltageDisplay.displays && this.pdCurrentVoltageDisplay.displays[0]) {
                this.pdCurrentVoltageDisplay.displays[0].textContent = `${transformedValue.toFixed(1)} V`;
                this.pdCurrentVoltageDisplay.displays[0].style.opacity = '1.0';
            }
        }
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

        // No timeout fallback needed; rely on backend status updates
    }

    resetNegotiateButtons() {
        this.negotiateBtn.setDisplayState(CONTROL_STATES.VALID);
        this.autoNegotiateBtn.setDisplayState(CONTROL_STATES.VALID);
    }
}

// StallGuard control binding with load visualization
/**
 * StallGuardControlBinding
 * Implements ControlBinding for StallGuard threshold (sgt) and result (sgr).
 * - Uses new ControlBinding constructor: super(commandType)
 * - Registers controls via addControl
 * - Implements transformation functions as class methods
 */
class StallGuardControlBinding extends ControlBinding {
    /**
     * @param {SliderControl} thresholdSlider
     * @param {SliderFillControl} thresholdFillControl
     * @param {DisplayControl} resultDisplay
     * @param {DisplayControl} thresholdValueDisplay
     */
    constructor(thresholdSlider, thresholdFillControl, resultDisplay, thresholdValueDisplay) {
        super({ debounceTime: 150 });
        this.thresholdSlider = thresholdSlider;
        this.thresholdFillControl = thresholdFillControl;
        this.resultDisplay = resultDisplay;
        this.thresholdValueDisplay = thresholdValueDisplay;

        /* No colorizer for slider fill */

        // Set displayTransform for result display
        if (resultDisplay) {
            resultDisplay.displayTransform = (value) => `${value.toFixed(1)} %`;
        }
        if (thresholdValueDisplay) {
            thresholdValueDisplay.displayTransform = (value) => `${value.toFixed(1)} %`;
        }

        this.addControl('sgt', thresholdSlider);
        this.addControl('sgr', thresholdFillControl);
        this.addControl('sgr', resultDisplay);
        this.addControl('sgt', thresholdValueDisplay);

        // Wire up event handler
        this.thresholdSlider.onChange((value) => {
            this.thresholdValueDisplay.setValue(value);
            this.handleValueChange(value, 'st');
        });
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

    inputValueTransform(percent) {
        // Invert percent for backend value (0-100 becomes 100-0)
        return Math.round((100 - percent) * 2.55);
    }

    hideFill() {
        if (this.thresholdFillControl) {
            this.thresholdFillControl.hideFill();
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
        if (emergencyStopBtn) this.addControl('emergency', emergencyStopBtn);

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
        if (resetStatsBtn) this.addControl('resetStats', resetStatsBtn);

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
        if (resetStallBtn) this.addControl('resetStall', resetStallBtn);

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
            debounceTime: 0
        });
        if (motorToggle) this.addControl('en', motorToggle);
        if (motorStatusDisplay) this.addControl('en', motorStatusDisplay);

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
        if (currentSlider) this.addControl('cur', currentSlider);
        if (currentDisplay) this.addControl('cur', currentDisplay);
        if (currentValueDisplay) this.addControl('cur', currentValueDisplay);

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
    constructor(strengthSlider, strengthValueDisplay) {
        super({ debounceTime: 150 });

        if (strengthSlider) this.addControl('svs', strengthSlider);
        if (strengthValueDisplay) {
            this.addControl('svs', strengthValueDisplay);
            strengthValueDisplay.displayTransform = (value) => `${Number(value).toFixed(1)} %`;
        }

        strengthSlider.onChange((value) => {
            strengthValueDisplay.setValue(value);
            this.handleValueChange(value, 'svs');
        });
    }
    inputValueTransform(value) { return parseInt(value) / 100.0; }
    statusValueTransform(value) { return Math.round(value * 100); }
    // No customStatusHandler needed; base class implementation suffices.
}

/**
 * Phase control binding
 */
class PhaseControlBinding extends ControlBinding {
    constructor(phaseSlider, phaseValueDisplay) {
        super({ debounceTime: 150 });

        if (phaseSlider) this.addControl('svp', phaseSlider);
        if (phaseValueDisplay) {
            this.addControl('svp', phaseValueDisplay);
            phaseValueDisplay.displayTransform = (value) => `${Number(-value).toFixed(1)}°`;
        }

        phaseSlider.onChange((value) => {
            phaseValueDisplay.setValue(value);
            this.handleValueChange(value, 'svp');
        });
    }
    inputValueTransform(value) {
        let phase = -parseInt(value);
        if (phase < 0) phase += 360;
        return (phase * Math.PI) / 180;
    }
    statusValueTransform(value) {
        let phaseDegrees = Math.round((value * 180) / Math.PI);
        if (phaseDegrees > 180) phaseDegrees -= 360;
        return -phaseDegrees;
    }
    // No customStatusHandler needed; base class implementation suffices.
    // No customStatusHandler needed; base class implementation suffices.
}
