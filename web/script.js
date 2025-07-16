// Control state constants
const CONTROL_STATES = {
    DISABLED: 'DISABLED',
    OUTDATED: 'OUTDATED', 
    RETRY: 'RETRY',
    TIMEOUT: 'TIMEOUT',
    VALID: 'VALID'
};

// State configuration for consistent styling
const STATE_CONFIGS = {
    [CONTROL_STATES.DISABLED]: { opacity: '0.4', disabled: true, borderColor: '', textColor: '', addClass: 'disabled' },
    [CONTROL_STATES.OUTDATED]: { opacity: '0.7', disabled: false, borderColor: '', textColor: '', removeClass: 'disabled' },
    [CONTROL_STATES.RETRY]: { opacity: '0.8', disabled: false, borderColor: '#3b82f6', textColor: '#3b82f6', removeClass: 'disabled' },
    [CONTROL_STATES.TIMEOUT]: { opacity: '0.6', disabled: false, borderColor: '#f59e0b', textColor: '#f59e0b', removeClass: 'disabled' },
    [CONTROL_STATES.VALID]: { opacity: '1', disabled: false, borderColor: '', textColor: '', removeClass: 'disabled' }
};

/**
 * Base control class that provides common functionality for UI controls
 * including state management, timeout handling, and command processing.
 */
class Control {
    /**
     * @param {HTMLElement|null} element - Main UI element (input, button, etc.)
     * @param {HTMLElement|null} valueElement - Element to display current value
     * @param {Object} options - Configuration options
     * @param {number} options.debounceTime - Debounce delay for input events (ms)
     * @param {number} options.commandTimeout - Timeout for command responses (ms)
     * @param {string} options.commandType - Type of command to send
     * @param {Function} options.valueTransform - Transform input value before sending
     * @param {Function} options.displayTransform - Transform value for display
     * @param {string} options.statusKey - Key to look for in status updates
     */
    constructor(element, valueElement, options = {}) {
        this.element = element;
        this.valueElement = valueElement;
        this.options = {
            debounceTime: 500,
            commandTimeout: 5000, // 5 second timeout for command responses
            commandType: null,
            valueTransform: (value) => value, // Function to transform input value before sending
            displayTransform: (value) => value, // Function to transform value for display
            statusKey: null, // Key to look for in status updates
            ...options
        };
        this.timer = null;
        this.timeoutTimer = null; // Add timeout timer
        this.retryAttempt = 0; // Track retry attempts
        this.lastCommandValue = null; // Store last command value for retry
        this.bratendreherble = null;
        
        // Additional UI elements that should follow the same state as the main element
        this.additionalElements = [];
        
        // Initialize with disabled state (after additionalElements is set)
        this.setDisplayState(CONTROL_STATES.DISABLED);
    }

    setBratenDreherBLE(bratendreherble) {
        this.bratendreherble = bratendreherble;
    }

    /**
     * Add additional UI elements that should follow the main element's state
     * @param {HTMLElement} element - Element to add to state management
     * @param {Object} options - Configuration for how state is applied
     * @param {boolean} options.applyOpacity - Whether to apply opacity changes
     * @param {boolean} options.applyDisabled - Whether to apply disabled state
     * @param {boolean} options.applyColors - Whether to apply color changes
     * @param {boolean} options.applyClasses - Whether to apply CSS classes
     */
    addAdditionalElement(element, options = {}) {
        if (!element) {
            console.warn('Cannot add null/undefined element to additional elements');
            return;
        }
        
        // Check if element is already added to prevent duplicates
        const alreadyExists = this.additionalElements.some(item => item.element === element);
        if (alreadyExists) {
            console.warn('Element is already in additional elements list');
            return;
        }
        
        this.additionalElements.push({
            element,
            applyOpacity: options.applyOpacity !== false, // Default true
            applyDisabled: options.applyDisabled !== false, // Default true
            applyColors: options.applyColors !== false, // Default true
            applyClasses: options.applyClasses !== false // Default true
        });
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
        
        // Set state to outdated when user changes control
        this.setDisplayState(CONTROL_STATES.OUTDATED);
        
        // Debounce the command sending
        if (this.timer) {
            clearTimeout(this.timer);
        }
        
        this.timer = setTimeout(() => {
            this.sendCommand(rawValue);
        }, this.options.debounceTime);
    }

    handleChange(event) {
        const value = event.target.checked;
        
        // Set state to outdated when user changes control
        this.setDisplayState(CONTROL_STATES.OUTDATED);
        
        this.sendCommand(value);
    }

    handleClick(event) {
        if (this.options.clickValue !== undefined) {
            // Set state to outdated when user clicks control
            this.setDisplayState(CONTROL_STATES.OUTDATED);
            
            this.sendCommand(this.options.clickValue);
        }
    }

    async sendCommand(rawValue) {
        if (!this.bratendreherble || !this.options.commandType) {
            console.error('Control not properly configured for command sending');
            return false;
        }

        // Clear any existing timeout timer
        this.clearTimeoutTimer();

        // Store the command value for potential retry
        this.lastCommandValue = rawValue;

        // Apply value transformation
        let transformedValue = this.options.valueTransform ? this.options.valueTransform(rawValue) : rawValue;

        const success = await this.bratendreherble.sendCommand(this.options.commandType, transformedValue, this.options.additionalParams || {});

        if (success) {
            // Start timeout timer to detect lost status updates
            this.timeoutTimer = setTimeout(() => {
                console.warn(`Command timeout for ${this.options.commandType} - no status update received (attempt ${this.retryAttempt + 1})`);
                // Reset to previous state and show warning
                this.handleCommandTimeout();
            }, this.options.commandTimeout);
        }

        return success;
    }

