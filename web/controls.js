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
 * including state management, timeout handling, and element management.
 * This class is now focused purely on UI state management.
 */
class BaseControl {
    /**
     * @param {HTMLElement|HTMLElement[]} elements - Main UI element(s)
     * @param {Object} options - Configuration options
     */
    constructor(elements, options = {}) {
        this.elements = Array.isArray(elements) ? elements : (elements ? [elements] : []);
        this.options = {
            debounceTime: 500,
            ...options
        };
        this.timer = null;
        this.displayState = CONTROL_STATES.DISABLED;
        
        // Additional UI elements that should follow the same state as the main elements
        this.additionalElements = [];
        
        // Initialize with disabled state
        this.setDisplayState(CONTROL_STATES.DISABLED);
    }

    /**
     * Add additional UI elements that should follow the main element's state
     * @param {HTMLElement} element - Element to add to state management
     * @param {Object} options - Configuration for how state is applied
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
            applyOpacity: options.applyOpacity !== false,
            applyDisabled: options.applyDisabled !== false,
            applyColors: options.applyColors !== false,
            applyClasses: options.applyClasses !== false
        });
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
        
        // Apply to main elements
        this.elements.forEach(element => {
            this.applyStateToElement(element, config);
        });
        
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

    // Bind events - to be implemented by subclasses
    bindEvents() {
        // Override in subclasses
    }

    // Handle status updates - to be implemented by subclasses
    handleStatusUpdate(statusUpdate) {
        // Override in subclasses
    }
}

/**
 * Slider control for range inputs with debouncing, value display, and fill indicators
 */
class SliderControl extends BaseControl {
    constructor(sliderElement, options = {}) {
        super(sliderElement, options);
        this.slider = this.elements[0];
        this.valueElement = options.valueElement;
        this.fillElement = options.fillElement;
        this.options = {
            debounceTime: 500,
            displayTransform: (value) => value.toString(),
            valueTransform: (value) => value,
            ...options
        };
        
        // Add value and fill elements to state management
        if (this.valueElement) {
            this.addAdditionalElement(this.valueElement, { applyDisabled: false });
        }
        if (this.fillElement) {
            this.addAdditionalElement(this.fillElement, { 
                applyOpacity: false, 
                applyDisabled: false, 
                applyColors: false, 
                applyClasses: false 
            });
        }
    }

    bindEvents() {
        if (!this.slider) return;
        
        this.slider.addEventListener('input', (e) => this.handleInput(e));
    }

    handleInput(event) {
        const rawValue = parseFloat(event.target.value);
        const displayValue = this.options.displayTransform(rawValue);
        
        // Update display immediately
        if (this.valueElement) {
            this.valueElement.textContent = displayValue;
        }
        
        // Set state to outdated when user changes control
        this.setDisplayState(CONTROL_STATES.OUTDATED);
        
        // Debounce the value change callback
        if (this.timer) {
            clearTimeout(this.timer);
        }
        
        this.timer = setTimeout(() => {
            if (this.options.onValueChange) {
                const transformedValue = this.options.valueTransform(rawValue);
                this.options.onValueChange(transformedValue);
            }
        }, this.options.debounceTime);
    }

    setValue(value) {
        if (this.slider) {
            this.slider.value = value;
            if (this.valueElement) {
                this.valueElement.textContent = this.options.displayTransform(value);
            }
        }
    }

    updateFillPosition(currentValue) {
        if (!this.fillElement || !this.slider) return;
        
        const min = parseFloat(this.slider.min);
        const max = parseFloat(this.slider.max);
        const clampedValue = Math.max(min, Math.min(max, currentValue));
        const percentage = (clampedValue - min) / (max - min);
        
        this.fillElement.style.width = `${percentage * 100}%`;
        this.fillElement.style.opacity = '1';
    }

    hideFill() {
        if (this.fillElement) {
            this.fillElement.style.opacity = '0';
        }
    }
}

/**
 * Button control for click handling, active states, and button groups
 */
class ButtonControl extends BaseControl {
    constructor(buttonElements, options = {}) {
        super(buttonElements, options);
        this.buttons = this.elements;
        this.options = {
            type: 'single', // 'single', 'toggle', 'radio-group'
            activeClass: 'active',
            clickValue: undefined,
            ...options
        };
    }

    bindEvents() {
        this.buttons.forEach((button, index) => {
            if (!button) return;
            
            button.addEventListener('click', (e) => this.handleClick(e, index));
        });
    }

