/**
 * Control Bindings - Maps UI events to commands and status updates to UI updates
 * Provides configuration-driven binding between controls and command manager
 */
class ControlBinding {
    constructor(config) {
        this.config = {
            commandType: null,
            statusKeys: [], // Array of status keys this binding responds to
            valueTransform: (value) => value, // Transform UI value before sending command
            statusTransform: (value) => value, // Transform status value before updating UI
            displayTransform: (value) => value.toString(), // Transform value for display
            customStatusHandler: null, // Custom function for complex status handling
            ...config
        };
        
        this.controls = [];
        this.commandManager = null;
    }

    setCommandManager(commandManager) {
        this.commandManager = commandManager;
    }

    addControl(control) {
        this.controls.push(control);
    }

    // Handle value changes from UI controls
    async handleValueChange(value) {
        if (!this.commandManager || !this.config.commandType) {
            console.warn('ControlBinding: No command manager or command type configured');
            return false;
        }

        // Set all controls to outdated state
        this.controls.forEach(control => {
            control.setDisplayState(CONTROL_STATES.OUTDATED);
        });

        // Transform value and send command
        const transformedValue = this.config.valueTransform(value);
        const success = await this.commandManager.sendCommand(
            this.config.commandType, 
            transformedValue, 
            this.config.additionalParams || {}
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
        const relevantKeys = this.config.statusKeys.filter(key => 
            statusUpdate[key] !== undefined
        );

        if (relevantKeys.length === 0) {
            return; // No relevant status updates
        }

        // Set all controls to valid state since we received data
        this.controls.forEach(control => {
            control.setDisplayState(CONTROL_STATES.VALID);
        });

        // Use custom handler if provided
        if (this.config.customStatusHandler) {
            this.config.customStatusHandler(statusUpdate, this.controls, this.config);
            return;
        }

        // Default handling for simple cases
        relevantKeys.forEach(key => {
            const value = statusUpdate[key];
            const transformedValue = this.config.statusTransform(value);
            
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
    constructor(accelerationSlider, accelerationDisplay, config = {}) {
        const MAX_SPEED_RPM = 30.0;
        const GEAR_RATIO = 10;
        const STEPS_PER_REVOLUTION = 200;
        const MICROSTEPS = 16;

        function rpmToStepsPerSecond(rpm) {
            const motorRPM = rpm * GEAR_RATIO;
            const motorStepsPerSecond = (motorRPM * STEPS_PER_REVOLUTION * MICROSTEPS) / 60.0;
            return Math.floor(motorStepsPerSecond);
        }

        function accelerationToTime(accelerationStepsPerSec2) {
            if (accelerationStepsPerSec2 === 0) {
                return 5.0;
            }
            const maxStepsPerSecond = rpmToStepsPerSecond(MAX_SPEED_RPM);
            const timeSeconds = maxStepsPerSecond / accelerationStepsPerSec2;
            return Math.max(1.0, timeSeconds);
        }

        function timeToAcceleration(timeSeconds) {
            const maxStepsPerSecond = rpmToStepsPerSecond(MAX_SPEED_RPM);
            const acceleration = maxStepsPerSecond / timeSeconds;
            return Math.floor(acceleration);
        }

        const defaults = {
            commandType: 'sa',
            statusKeys: ['acc'],
            valueTransform: (sliderValue) => {
                const time = parseFloat(sliderValue);
                const acceleration = timeToAcceleration(time);
                const minAcceleration = 100;
                if (acceleration < minAcceleration) {
                    const minTime = accelerationToTime(minAcceleration).toFixed(1);
                    if (accelerationSlider) {
                        accelerationSlider.setValue(minTime);
                    }
                    // Optionally: show warning if needed (requires commandManager reference)
                    return minAcceleration;
                }
                return acceleration;
            },
            statusTransform: (accelerationValue) => {
                const timeSeconds = accelerationToTime(accelerationValue);
                return parseFloat(timeSeconds.toFixed(1));
            }
        };
        super({ ...defaults, ...config });

        this.accelerationSlider = accelerationSlider;
        this.accelerationDisplay = accelerationDisplay;

        this.addControl(accelerationSlider);
        this.addControl(accelerationDisplay);
    }
}

class StatisticsControlBinding extends ControlBinding {
    constructor(totalRevolutionsDisplay, runTimeDisplay, avgSpeedDisplay, updateAverageSpeed, config = {}) {
        const defaults = {
            statusKeys: ['tr', 'rt'],
            customStatusHandler: (statusUpdate, controls, config) => {
                if (statusUpdate.tr !== undefined) {
                    totalRevolutionsDisplay.updateValue(statusUpdate.tr);
                }
                if (statusUpdate.rt !== undefined) {
                    runTimeDisplay.updateValue(statusUpdate.rt);
                    if (typeof updateAverageSpeed === 'function') {
                        updateAverageSpeed();
                    }
                }
            }
        };
        super({ ...defaults, ...config });

        this.addControl(totalRevolutionsDisplay);
        this.addControl(runTimeDisplay);
        this.addControl(avgSpeedDisplay);
    }
}

class TmcStatusControlBinding extends ControlBinding {
    constructor(tmcStatusDisplay, tmcTempDisplay, stallStatusDisplay, stallCountDisplay, config = {}) {
        const defaults = {
            statusKeys: ['tmcst', 'tmct', 'sd', 'sc'],
            customStatusHandler: (statusUpdate, controls, config) => {
                if (statusUpdate.tmcst !== undefined) {
                    tmcStatusDisplay.updateValue(statusUpdate.tmcst ? 'OK' : 'Error');
                    tmcStatusDisplay.updateClass(statusUpdate.tmcst ? 'status-success' : 'status-error');
                }

                if (statusUpdate.tmct !== undefined) {
                    const tempLabels = ['Normal', 'Warm (>120°C)', 'Elevated (>143°C)', 'High (>150°C)', 'Critical (>157°C)'];
                    const tempIdx = Math.max(0, Math.min(4, statusUpdate.tmct));
                    tmcTempDisplay.updateValue(tempLabels[tempIdx]);
                    const className = tempIdx === 0 ? 'status-success' : (tempIdx < 3 ? 'status-warning' : 'status-error');
                    tmcTempDisplay.updateClass(className);
                }

                if (statusUpdate.sd !== undefined) {
                    stallStatusDisplay.updateValue(statusUpdate.sd ? 'STALL!' : 'OK');
                    const color = statusUpdate.sd ? '#e74c3c' : '#10b981';
                    stallStatusDisplay.displays.forEach(element => {
                        if (element) element.style.color = color;
                        element.style.fontWeight = statusUpdate.sd ? 'bold' : 'normal';
                    });
                }

                if (statusUpdate.sc !== undefined) {
                    stallCountDisplay.updateValue(statusUpdate.sc);
                    const color = statusUpdate.sc > 0 ? '#e74c3c' : '#10b981';
                    stallCountDisplay.displays.forEach(element => {
                        if (element) element.style.color = color;
                    });
                }
            }
        };
        super({ ...defaults, ...config });

        this.addControl(tmcStatusDisplay);
        this.addControl(tmcTempDisplay);
        this.addControl(stallStatusDisplay);
        this.addControl(stallCountDisplay);
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
    constructor(speedSlider, speedDisplay, presetButtons, fillElement, config = {}) {
        const defaults = {
            commandType: 'ss',
            statusKeys: ['sp', 'cs'],
            displayTransform: (value) => value.toFixed(1),
            valueTransform: (value) => Math.max(0.1, Math.min(30.0, value)),
            customStatusHandler: null
        };
        super({ ...defaults, ...config });

        this.speedSlider = speedSlider;
        this.speedDisplay = speedDisplay;
        this.presetButtons = presetButtons;
        this.fillElement = fillElement;

        this.addControl(speedSlider);
        this.addControl(speedDisplay);
        this.addControl(presetButtons);

        // If no customStatusHandler provided, use default
        if (!this.config.customStatusHandler) {
            this.config.customStatusHandler = (statusUpdate, controls, config) => {
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
                    this.speedSlider.updateFillPosition(statusUpdate.cs);
                }
            };
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
    constructor(directionButtons, directionDisplay, config = {}) {
        const defaults = {
            commandType: 'sd',
            statusKeys: ['dir'],
            customStatusHandler: null
        };
        super({ ...defaults, ...config });

        this.directionButtons = directionButtons;
        this.directionDisplay = directionDisplay;

        this.addControl(directionButtons);
        this.addControl(directionDisplay);

        // If no customStatusHandler provided, use default
        if (!this.config.customStatusHandler) {
            this.config.customStatusHandler = (statusUpdate, controls, config) => {
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
            };
        }
    }

    async setDirection(clockwise) {
        return await this.handleValueChange(clockwise);
    }
}

// Variable speed control binding with UI coordination
class VariableSpeedControlBinding extends ControlBinding {
    constructor(toggle, strengthSlider, phaseSlider, statusDisplay, controlsContainer, config = {}) {
        const defaults = {
            commandType: null, // Multiple command types
            statusKeys: ['sve', 'svs', 'svp'],
            customStatusHandler: null
        };
        super({ ...defaults, ...config });

        this.toggle = toggle;
        this.strengthSlider = strengthSlider;
        this.phaseSlider = phaseSlider;
        this.statusDisplay = statusDisplay;
        this.controlsContainer = controlsContainer;

        this.addControl(toggle);
        this.addControl(strengthSlider);
        this.addControl(phaseSlider);
        this.addControl(statusDisplay);

        // If no customStatusHandler provided, use default
        if (!this.config.customStatusHandler) {
            this.config.customStatusHandler = (statusUpdate, controls, config) => {
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
                    const strength = Math.round(statusUpdate.svs * 100);
                    this.strengthSlider.setValue(strength);
                }

                if (statusUpdate.svp !== undefined) {
                    // Convert from radians to degrees and then to -180 to 180 range
                    let phaseDegrees = Math.round((statusUpdate.svp * 180) / Math.PI);
                    if (phaseDegrees > 180) {
                        phaseDegrees -= 360;
                    }
                    this.phaseSlider.setValue(phaseDegrees);
                }
            };
        }
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

// Power delivery control binding with complex state management
class PowerDeliveryControlBinding extends ControlBinding {
    constructor(voltageSelect, negotiateBtn, autoNegotiateBtn, statusElements, config = {}) {
        const defaults = {
            commandType: null, // Multiple command types
            statusKeys: ['pdns', 'pdpg', 'pdnv', 'pdcv'],
            customStatusHandler: null
        };
        super({ ...defaults, ...config });

        this.voltageSelect = voltageSelect;
        this.negotiateBtn = negotiateBtn;
        this.autoNegotiateBtn = autoNegotiateBtn;
        this.statusElements = statusElements; // Object with status display elements
        this.negotiationTimeout = null;

        this.addControl(voltageSelect);
        this.addControl(negotiateBtn);
        this.addControl(autoNegotiateBtn);

        // Add status displays
        Object.values(statusElements).forEach(element => {
            if (element) {
                const display = new DisplayControl(element);
                this.addControl(display);
            }
        });

        // State mapping for negotiation status
        this.negotiationStates = {
            0: { text: 'Idle', class: 'status-unknown' },
            1: { text: 'Negotiating...', class: 'status-warning' },
            2: { text: 'Success', class: 'status-success' },
            3: { text: 'Failed (No PD Adapter)', class: 'status-error' },
            4: { text: 'Auto-Negotiating...', class: 'status-warning' }
        };

        // If no customStatusHandler provided, use default
        if (!this.config.customStatusHandler) {
            this.config.customStatusHandler = (statusUpdate, controls, config) => {
                this.handlePowerDeliveryStatus(statusUpdate);
            };
        }
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
        if (this.statusElements.status) {
            this.statusElements.status.textContent = isAutoNegotiation ? 'Auto-Negotiating...' : 'Negotiating...';
            this.statusElements.status.className = 'power-value status-warning negotiating';
            this.statusElements.status.style.opacity = '1.0';
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

            if (this.statusElements.status) {
                this.statusElements.status.textContent = status.text;
                this.statusElements.status.className = `power-value ${status.class}`;
                if (statusValue === 1 || statusValue === 4) {
                    this.statusElements.status.classList.add('negotiating');
                }
                this.statusElements.status.style.opacity = '1.0';
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
            if (this.statusElements.powerGood) {
                this.statusElements.powerGood.textContent = statusUpdate.pdpg ? 'Good' : 'Bad';
                this.statusElements.powerGood.className = statusUpdate.pdpg ?
                    'power-value status-success' : 'power-value status-error';
                this.statusElements.powerGood.style.opacity = '1.0';
            }
        }

        if (statusUpdate.pdnv !== undefined) {
            if (this.statusElements.negotiatedVoltage) {
                this.statusElements.negotiatedVoltage.textContent = statusUpdate.pdnv > 0 ?
                    `${statusUpdate.pdnv}V` : '- V';
                this.statusElements.negotiatedVoltage.style.opacity = '1.0';
            }
        }

        if (statusUpdate.pdcv !== undefined) {
            if (this.statusElements.currentVoltage) {
                this.statusElements.currentVoltage.textContent = `${statusUpdate.pdcv.toFixed(1)}V`;
                this.statusElements.currentVoltage.style.opacity = '1.0';
            }
        }
    }
}

// StallGuard control binding with load visualization
class StallGuardControlBinding extends ControlBinding {
    constructor(thresholdSlider, resultDisplay, fillElement, config = {}) {
        const defaults = {
            commandType: 'st',
            statusKeys: ['sgt', 'sgr'],
            displayTransform: (value) => {
                const percentage = (value / 255) * 100;
                return `${percentage.toFixed(1)}%`;
            },
            valueTransform: (value) => {
                // Invert slider value: right (255) sends 0, left (0) sends 255
                return 255 - parseInt(value);
            },
            customStatusHandler: null
        };
        super({ ...defaults, ...config });

        this.thresholdSlider = thresholdSlider;
        this.resultDisplay = resultDisplay;
        this.fillElement = fillElement;
        this.currentSgResult = null;

        this.addControl(thresholdSlider);
        this.addControl(resultDisplay);

        // If no customStatusHandler provided, use default
        if (!this.config.customStatusHandler) {
            this.config.customStatusHandler = (statusUpdate, controls, config) => {
                if (statusUpdate.sgr !== undefined) {
                    this.currentSgResult = statusUpdate.sgr;
                    this.updateStallGuardResult(statusUpdate.sgr);
                }

                if (statusUpdate.sgt !== undefined) {
                    // Backend sends 0-255, invert for slider display
                    const invertedSliderValue = 255 - statusUpdate.sgt;
                    this.thresholdSlider.setValue(invertedSliderValue);

                    // Update visual if we have current result
                    if (this.currentSgResult !== null) {
                        this.updateStallGuardResult(this.currentSgResult);
                    }
                }
            };
        }
    }

    updateStallGuardResult(sgResult) {
        if (!this.fillElement || !this.thresholdSlider.slider) return;

        this.currentSgResult = sgResult;

        // Convert SG_RESULT to load percentage (invert the scale)
        const loadPercentage = ((510 - sgResult) / 510) * 100;

        // Update fill width and opacity
        this.fillElement.style.width = `${loadPercentage}%`;
        this.fillElement.style.opacity = '1';

        // Get current threshold and determine color
        const sliderValue = parseInt(this.thresholdSlider.slider.value);
        const actualThresholdValue = 255 - sliderValue;
        const stallThreshold = actualThresholdValue * 2;

        // Color the fill based on proximity to stall threshold
        if (sgResult < stallThreshold) {
            this.fillElement.style.backgroundColor = '#ef4444'; // Red - stall detected
        } else if (sgResult < (stallThreshold * 1.2)) {
            this.fillElement.style.backgroundColor = '#f59e0b'; // Orange - warning
        } else {
            this.fillElement.style.backgroundColor = '#10b981'; // Green - normal
        }

        // Update result display
        this.resultDisplay.updateValue(`${loadPercentage.toFixed(1)}%`);
    }

    hideFill() {
        if (this.fillElement) {
            this.fillElement.style.opacity = '0';
        }
    }
}
 
/**
 * Emergency stop control binding
 */
class EmergencyStopControlBinding extends ControlBinding {
    constructor(emergencyStopBtn, config = {}) {
        super({
            commandType: 'es',
            ...config
        });
        this.emergencyStopBtn = emergencyStopBtn;
        this.addControl(emergencyStopBtn);
    }

    async handleValueChange() {
        // Hide speed fill if available
        if (window.app && window.app.controls && window.app.controls.get('speedSlider')) {
            window.app.controls.get('speedSlider').hideFill();
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

window.EmergencyStopControlBinding = EmergencyStopControlBinding;
window.StatisticsResetControlBinding = StatisticsResetControlBinding;
window.StallResetControlBinding = StallResetControlBinding;