    // Generic method for sending custom commands with timeout handling
    async sendCustomCommand(commandType, value, additionalParams = {}, customTimeout = null) {
        if (!this.bratendreherble) {
            console.error('Control not properly configured for command sending');
            return false;
        }

        // Clear any existing timeout timer
        this.clearTimeoutTimer();

        // Store the command info for potential retry
        this.lastCommandValue = { commandType, value, additionalParams };
        this.retryAttempt = 0;

        // Set state to outdated when command is sent
        this.setDisplayState(CONTROL_STATES.OUTDATED);

        const success = await this.bratendreherble.sendCommand(commandType, value, additionalParams);
        
        if (success) {
            // Start timeout timer to detect lost status updates
            const timeout = customTimeout || this.options.commandTimeout;
            this.timeoutTimer = setTimeout(() => {
                console.warn(`Command timeout for ${commandType} - no status update received (attempt ${this.retryAttempt + 1})`);
                this.handleCustomCommandTimeout();
            }, timeout);
        }
        
        return success;
    }

    // Handle timeout for custom commands
    handleCustomCommandTimeout() {
        if (this.retryAttempt === 0 && this.lastCommandValue !== null) {
            // First timeout - attempt retry
            const { commandType, value, additionalParams } = this.lastCommandValue;
            console.log(`Retrying command ${commandType} (attempt 2/2)...`);
            this.retryAttempt = 1;
            
            // Show brief retry indication
            this.setDisplayState(CONTROL_STATES.RETRY);
            
            // Retry the command after a short delay
            setTimeout(() => {
                this.sendCustomCommand(commandType, value, additionalParams);
            }, 500);
        } else {
            // Second timeout or no command to retry - give up
            const commandType = this.lastCommandValue?.commandType || 'unknown';
            console.warn(`Command ${commandType} failed after retry - giving up`);
            this.setDisplayState(CONTROL_STATES.TIMEOUT);
            
            // Show warning to user about communication failure
            if (this.bratendreherble && this.bratendreherble.showWarning) {
                this.bratendreherble.showWarning(`Command ${commandType} failed after retry. Check connection.`);
            }
            
            // Reset retry state
            this.retryAttempt = 0;
            this.lastCommandValue = null;
        }
        
        // Clear timeout timer
        this.clearTimeoutTimer();
    }

    handleCommandTimeout() {
        if (this.retryAttempt === 0 && this.lastCommandValue !== null) {
            // First timeout - attempt retry
            console.log(`Retrying command ${this.options.commandType} (attempt 2/2)...`);
            this.retryAttempt = 1;
            
            // Show brief retry indication
            this.setDisplayState('RETRY');
            
            // Retry the command after a short delay
            setTimeout(() => {
                this.sendCommand(this.lastCommandValue);
            }, 500);
        } else {
            // Second timeout or no command to retry - give up
            console.warn(`Command ${this.options.commandType} failed after retry - giving up`);
            this.setDisplayState('TIMEOUT');
            
            // Show warning to user about communication failure
            if (this.bratendreherble && this.bratendreherble.showWarning) {
                this.bratendreherble.showWarning(`Command ${this.options.commandType} failed after retry. Check connection.`);
            }
            
            // Reset retry state
            this.retryAttempt = 0;
            this.lastCommandValue = null;
        }
        
        // Clear timeout timer
        this.clearTimeoutTimer();
    }

    // Handle status updates - can be overridden by subclasses for complex logic
    handleStatusUpdate(statusUpdate) {
        // Always clear timeout timer and reset retry state when receiving any status update
        this.clearTimeoutTimer();
        this.retryAttempt = 0;
        this.lastCommandValue = null;
        
        // Check for custom handler first
        if (this.options.customHandler) {
            this.options.customHandler(this, statusUpdate);
            return;
        }
        
        // First, handle standard status key mapping (for simple controls)
        if (this.options.statusKey && statusUpdate[this.options.statusKey] !== undefined) {
            let value = statusUpdate[this.options.statusKey];
            
            // Apply status transform if available (for converting backend values to UI values)
            if (this.options.statusTransform) {
                value = this.options.statusTransform(value);
            }
            
            // Update UI element based on type - basic handling only
            if (this.element) {
                if (this.element.type === 'range') {
                    this.element.value = value;
                } else if (this.element.type === 'checkbox') {
                    this.element.checked = value;
                }
            }
            
            // Update value display element if exists
            if (this.valueElement) {
                this.valueElement.textContent = this.options.displayTransform(value);
                this.valueElement.style.opacity = '1.0'; // VALID state - data received
            }
            
            // Mark that we've received valid status
            this.setDisplayState(CONTROL_STATES.VALID);
        }
        
        // Subclasses can override this method to add additional specialized handling
        // They should call super.handleStatusUpdate(statusUpdate) first to get common behavior
    }

    /**
     * Unified display state management with performance optimization
     * @param {string} state - The display state to apply
     */
    setDisplayState(state) {
        // Skip if state hasn't changed (performance optimization)
        if (this.displayState === state) {
            return;
        }
        
        // Validate state parameter
        const validStates = Object.values(CONTROL_STATES);
        if (!validStates.includes(state)) {
            console.warn(`Invalid display state: ${state}. Using '${CONTROL_STATES.DISABLED}' as fallback.`);
            state = CONTROL_STATES.DISABLED;
        }
        
        this.displayState = state;
        
        const config = STATE_CONFIGS[state];
        
        // Apply to main element
        this.applyStateToElement(this.element, config);
        
        // Apply to value element (but don't touch disabled property)
        if (this.valueElement) {
            this.applyStateToElement(this.valueElement, { ...config, disabled: undefined });
        }
        
        // Apply to additional elements with error handling
        this.additionalElements.forEach(({ element, applyOpacity, applyDisabled, applyColors, applyClasses }) => {
            try {
                const elementConfig = { ...config };
                if (!applyOpacity) elementConfig.opacity = undefined;
                if (!applyDisabled) elementConfig.disabled = undefined;
                if (!applyColors) {
                    elementConfig.borderColor = undefined;
                    elementConfig.textColor = undefined;
                }
                if (!applyClasses) {
                    elementConfig.addClass = undefined;
                    elementConfig.removeClass = undefined;
                }
                this.applyStateToElement(element, elementConfig);
            } catch (error) {
                console.warn(`Failed to apply state to element:`, error);
            }
        });
    }