    handleClick(event, buttonIndex) {
        const button = event.target;
        
        // Set state to outdated when user clicks control
        this.setDisplayState(CONTROL_STATES.OUTDATED);
        
        if (this.options.type === 'radio-group') {
            // Deactivate all buttons, activate clicked one
            this.buttons.forEach(btn => btn.classList.remove(this.options.activeClass));
            button.classList.add(this.options.activeClass);
        } else if (this.options.type === 'toggle') {
            // Toggle the clicked button
            button.classList.toggle(this.options.activeClass);
        }
        
        if (this.options.onClick) {
            const value = this.options.clickValue !== undefined ? 
                this.options.clickValue : 
                (button.dataset.value || buttonIndex);
            this.options.onClick(value, buttonIndex, button);
        }
    }

    setActiveButton(index) {
        if (this.options.type === 'radio-group') {
            this.buttons.forEach((btn, i) => {
                if (btn) {
                    btn.classList.toggle(this.options.activeClass, i === index);
                }
            });
        }
    }

    setActiveByValue(value) {
        this.buttons.forEach(btn => {
            if (btn && btn.dataset.value !== undefined) {
                const isActive = btn.dataset.value == value;
                btn.classList.toggle(this.options.activeClass, isActive);
            }
        });
    }
}

/**
 * Toggle control for checkboxes/switches with associated UI updates
 */
class ToggleControl extends BaseControl {
    constructor(toggleElement, options = {}) {
        super(toggleElement, options);
        this.toggle = this.elements[0];
        this.options = {
            debounceTime: 0, // No debouncing for toggles by default
            ...options
        };
    }

    bindEvents() {
        if (!this.toggle) return;
        
        this.toggle.addEventListener('change', (e) => this.handleChange(e));
    }

    handleChange(event) {
        const value = event.target.checked;
        
        // Set state to outdated when user changes control
        this.setDisplayState(CONTROL_STATES.OUTDATED);
        
        if (this.options.onChange) {
            this.options.onChange(value);
        }
    }

    setValue(value) {
        if (this.toggle) {
            this.toggle.checked = value;
        }
    }

    getValue() {
        return this.toggle ? this.toggle.checked : false;
    }
}

/**
 * Select control for dropdowns and option management
 */
class SelectControl extends BaseControl {
    constructor(selectElement, options = {}) {
        super(selectElement, options);
        this.select = this.elements[0];
        this.options = {
            debounceTime: 0, // No debouncing for selects by default
            ...options
        };
    }

    bindEvents() {
        if (!this.select) return;
        
        this.select.addEventListener('change', (e) => this.handleChange(e));
    }

    handleChange(event) {
        const value = event.target.value;
        
        // Set state to outdated when user changes control
        this.setDisplayState(CONTROL_STATES.OUTDATED);
        
        if (this.options.onChange) {
            this.options.onChange(value);
        }
    }

    setValue(value) {
        if (this.select) {
            this.select.value = value;
        }
    }

    getValue() {
        return this.select ? this.select.value : null;
    }
}

/**
 * Display control for read-only status displays with formatting and color coding
 */
class DisplayControl extends BaseControl {
    constructor(displayElements, options = {}) {
        super(displayElements, options);
        this.displays = this.elements;
        this.options = {
            formatter: (value) => value.toString(),
            colorizer: null, // Function that returns color based on value
            ...options
        };
    }

    updateValue(value) {
        const formattedValue = this.options.formatter(value);
        
        this.displays.forEach(element => {
            if (element) {
                element.textContent = formattedValue;
                element.style.opacity = '1.0'; // VALID state
                
                if (this.options.colorizer) {
                    const color = this.options.colorizer(value);
                    if (color) {
                        element.style.color = color;
                    }
                }
            }
        });
    }

    updateClass(className) {
        this.displays.forEach(element => {
            if (element) {
                // Remove existing status classes
                element.className = element.className.replace(/status-\w+/g, '');
                if (className) {
                    element.classList.add(className);
                }
            }
        });
    }
}

/**
 * Composite control for managing multiple related UI elements with complex interactions
 */
class CompositeControl extends BaseControl {
    constructor(options = {}) {
        super([], options);
        this.childControls = [];
        this.options = {
            ...options
        };
    }

    addChildControl(control) {
        this.childControls.push(control);
        // Sync state with child controls
        control.setDisplayState(this.displayState);
    }

    setDisplayState(state) {
        super.setDisplayState(state);
        // Propagate state to child controls (check if childControls exists)
        if (this.childControls) {
            this.childControls.forEach(control => {
                control.setDisplayState(state);
            });
        }
    }

    bindEvents() {
        // Bind events for all child controls
        this.childControls.forEach(control => {
            control.bindEvents();
        });
    }

    handleStatusUpdate(statusUpdate) {
        // Delegate to child controls
        this.childControls.forEach(control => {
            if (control.handleStatusUpdate) {
                control.handleStatusUpdate(statusUpdate);
            }
        });
    }
}
