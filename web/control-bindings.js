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

// Speed control binding with preset buttons and fill indicator
class SpeedControlBinding extends ControlBinding {
    constructor(speedSlider, speedDisplay, presetButtons, fillElement, config = {}) {
        super({
            commandType: 'speed',
            statusKeys: ['speed', 'currentSpeed'],
            displayTransform: (value) => value.toFixed(1),
            valueTransform: (value) => Math.max(0.1, Math.min(30.0, value)),
            ...config
        });

        this.speedSlider = speedSlider;
        this.speedDisplay = speedDisplay;
        this.presetButtons = presetButtons;
        this.fillElement = fillElement;

        this.addControl(speedSlider);
        this.addControl(speedDisplay);
        this.addControl(presetButtons);

        // Custom status handler for speed control
        this.config.customStatusHandler = (statusUpdate, controls, config) => {
            if (statusUpdate.speed !== undefined) {
                // Update setpoint speed
                const speed = statusUpdate.speed;
                this.speedSlider.setValue(speed);
                this.speedDisplay.updateValue(speed);
                
                // Update preset button active state
                this.presetButtons.setActiveByValue(speed);
            }

            if (statusUpdate.currentSpeed !== undefined) {
                // Update fill indicator to show current speed
                this.speedSlider.updateFillPosition(statusUpdate.currentSpeed);
            }
        };
    }
}