    // Helper method to apply state configuration to an element
    applyStateToElement(element, config) {
        if (!element) return;
        
        if (config.opacity !== undefined) {
            element.style.opacity = config.opacity;
        }
        if (config.disabled !== undefined && 'disabled' in element) {
            element.disabled = config.disabled;
        }
        if (config.borderColor !== undefined) {
            element.style.borderColor = config.borderColor;
        }
        if (config.textColor !== undefined) {
            element.style.color = config.textColor;
        }
        if (config.addClass) {
            element.classList.add(config.addClass);
        }
        if (config.removeClass) {
            element.classList.remove(config.removeClass);
        }
    }

    // Clear any pending timers
    clearTimer() {
        if (this.timer) {
            clearTimeout(this.timer);
            this.timer = null;
        }
    }

    clearTimeoutTimer() {
        if (this.timeoutTimer) {
            clearTimeout(this.timeoutTimer);
            this.timeoutTimer = null;
        }
    }
}

// Specialized control for speed with preset button management
class SpeedControl extends Control {
    constructor(element, valueElement, presetButtons, triangleElement, setpointSpeedElement, currentSpeedElement, options = {}) {
        super(element, valueElement, options);
        this.presetButtons = presetButtons;
        this.triangleElement = triangleElement;
        this.setpointSpeedElement = setpointSpeedElement;
        this.currentSpeedElement = currentSpeedElement;
        
        // Add preset buttons as additional elements
        if (this.presetButtons) {
            this.presetButtons.forEach(btn => {
                this.addAdditionalElement(btn);
            });
        }
        
        // Add triangle element as additional element (no state management applied)
        if (this.triangleElement) {
            this.addAdditionalElement(this.triangleElement, { 
                applyOpacity: false, 
                applyDisabled: false, 
                applyColors: false, 
                applyClasses: false 
            });
        }
        
        // Add setpoint and current speed elements as additional elements
        if (this.setpointSpeedElement) {
            this.addAdditionalElement(this.setpointSpeedElement, { applyDisabled: false });
        }
        if (this.currentSpeedElement) {
            this.addAdditionalElement(this.currentSpeedElement, { applyDisabled: false });
        }
        
        // Update display state now that all elements are properly registered
        this.setDisplayState(this.displayState);
    }

    // Update triangle position based on current speed
    updateTrianglePosition(currentSpeed) {
        if (!this.triangleElement || !this.element) {
            return;
        }
        
        // Get slider properties
        const slider = this.element;
        const min = parseFloat(slider.min);
        const max = parseFloat(slider.max);
        
        // Clamp current speed to slider range
        const clampedSpeed = Math.max(min, Math.min(max, currentSpeed));
        
        // Calculate position as percentage
        const percentage = (clampedSpeed - min) / (max - min);
        
        // Get slider width and calculate accurate track position
        const sliderRect = slider.getBoundingClientRect();
        const sliderStyle = getComputedStyle(slider);
        
        // Account for the actual track area (excluding browser-specific padding)
        const trackPadding = 12; // Typical browser slider track padding
        const trackWidth = sliderRect.width - (trackPadding * 2);
        const position = percentage * trackWidth + trackPadding;
        
        // Position the triangle (note: CSS already handles centering with left: -8px)
        this.triangleElement.style.left = `${position}px`;
        
        // Show the triangle with a fade-in effect
        this.triangleElement.classList.add('visible');
    }

    // Hide triangle (called during disconnection or emergency stop)
    hideTriangle() {
        if (this.triangleElement) {
            this.triangleElement.classList.remove('visible');
        }
    }

    handleStatusUpdate(statusUpdate) {
        // Call parent to handle standard status key mapping
        super.handleStatusUpdate(statusUpdate);
        
        // Add specialized handling
        if (statusUpdate.speed !== undefined) {
            // Set valid state since we received speed setpoint data
            this.setDisplayState(CONTROL_STATES.VALID);
            
            // Update the speed control (slider/input) with the setpoint
            if (this.element) {
                this.element.value = statusUpdate.speed;
            }
            
            // Update the value display element if it exists
            if (this.valueElement) {
                this.valueElement.textContent = statusUpdate.speed.toFixed(1);
            }
            
            // Update the setpoint speed display in status section with adaptive coloring
            if (this.setpointSpeedElement) {
                this.setpointSpeedElement.textContent = `${statusUpdate.speed.toFixed(1)} RPM`;
                this.setpointSpeedElement.style.opacity = '1.0'; // VALID state - data received
                // Color based on speed level
                if (statusUpdate.speed === 0) {
                    this.setpointSpeedElement.style.color = '#1f2937'; // Black for stopped
                } else if (statusUpdate.speed < 5) {
                    this.setpointSpeedElement.style.color = '#10b981'; // Green for slow
                } else if (statusUpdate.speed < 15) {
                    this.setpointSpeedElement.style.color = '#3b82f6'; // Blue for medium
                } else {
                    this.setpointSpeedElement.style.color = '#8b5cf6'; // Purple for fast
                }
            }
            
            // Update preset button active state based on setpoint
            this.presetButtons.forEach(btn => {
                const presetSpeed = parseFloat(btn.dataset.speed);
                btn.classList.toggle('active', Math.abs(presetSpeed - statusUpdate.speed) < 0.05);
            });
        }
        
        if (statusUpdate.currentSpeed !== undefined) {
            // Update current speed display in status section with adaptive coloring
            if (this.currentSpeedElement) {
                this.currentSpeedElement.textContent = `${statusUpdate.currentSpeed.toFixed(1)} RPM`;
                this.currentSpeedElement.style.opacity = '1.0'; // VALID state - data received
                // Color based on speed level
                if (statusUpdate.currentSpeed === 0) {
                    this.currentSpeedElement.style.color = '#1f2937'; // Black for stopped
                } else if (statusUpdate.currentSpeed < 5) {
                    this.currentSpeedElement.style.color = '#10b981'; // Green for slow
                } else if (statusUpdate.currentSpeed < 15) {
                    this.currentSpeedElement.style.color = '#3b82f6'; // Blue for medium
                } else {
                    this.currentSpeedElement.style.color = '#8b5cf6'; // Purple for fast
                }
            }
            
            // Update the triangle indicator position
            this.updateTrianglePosition(statusUpdate.currentSpeed);
        }
    }
}

// Specialized control for current with adaptive coloring
class CurrentControl extends Control {
    constructor(element, valueElement, currentDisplayElement, options = {}) {
        super(element, valueElement, options);
        this.currentDisplayElement = currentDisplayElement;
        
        // Add current display element as additional element
        if (this.currentDisplayElement) {
            this.addAdditionalElement(this.currentDisplayElement, { applyDisabled: false });
        }
    }

    handleStatusUpdate(statusUpdate) {
        // Call parent to handle standard status key mapping
        super.handleStatusUpdate(statusUpdate);
        
        // Add specialized handling for current display
        if (statusUpdate.current !== undefined) {
            // Set valid state since we received current data
            this.setDisplayState(CONTROL_STATES.VALID);
            
            const current = statusUpdate.current;
            
            // Update the current display element with adaptive coloring
            if (this.currentDisplayElement) {
                this.currentDisplayElement.textContent = `${current}%`;
                this.currentDisplayElement.style.opacity = '1.0'; // VALID state - data received
                
                // Add adaptive coloring based on current level
                if (current <= 20) {
                    this.currentDisplayElement.style.color = '#10b981'; // Green for low current
                } else if (current <= 50) {
                    this.currentDisplayElement.style.color = '#3b82f6'; // Blue for medium current
                } else if (current <= 80) {
                    this.currentDisplayElement.style.color = '#f59e0b'; // Orange for high current
                } else {
                    this.currentDisplayElement.style.color = '#8b5cf6'; // Purple for very high current (100% is normal operation)
                }
            }
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
        
        // Add buttons and direction element as additional elements
        this.addAdditionalElement(this.clockwiseBtn);
        this.addAdditionalElement(this.counterclockwiseBtn);
        this.addAdditionalElement(this.currentDirectionElement, { applyDisabled: false });
        
        // Initialize with disabled state
        this.setDisplayState(CONTROL_STATES.DISABLED);
    }

    bindEvents() {
        if (this.clockwiseBtn) {
            this.clockwiseBtn.addEventListener('click', () => this.setDirection(true));
        }
        if (this.counterclockwiseBtn) {
            this.counterclockwiseBtn.addEventListener('click', () => this.setDirection(false));
        }
    }

    async setDirection(clockwise) {
        // Update UI immediately for responsive feedback
        if (this.clockwiseBtn) {
            this.clockwiseBtn.classList.toggle('active', clockwise);
        }
        if (this.counterclockwiseBtn) {
            this.counterclockwiseBtn.classList.toggle('active', !clockwise);
        }
        
        return await this.sendCustomCommand('direction', clockwise);
    }

    handleStatusUpdate(statusUpdate) {
        // Call parent to get common timeout clearing and retry reset
        super.handleStatusUpdate(statusUpdate);
        
        if (statusUpdate.direction !== undefined) {
            // Set valid state since we received direction data
            this.setDisplayState('VALID');
            
            const clockwise = statusUpdate.direction === 'cw';
            if (this.clockwiseBtn) {
                this.clockwiseBtn.classList.toggle('active', clockwise);
            }
            if (this.counterclockwiseBtn) {
                this.counterclockwiseBtn.classList.toggle('active', !clockwise);
            }
            if (this.currentDirectionElement) {
                this.currentDirectionElement.textContent = clockwise ? 'Clockwise' : 'Counter-clockwise';
                this.currentDirectionElement.style.opacity = '1.0'; // VALID state - data received
                // Add adaptive coloring - blue for clockwise, purple for counter-clockwise
                this.currentDirectionElement.style.color = clockwise ? '#3b82f6' : '#8b5cf6';
            }
        }
    }
}

// Specialized control for motor status with multiple status indicators
class MotorStatusControl extends Control {
    constructor(element, motorStatusElement, options = {}) {
        super(element, null, options);
        this.motorStatusElement = motorStatusElement;
        
        // Add motor status element as additional element
        this.addAdditionalElement(this.motorStatusElement, { applyDisabled: false });
    }

    handleStatusUpdate(statusUpdate) {
        // Call parent to handle standard status key mapping
        super.handleStatusUpdate(statusUpdate);
        
        // Add specialized handling
        if (statusUpdate.enabled !== undefined) {
            // Set valid state since we received enabled data
            this.setDisplayState('VALID');
            
            const enabled = statusUpdate.enabled;
            this.motorStatusElement.textContent = enabled ? 'Enabled' : 'Stopped';
            this.motorStatusElement.style.opacity = '1.0'; // VALID state - data received
            // Add adaptive coloring
            this.motorStatusElement.style.color = enabled ? '#10b981' : '#1f2937'; // Green for enabled, black for stopped
        }
    }
}

// Specialized control for acceleration with time conversion
class AccelerationControl extends Control {
    constructor(element, valueElement, currentAccelerationElement, options = {}) {
        super(element, valueElement, options);
        this.currentAccelerationElement = currentAccelerationElement;
        
        // Add current acceleration element as additional element
        this.addAdditionalElement(this.currentAccelerationElement, { applyDisabled: false });
    }