// Direction control binding with button coordination
class DirectionControlBinding extends ControlBinding {
    constructor(clockwiseBtn, counterclockwiseBtn, directionDisplay, config = {}) {
        super({
            commandType: 'direction',
            statusKeys: ['direction'],
            ...config
        });

        this.clockwiseBtn = clockwiseBtn;
        this.counterclockwiseBtn = counterclockwiseBtn;
        this.directionDisplay = directionDisplay;

        this.addControl(clockwiseBtn);
        this.addControl(counterclockwiseBtn);
        this.addControl(directionDisplay);

        // Custom status handler for direction control
        this.config.customStatusHandler = (statusUpdate, controls, config) => {
            if (statusUpdate.direction !== undefined) {
                const clockwise = statusUpdate.direction === 'cw';
                
                // Update button states
                this.clockwiseBtn.setActiveButton(clockwise ? 0 : -1);
                this.counterclockwiseBtn.setActiveButton(clockwise ? -1 : 0);
                
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

    async setDirection(clockwise) {
        return await this.handleValueChange(clockwise);
    }
}

// Variable speed control binding with UI coordination
class VariableSpeedControlBinding extends ControlBinding {
    constructor(toggle, strengthSlider, phaseSlider, statusDisplay, controlsContainer, config = {}) {
        super({
            commandType: null, // Multiple command types
            statusKeys: ['speedVariationEnabled', 'speedVariationStrength', 'speedVariationPhase'],
            ...config
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

        // Custom status handler for variable speed
        this.config.customStatusHandler = (statusUpdate, controls, config) => {
            if (statusUpdate.speedVariationEnabled !== undefined) {
                const enabled = statusUpdate.speedVariationEnabled;
                this.toggle.setValue(enabled);
                this.updateVariableSpeedUI(enabled);
                this.statusDisplay.updateValue(enabled ? 'ON' : 'OFF');
                
                // Color coding
                const color = enabled ? '#10b981' : '#1f2937';
                this.statusDisplay.displays.forEach(element => {
                    if (element) element.style.color = color;
                });
            }

            if (statusUpdate.speedVariationStrength !== undefined) {
                const strength = Math.round(statusUpdate.speedVariationStrength * 100);
                this.strengthSlider.setValue(strength);
            }

            if (statusUpdate.speedVariationPhase !== undefined) {
                // Convert from radians to degrees and then to -180 to 180 range
                let phaseDegrees = Math.round((statusUpdate.speedVariationPhase * 180) / Math.PI);
                if (phaseDegrees > 180) {
                    phaseDegrees -= 360;
                }
                this.phaseSlider.setValue(phaseDegrees);
            }
        };
    }

    async setVariableSpeedEnabled(enabled) {
        const commandType = enabled ? 'enable_speed_variation' : 'disable_speed_variation';
        
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
        super({
            commandType: null, // Multiple command types
            statusKeys: ['pdNegotiationStatus', 'pdPowerGood', 'pdNegotiatedVoltage', 'pdCurrentVoltage'],
            ...config
        });

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

        // Custom status handler
        this.config.customStatusHandler = (statusUpdate, controls, config) => {
            this.handlePowerDeliveryStatus(statusUpdate);
        };
    }

    async negotiateVoltage() {
        const selectedVoltage = parseInt(this.voltageSelect.getValue());
        if (selectedVoltage && this.commandManager) {
            this.showNegotiationStarted(false);
            return await this.commandManager.sendCommand('pd_voltage', selectedVoltage);
        }
        return false;
    }

    async autoNegotiate() {
        if (this.commandManager) {
            this.showNegotiationStarted(true);
            return await this.commandManager.sendCommand('pd_auto_negotiate', 1);
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
        if (statusUpdate.pdNegotiationStatus !== undefined) {
            const statusValue = Math.round(statusUpdate.pdNegotiationStatus);
            const status = this.negotiationStates[statusValue] || 
                          { text: `Unknown (${statusUpdate.pdNegotiationStatus})`, class: 'status-error' };

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
            if (statusValue === 2 && statusUpdate.pdNegotiatedVoltage > 0) {
                this.voltageSelect.setValue(statusUpdate.pdNegotiatedVoltage);
            }
        }

        if (statusUpdate.pdPowerGood !== undefined) {
            if (this.statusElements.powerGood) {
                this.statusElements.powerGood.textContent = statusUpdate.pdPowerGood ? 'Good' : 'Bad';
                this.statusElements.powerGood.className = statusUpdate.pdPowerGood ? 
                    'power-value status-success' : 'power-value status-error';
                this.statusElements.powerGood.style.opacity = '1.0';
            }
        }

        if (statusUpdate.pdNegotiatedVoltage !== undefined) {
            if (this.statusElements.negotiatedVoltage) {
                this.statusElements.negotiatedVoltage.textContent = statusUpdate.pdNegotiatedVoltage > 0 ? 
                    `${statusUpdate.pdNegotiatedVoltage}V` : '- V';
                this.statusElements.negotiatedVoltage.style.opacity = '1.0';
            }
        }

        if (statusUpdate.pdCurrentVoltage !== undefined) {
            if (this.statusElements.currentVoltage) {
                this.statusElements.currentVoltage.textContent = `${statusUpdate.pdCurrentVoltage.toFixed(1)}V`;
                this.statusElements.currentVoltage.style.opacity = '1.0';
            }
        }
    }
}

// StallGuard control binding with load visualization
class StallGuardControlBinding extends ControlBinding {
    constructor(thresholdSlider, resultDisplay, fillElement, config = {}) {
        super({
            commandType: 'stallguard_threshold',
            statusKeys: ['stallguardThreshold', 'stallguardResult'],
            displayTransform: (value) => {
                const percentage = (value / 255) * 100;
                return `${percentage.toFixed(1)}%`;
            },
            valueTransform: (value) => {
                // Invert slider value: right (255) sends 0, left (0) sends 255
                return 255 - parseInt(value);
            },
            ...config
        });

        this.thresholdSlider = thresholdSlider;
        this.resultDisplay = resultDisplay;
        this.fillElement = fillElement;
        this.currentSgResult = null;

        this.addControl(thresholdSlider);
        this.addControl(resultDisplay);

        // Custom status handler
        this.config.customStatusHandler = (statusUpdate, controls, config) => {
            if (statusUpdate.stallguardResult !== undefined) {
                this.currentSgResult = statusUpdate.stallguardResult;
                this.updateStallGuardResult(statusUpdate.stallguardResult);
            }

            if (statusUpdate.stallguardThreshold !== undefined) {
                // Backend sends 0-255, invert for slider display
                const invertedSliderValue = 255 - statusUpdate.stallguardThreshold;
                this.thresholdSlider.setValue(invertedSliderValue);
                
                // Update visual if we have current result
                if (this.currentSgResult !== null) {
                    this.updateStallGuardResult(this.currentSgResult);
                }
            }
        };
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