    handleStatusUpdate(statusUpdate) {
        // Call parent to handle standard status key mapping
        super.handleStatusUpdate(statusUpdate);
        
        if (statusUpdate.acceleration !== undefined) {
            // Set valid state since we received acceleration data
            this.setDisplayState('VALID');
            
            const accelerationTimeDisplay = this.bratendreherble.accelerationToTime(statusUpdate.acceleration).toFixed(1);
            this.currentAccelerationElement.textContent = `${accelerationTimeDisplay}s to max`;
            this.currentAccelerationElement.style.opacity = '1.0'; // VALID state - data received
            
            // Add adaptive coloring based on acceleration time
            const time = parseFloat(accelerationTimeDisplay);
            if (time <= 2) {
                this.currentAccelerationElement.style.color = '#8b5cf6'; // Purple for very fast acceleration
            } else if (time <= 5) {
                this.currentAccelerationElement.style.color = '#3b82f6'; // Blue for fast acceleration
            } else if (time <= 10) {
                this.currentAccelerationElement.style.color = '#10b981'; // Green for medium acceleration
            } else {
                this.currentAccelerationElement.style.color = '#1f2937'; // Black for slow acceleration
            }
            
            // Update acceleration slider if significantly different
            const currentSliderTime = parseFloat(this.element.value);
            const deviceTime = parseFloat(this.bratendreherble.accelerationToTime(statusUpdate.acceleration).toFixed(1));
            if (Math.abs(currentSliderTime - deviceTime) > 0.05) {
                this.element.value = deviceTime;
                this.valueElement.textContent = deviceTime.toFixed(1);
            }
        }
    }
}

// Specialized control for variable speed with UI management
class VariableSpeedControl extends Control {
    constructor(element, variableSpeedControls, variableSpeedStatus, options = {}) {
        super(element, null, options);
        this.variableSpeedControls = variableSpeedControls;
        this.variableSpeedStatus = variableSpeedStatus;
        
        // Add additional elements for state management
        this.addAdditionalElement(this.variableSpeedStatus, { applyDisabled: false });
        
        // Initialize with disabled state
        this.setDisplayState(CONTROL_STATES.DISABLED);
    }

    bindEvents() {
        this.element.addEventListener('change', (e) => {
            this.setVariableSpeedEnabled(e.target.checked);
        });
    }

    async setVariableSpeedEnabled(enabled) {
        // Update UI immediately for responsive feedback
        this.element.checked = enabled;
        this.updateVariableSpeedUI();
        
        const commandType = enabled ? 'enable_speed_variation' : 'disable_speed_variation';
        return await this.sendCustomCommand(commandType, true);
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
        // Call parent to get common timeout clearing and retry reset
        super.handleStatusUpdate(statusUpdate);
        
        if (statusUpdate.speedVariationEnabled !== undefined) {
            // Set valid state since we received speed variation data
            this.setDisplayState('VALID');
            
            const enabled = statusUpdate.speedVariationEnabled;
            
            // Update the checkbox state to match the server state
            if (this.element) {
                this.element.checked = enabled;
            }
            
            // Update the UI based on the new state
            this.updateVariableSpeedUI();
            
            this.variableSpeedStatus.textContent = enabled ? 'ON' : 'OFF';
            this.variableSpeedStatus.style.opacity = '1.0'; // VALID state - data received
            this.variableSpeedStatus.style.color = enabled ? '#10b981' : '#1f2937';
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
        
        // Add all status elements as additional elements
        this.addAdditionalElement(this.statusElement, { applyDisabled: false });
        this.addAdditionalElement(this.stallStatusElement, { applyDisabled: false });
        this.addAdditionalElement(this.stallCountElement, { applyDisabled: false });
        
        // Initialize with disabled state
        this.setDisplayState(CONTROL_STATES.DISABLED);
    }

    handleStatusUpdate(statusUpdate) {
        // Call parent to get common timeout clearing and retry reset
        super.handleStatusUpdate(statusUpdate);
        
        if (statusUpdate.tmc2209Status !== undefined) {
            // Set valid state since we received TMC status data
            this.setDisplayState('VALID');
            
            this.statusElement.textContent = statusUpdate.tmc2209Status ? 'OK' : 'Error';
            this.statusElement.style.opacity = '1.0'; // VALID state - data received
            this.statusElement.style.color = statusUpdate.tmc2209Status ? '#10b981' : '#e74c3c';
        }
        
        if (statusUpdate.stallDetected !== undefined) {
            // Set valid state since we received stall data
            this.setDisplayState('VALID');
            
            this.stallStatusElement.textContent = statusUpdate.stallDetected ? 'STALL!' : 'OK';
            this.stallStatusElement.style.opacity = '1.0'; // VALID state - data received
            this.stallStatusElement.style.color = statusUpdate.stallDetected ? '#e74c3c' : '#10b981';
            this.stallStatusElement.style.fontWeight = statusUpdate.stallDetected ? 'bold' : 'normal';
        }
        
        if (statusUpdate.stallCount !== undefined) {
            // Set valid state since we received stall count data
            this.setDisplayState('VALID');
            
            this.stallCountElement.textContent = statusUpdate.stallCount;
            this.stallCountElement.style.opacity = '1.0'; // VALID state - data received
            this.stallCountElement.style.color = statusUpdate.stallCount > 0 ? '#e74c3c' : '#10b981';
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
        
        // Add all statistics elements as additional elements
        this.addAdditionalElement(this.totalRevolutionsElement, { applyDisabled: false });
        this.addAdditionalElement(this.runTimeElement, { applyDisabled: false });
        this.addAdditionalElement(this.avgSpeedElement, { applyDisabled: false });
        
        // Initialize with disabled state
        this.setDisplayState(CONTROL_STATES.DISABLED);
    }

    handleStatusUpdate(statusUpdate) {
        // Call parent to get common timeout clearing and retry reset
        super.handleStatusUpdate(statusUpdate);
        
        if (statusUpdate.totalRevolutions !== undefined) {
            // Set valid state since we received statistics data
            this.setDisplayState('VALID');
            
            this.totalRevolutionsElement.textContent = statusUpdate.totalRevolutions.toFixed(3);
            this.totalRevolutionsElement.style.opacity = '1.0'; // VALID state - data received
        }
        
        if (statusUpdate.runtime !== undefined) {
            // Set valid state since we received runtime data
            this.setDisplayState('VALID');
            
            this.runTimeElement.textContent = this.formatTime(statusUpdate.runtime);
            this.runTimeElement.style.opacity = '1.0'; // VALID state - data received
        }
        
        // Calculate average speed if we have both runtime and revolutions
        if (statusUpdate.runtime !== undefined || statusUpdate.totalRevolutions !== undefined) {
            // Get current values from UI (our state)
            const currentRevolutions = parseFloat(this.totalRevolutionsElement.textContent) || 0;
            const currentRuntimeText = this.runTimeElement.textContent;
            let currentRuntimeSeconds = 0;
            
            // Parse runtime from HH:MM:SS.mmm format
            if (currentRuntimeText && currentRuntimeText !== '00:00:00.000') {
                const [timePart, millisPart = '0'] = currentRuntimeText.split('.');
                const timeParts = timePart.split(':');
                currentRuntimeSeconds = parseInt(timeParts[0]) * 3600 + parseInt(timeParts[1]) * 60 + parseInt(timeParts[2]);
                // Add milliseconds as fraction of second
                if (millisPart) {
                    currentRuntimeSeconds += parseInt(millisPart) / 1000;
                }
            }
            
            if (currentRuntimeSeconds > 0 && currentRevolutions > 0) {
                const avgSpeed = (currentRevolutions * 60) / currentRuntimeSeconds; // RPM
                this.avgSpeedElement.textContent = avgSpeed.toFixed(1);
                this.avgSpeedElement.style.opacity = '1.0'; // VALID state - data received
            } else {
                this.avgSpeedElement.textContent = '0.0';
                this.avgSpeedElement.style.opacity = '1.0'; // VALID state - data received
            }
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

// Specialized control for Power Delivery management
class PowerDeliveryControl extends Control {
    constructor(voltageSelectElement, negotiateButton, statusElement, powerGoodElement, negotiatedVoltageElement, currentVoltageElement, options = {}) {
        super(voltageSelectElement, null, options); // Use voltage select as main element
        this.negotiateButton = negotiateButton;
        this.statusElement = statusElement;
        this.powerGoodElement = powerGoodElement;
        this.negotiatedVoltageElement = negotiatedVoltageElement;
        this.currentVoltageElement = currentVoltageElement;
        this.negotiationTimeout = null;
        
        // Add all status elements as additional elements
        this.addAdditionalElement(this.statusElement, { applyDisabled: false });
        this.addAdditionalElement(this.powerGoodElement, { applyDisabled: false });
        this.addAdditionalElement(this.negotiatedVoltageElement, { applyDisabled: false });
        this.addAdditionalElement(this.currentVoltageElement, { applyDisabled: false });
        this.addAdditionalElement(this.negotiateButton, { applyDisabled: true });
        
        // Initialize with disabled state
        this.setDisplayState(CONTROL_STATES.DISABLED);
        
        // State mapping for negotiation status
        this.negotiationStates = {
            0: { text: 'Idle', class: 'status-unknown' },
            1: { text: 'Negotiating...', class: 'status-warning' },
            2: { text: 'Success', class: 'status-success' },
            3: { text: 'Failed (No PD Adapter)', class: 'status-error' },
            4: { text: 'Timeout (No PD Adapter)', class: 'status-error' }
        };
    }

    bindEvents() {
        if (this.negotiateButton && this.element) {
            this.negotiateButton.addEventListener('click', () => {
                const selectedVoltage = parseInt(this.element.value);
                if (selectedVoltage && this.parent) {
                    console.log(`PowerDelivery: Requesting voltage change to ${selectedVoltage}V`);
                    
                    // Provide immediate visual feedback
                    this.showNegotiationStarted();
                    
                    // Send power delivery command to set target voltage and start negotiation
                    this.parent.sendCommand('pd_voltage', selectedVoltage);
                }
            });
        }
    }

    showNegotiationStarted() {
        // Update status to show negotiation is starting
        this.statusElement.textContent = 'Negotiating...';
        this.statusElement.className = 'power-value status-warning negotiating';
        this.statusElement.style.opacity = '1.0';
        
        // Temporarily disable the negotiate button and show feedback
        this.negotiateButton.disabled = true;
        this.negotiateButton.textContent = '🔄 Negotiating...';
        this.negotiateButton.className = 'btn btn-secondary negotiating';
        this.negotiateButton.style.opacity = '0.8';
        
        // Set a timeout to re-enable button if no response (fallback)
        if (this.negotiationTimeout) {
            clearTimeout(this.negotiationTimeout);
        }
        this.negotiationTimeout = setTimeout(() => {
            this.resetNegotiateButton();
        }, 10000); // 10 second timeout for button reset
    }

    resetNegotiateButton() {
        this.negotiateButton.disabled = false;
        this.negotiateButton.textContent = '🔌 Renegotiate';
        this.negotiateButton.className = 'btn btn-secondary';
        this.negotiateButton.style.opacity = '1.0';
        if (this.negotiationTimeout) {
            clearTimeout(this.negotiationTimeout);
            this.negotiationTimeout = null;
        }
    }

    handleStatusUpdate(statusUpdate) {
        // Call parent to get common timeout clearing and retry reset
        super.handleStatusUpdate(statusUpdate);
        
        // Update negotiation status
        if (statusUpdate.pdNegotiationStatus !== undefined) {
            this.setDisplayState('VALID');
            
            const status = this.negotiationStates[statusUpdate.pdNegotiationStatus] || 
                          { text: 'Unknown', class: 'status-unknown' };
            
            this.statusElement.textContent = status.text;
            // Remove negotiating class and apply the appropriate status class
            this.statusElement.className = `power-value ${status.class}`;
            this.statusElement.style.opacity = '1.0';
            
            // Reset negotiate button when we receive any status update
            this.resetNegotiateButton();
            
            // Show fallback mode notice if PD failed/timed out
            if (statusUpdate.pdNegotiationStatus === 3 || statusUpdate.pdNegotiationStatus === 4) {
                this.showFallbackNotice();
            } else {
                this.hideFallbackNotice();
            }
        }
        
        // Update power good status
        if (statusUpdate.pdPowerGood !== undefined) {
            this.setDisplayState('VALID');
            
            this.powerGoodElement.textContent = statusUpdate.pdPowerGood ? 'Good' : 'Bad';
            this.powerGoodElement.className = statusUpdate.pdPowerGood ? 
                'power-value status-success' : 'power-value status-error';
            this.powerGoodElement.style.opacity = '1.0';
        }
        
        // Update negotiated voltage
        if (statusUpdate.pdNegotiatedVoltage !== undefined) {
            this.setDisplayState('VALID');
            
            this.negotiatedVoltageElement.textContent = statusUpdate.pdNegotiatedVoltage > 0 ? 
                `${statusUpdate.pdNegotiatedVoltage}V` : '- V';
            this.negotiatedVoltageElement.style.opacity = '1.0';
        }
        
        // Update current voltage measurement
        if (statusUpdate.pdCurrentVoltage !== undefined) {
            this.setDisplayState('VALID');
            
            this.currentVoltageElement.textContent = `${statusUpdate.pdCurrentVoltage.toFixed(1)}V`;
            this.currentVoltageElement.style.opacity = '1.0';
        }
    }
    
    showFallbackNotice() {
        // Find or create fallback notice
        let notice = document.getElementById('pdFallbackNotice');
        if (!notice) {
            notice = document.createElement('div');
            notice.id = 'pdFallbackNotice';
            notice.className = 'power-fallback-notice';
            notice.innerHTML = '⚠️ Power Delivery not available - running in fallback mode. Motor control is still functional.';
            
            // Insert after power status grid
            const powerCard = this.statusElement.closest('.power-card');
            const powerStatus = powerCard.querySelector('.power-status');
            powerStatus.appendChild(notice);
        }
        notice.style.display = 'block';
    }
    
    hideFallbackNotice() {
        const notice = document.getElementById('pdFallbackNotice');
        if (notice) {
            notice.style.display = 'none';
        }
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
        
        // Initialize UI state
        this.updateUI();
        
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
        this.currentSpeedIndicator = document.getElementById('currentSpeedIndicator');
        this.currentSpeedTriangle = document.getElementById('currentSpeedTriangle');
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
        this.stallStatus = document.getElementById('stallStatus');
        this.stallCount = document.getElementById('stallCount');
        this.lastUpdate = document.getElementById('lastUpdate');
        
        // Variable speed status elements
        this.variableSpeedStatus = document.getElementById('variableSpeedStatus');
        
        // Statistics elements
        this.totalRevolutions = document.getElementById('totalRevolutions');
        this.runTime = document.getElementById('runTime');
        this.avgSpeed = document.getElementById('avgSpeed');
        
        // Power delivery elements
        this.voltageSelect = document.getElementById('voltageSelect');
        this.negotiateBtn = document.getElementById('negotiateBtn');
        this.pdStatus = document.getElementById('pdStatus');
        this.pdPowerGood = document.getElementById('pdPowerGood');
        this.pdNegotiatedVoltage = document.getElementById('pdNegotiatedVoltage');
        this.pdCurrentVoltage = document.getElementById('pdCurrentVoltage');
        
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
                
                // Set speed control to outdated state
                const speedControl = this.controls.get('speed');
                if (speedControl) {
                    speedControl.setDisplayState('OUTDATED');
                }
                
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
            
            // Request all current status to synchronize the UI
            console.log('Requesting current status...');
            await this.sendCommand('status_request', null);
            
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

        // Clear any pending control timers and timeout timers
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
            control.clearTimeoutTimer(); // Also clear timeout timers
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

    /**
     * Sets a status element to VALID state (fully opaque) when valid data is received
     * @param {HTMLElement} element - The status element to update
     */
    setStatusElementValid(element) {
        if (element) {
            element.style.opacity = '1.0'; // VALID state - fully opaque
        }
    }
    
    async resetStatistics() {
        // Set statistics control to outdated state
        const statisticsControl = this.controls.get('statistics');
        if (statisticsControl) {
            statisticsControl.setDisplayState('OUTDATED');
        }
        
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
        // Set TMC status control to outdated state
        const tmcStatusControl = this.controls.get('tmcStatus');
        if (tmcStatusControl) {
            tmcStatusControl.setDisplayState('OUTDATED');
        }
        
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
        // Set motor control to outdated state
        const motorControl = this.controls.get('motor');
        if (motorControl) {
            motorControl.setDisplayState('OUTDATED');
        }
        
        // Update UI immediately for responsive feedback
        this.motorToggle.checked = enabled;
        
        return await this.sendCommand('enable', enabled);
    }
    
    async emergencyStop() {
        console.log('Emergency stop triggered');
        
        // Set motor control to outdated state
        const motorControl = this.controls.get('motor');
        if (motorControl) {
            motorControl.setDisplayState('OUTDATED');
        }
        
        // Hide speed triangle during emergency stop
        const speedControl = this.controls.get('speed');
        if (speedControl && speedControl.hideTriangle) {
            speedControl.hideTriangle();
        }
        
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
            } else if (message.type === 'notification') {
                // Notification (warnings and errors only)
                this.handleNotification(message);
            } else {
                console.log('Unknown message type:', message.type);
            }
        } catch (error) {
            console.error('Failed to parse message:', error);
        }
    }
    
    handleStatusUpdate(statusUpdate) {
        console.log('Status update:', statusUpdate);
        
        // Update timestamp and make it fully visible since we're receiving data
        this.lastUpdate.textContent = new Date().toLocaleTimeString();
        this.lastUpdate.style.opacity = '1.0'; // VALID state - data received
        
        // Update all controls using the unified handleStatusUpdate method
        this.controls.forEach(control => {
            control.handleStatusUpdate(statusUpdate);
        });
    }
    
    handleNotification(notification) {
        console.log('Notification received:', notification);
        
        const level = notification.level;
        const message = notification.message || '';
        if (level === 'warning') {
            this.showWarning(message);
            console.log(`Warning: ${message}`);
        } else if (level === 'error') {
            this.showError(message);
            console.error(`Error: ${message}`);
        } else {
            console.warn(`Unknown notification level: ${level}`);
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
        // Set display state for all controls based on connection state
        const state = this.connected ? 'OUTDATED' : 'DISABLED';
        this.controls.forEach(control => {
            control.setDisplayState(state);
        });
        
        // Handle other UI elements that aren't managed by Control system
        const otherControls = [
            this.emergencyStopBtn,
            this.resetStatsBtn,
            this.resetStallBtn
        ];
        
        const opacity = this.connected ? '0.7' : '0.4'; // Outdated state for non-controls
        const disabled = !this.connected;
        
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
        
        this.presetBtns.forEach(btn => {
            btn.disabled = disabled;
            btn.style.opacity = opacity;
            if (disabled) {
                btn.classList.add('disabled');
            } else {
                btn.classList.remove('disabled');
            }
        });
        
        // Hide triangle indicator when disconnected
        if (!this.connected) {
            const speedControl = this.controls.get('speed');
            if (speedControl && speedControl.hideTriangle) {
                speedControl.hideTriangle();
            }
        }
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

    showWarning(message) {
        console.warn(message);
        
        // Create a warning toast notification
        const toast = document.createElement('div');
        toast.className = 'warning-toast';
        toast.textContent = message;
        
        // Add toast styles inline (orange/amber color for warnings)
        toast.style.cssText = `
            position: fixed;
            top: 20px;
            right: 20px;
            background: #f59e0b;
            color: white;
            padding: 16px 20px;
            border-radius: 8px;
            box-shadow: 0 4px 12px rgba(0,0,0,0.3);
            z-index: 1000;
            max-width: 350px;
            font-size: 0.9rem;
            animation: slideIn 0.3s ease;
        `;
        
        // Add animation keyframes (reuse the same ones)
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
        
        // Remove toast after 4 seconds (slightly shorter than error toasts)
        setTimeout(() => {
            toast.style.animation = 'slideOut 0.3s ease';
            setTimeout(() => {
                if (toast.parentNode) {
                    toast.parentNode.removeChild(toast);
                }
            }, 300);
        }, 4000);
    }

    initializeControls() {
        // Initialize ALL status display elements to OUTDATED state (0.7 opacity)
        const statusElements = [
            this.motorStatus,
            this.setpointSpeed,
            this.currentSpeed,
            this.currentAcceleration,
            this.currentDirection,
            this.currentCurrent,
            this.tmc2209Status,
            this.stallStatus,
            this.stallCount,
            this.lastUpdate,
            this.variableSpeedStatus,
            this.totalRevolutions,
            this.runTime,
            this.avgSpeed,
            this.pdStatus,
            this.pdPowerGood,
            this.pdNegotiatedVoltage,
            this.pdCurrentVoltage
        ];
        
        statusElements.forEach(element => {
            if (element) {
                element.style.opacity = '0.7'; // Initial OUTDATED state - visible but indicating no data received
            }
        });

        // Speed control with preset button management
        this.controls.set('speed', new SpeedControl(
            this.speedSlider,
            this.speedValue,
            this.presetBtns,
            this.currentSpeedTriangle,
            this.setpointSpeed,
            this.currentSpeed,
            {
                commandType: 'speed',
                statusKey: 'speed',
                displayTransform: (value) => value.toFixed(1),
                valueTransform: (value) => Math.max(0.1, Math.min(30.0, value))
            }
        ));

        // Current control with display
        this.controls.set('current', new CurrentControl(
            this.currentSlider,
            this.currentValue,
            this.currentCurrent,
            {
                commandType: 'current',
                statusKey: 'current',
                displayTransform: (value) => value.toString(),
                valueTransform: (value) => parseInt(value)
            }
        ));

        // Acceleration time control with time conversion and validation
        this.controls.set('acceleration', new AccelerationControl(
            this.accelerationTimeSlider,
            this.accelerationTimeValue,
            this.currentAcceleration,
            {
                commandType: 'acceleration',
                statusKey: null, // Handled specially in the control
                displayTransform: (value) => Number(value).toFixed(1),
                valueTransform: (timeValue) => {
                    // Convert time to acceleration (steps/s²)
                    const acceleration = this.timeToAcceleration(parseInt(timeValue));
                    
                    // Apply minimum acceleration validation (backend limit: 100 steps/s²)
                    const minAcceleration = 100;
                    if (acceleration < minAcceleration) {
                        // Update UI to show the clamped value
                        const minTime = this.accelerationToTime(minAcceleration).toFixed(1);
                        const control = this.controls.get('acceleration');
                        if (control && control.valueElement) {
                            control.valueElement.textContent = minTime;
                        }
                        if (control && control.element) {
                            control.element.value = minTime;
                        }
                        
                        // Show warning for user feedback
                        if (this.showWarning) {
                            this.showWarning('Acceleration too low. Set to minimum allowed.');
                        }
                        return minAcceleration;
                    }
                    
                    return acceleration;
                }
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

        // Power delivery control
        this.controls.set('powerDelivery', new PowerDeliveryControl(
            this.voltageSelect,
            this.negotiateBtn,
            this.pdStatus,
            this.pdPowerGood,
            this.pdNegotiatedVoltage,
            this.pdCurrentVoltage
        ));

        // Statistics control with calculation logic
        this.controls.set('statistics', new StatisticsControl(
            this.totalRevolutions,
            this.runTime,
            this.avgSpeed
        ));

        // Set parent reference and bind events for all controls
        this.controls.forEach(control => {
            control.setBratenDreherBLE(this);
            control.bindEvents();
        });
    }
}

// Initialize the application when the page loads
document.addEventListener('DOMContentLoaded', () => {
    console.log('Initializing BratenDreher Web Interface');
    new BratenDreherBLE();
});
