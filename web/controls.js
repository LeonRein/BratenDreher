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
 * Base control class with standardized interface for UI controls.
 * All controls implement setValue, getValue, and onChange.
 */
class BaseControl {
    constructor(elements, options = {}) {
        this.elements = Array.isArray(elements) ? elements : (elements ? [elements] : []);
        this.options = { debounceTime: 500, ...options };
        this.timer = null;
        this.displayState = CONTROL_STATES.DISABLED;
        this.additionalElements = [];
        this._onChange = null;
        this.setDisplayState(CONTROL_STATES.DISABLED);
    }

    addAdditionalElement(element, options = {}) {
        if (!element) return;
        const alreadyExists = this.additionalElements.some(item => item.element === element);
        if (alreadyExists) return;
        this.additionalElements.push({
            element,
            applyOpacity: options.applyOpacity !== false,
            applyDisabled: options.applyDisabled !== false,
            applyColors: options.applyColors !== false,
            applyClasses: options.applyClasses !== false
        });
    }

    setDisplayState(state, force = false) {
        if (!force && this.displayState === state) return;
        const validStates = Object.values(CONTROL_STATES);
        if (!validStates.includes(state)) state = CONTROL_STATES.DISABLED;
        this.displayState = state;
        const config = STATE_CONFIGS[state];
        this.elements.forEach(element => this.applyStateToElement(element, config));
        this.additionalElements.forEach(({ element, applyOpacity, applyDisabled, applyColors, applyClasses }) => {
            const elementConfig = { ...config };
            if (!applyOpacity) elementConfig.opacity = undefined;
            if (!applyDisabled) elementConfig.disabled = undefined;
            if (!applyColors) { elementConfig.borderColor = undefined; elementConfig.textColor = undefined; }
            if (!applyClasses) { elementConfig.addClass = undefined; elementConfig.removeClass = undefined; }
            this.applyStateToElement(element, elementConfig);
        });
    }

    applyStateToElement(element, config) {
        if (!element) return;
        if (config.opacity !== undefined) element.style.opacity = config.opacity;
        if (config.disabled !== undefined && 'disabled' in element) element.disabled = config.disabled;
        if (config.borderColor !== undefined) element.style.borderColor = config.borderColor;
        if (config.textColor !== undefined) element.style.color = config.textColor;
        if (config.addClass) element.classList.add(config.addClass);
        if (config.removeClass) element.classList.remove(config.removeClass);
    }

    clearTimer() {
        if (this.timer) { clearTimeout(this.timer); this.timer = null; }
    }

    bindEvents() { }

    handleStatusUpdate(statusUpdate) { }

    setValue(value) { }

    getValue() { return undefined; }

    onChange(callback) { this._onChange = callback; }
}

/**
 * Slider control for range inputs.
 */
class SliderControl extends BaseControl {
    constructor(sliderElement, options = {}) {
        super(sliderElement, options);
        this.slider = this.elements[0];
        this.fillElement = options.fillElement;
        this.options = {
            ...options
        };
        if (this.fillElement) this.addAdditionalElement(this.fillElement, { applyOpacity: false, applyDisabled: false, applyColors: false, applyClasses: false });
        this.bindEvents();
    }

    setDisplayState(state, force = false) {
        if (state === CONTROL_STATES.DISABLED) this.hideFill();
        super.setDisplayState(state, force);
    }

    bindEvents() {
        if (!this.slider) return;
        this.slider.addEventListener('input', (e) => this.handleInput(e));
    }

    handleInput(event) {
        const rawValue = parseFloat(event.target.value);
        const displayValue = rawValue.toString();
        this.setDisplayState(CONTROL_STATES.OUTDATED);
        if (this._onChange) this._onChange(rawValue);
    }

    setValue(value) {
        if (this.slider) {
            this.slider.value = value;
        }
    }

    getValue() {
        return this.slider ? parseFloat(this.slider.value) : undefined;
    }

    updateFillWidth(percentage) {
        if (this.fillElement) {
            this.fillElement.style.width = `${percentage}%`;
            this.fillElement.style.opacity = '1';
        }
    }

    setFillColor(color) {
        if (this.fillElement) {
            // Use background instead of backgroundColor to override gradient
            this.fillElement.style.background = color;
        }
    }

    hideFill() {
        if (this.fillElement) {
            this.fillElement.style.opacity = '0';
        }
    }
}

/**
 * SingleButtonControl for single-action buttons.
 */
class SingleButtonControl extends BaseControl {
    constructor(buttonElement, options = {}) {
        super(buttonElement, options);
        this.button = this.elements[0];
        this.bindEvents();
    }

    bindEvents() {
        if (!this.button) return;
        this.button.addEventListener('click', (e) => this.handleClick(e));
    }

    handleClick(event) {
        this.setDisplayState(CONTROL_STATES.OUTDATED);
        if (this._onChange) {
            this._onChange(true);
        }
    }

    setValue(value) {
        // No-op for single button
    }

    getValue() {
        return undefined;
    }
}

/**
 * RadioGroupControl for mutually exclusive button groups.
 */
class RadioGroupControl extends BaseControl {
    constructor(buttonElements, options = {}) {
        super(buttonElements, options);
        this.buttons = this.elements;
        this.activeClass = options.activeClass || 'active';
        this.bindEvents();
    }

    bindEvents() {
        this.buttons.forEach((button) => {
            if (!button) return;
            button.addEventListener('click', (e) => this.handleClick(e));
        });
    }

    handleClick(event) {
        const button = event.target;
        this.setDisplayState(CONTROL_STATES.OUTDATED);
        this.buttons.forEach(btn => btn.classList.remove(this.activeClass));
        button.classList.add(this.activeClass);
        if (this._onChange && button.dataset.value !== undefined) {
            this._onChange(button.dataset.value);
        }
    }

    setValue(value) {
        this.buttons.forEach(btn => {
            btn.classList.toggle(this.activeClass, btn.dataset.value == value);
        });
    }

    getValue() {
        for (let btn of this.buttons) {
            if (btn.classList.contains(this.activeClass)) {
                if (btn.dataset.value !== undefined) return btn.dataset.value;
                return btn.textContent;
            }
        }
        return undefined;
    }

    /* setActiveButton removed: use setValue(value) instead, which calls setActiveByValue */

    setActiveByValue(value) {
        this.buttons.forEach(btn => {
            let btnValue = btn.dataset.value !== undefined ? btn.dataset.value : undefined;
            if (btnValue !== undefined) {
                const isActive = btnValue == value;
                btn.classList.toggle(this.activeClass, isActive);
            }
        });
    }
}

/**
 * Toggle control for checkboxes/switches.
 */
class ToggleControl extends BaseControl {
    constructor(toggleElement, options = {}) {
        super(toggleElement, options);
        this.toggle = this.elements[0];
        this.options = { debounceTime: 0, ...options };
        this.bindEvents();
    }

    bindEvents() {
        if (!this.toggle) return;
        this.toggle.addEventListener('change', (e) => this.handleChange(e));
    }

    handleChange(event) {
        const value = event.target.checked;
        this.setDisplayState(CONTROL_STATES.OUTDATED);
        if (this._onChange) this._onChange(value);
    }

    setValue(value) {
        if (this.toggle) this.toggle.checked = value;
    }

    getValue() {
        return this.toggle ? this.toggle.checked : false;
    }
}

/**
 * Select control for dropdowns.
 */
class SelectControl extends BaseControl {
    constructor(selectElement, options = {}) {
        super(selectElement, options);
        this.select = this.elements[0];
        this.options = { debounceTime: 0, ...options };
        this.bindEvents();
    }

    bindEvents() {
        if (!this.select) return;
        this.select.addEventListener('change', (e) => this.handleChange(e));
    }

    handleChange(event) {
        const value = event.target.value;
        this.setDisplayState(CONTROL_STATES.OUTDATED);
        if (this._onChange) this._onChange(value);
    }

    setValue(value) {
        if (this.select) this.select.value = value;
    }

    getValue() {
        return this.select ? this.select.value : null;
    }
}

/**
 * Display control for read-only status displays.
 */
class DisplayControl extends BaseControl {
    constructor(displayElements, options = {}) {
        super(displayElements, options);
        this.displays = this.elements;
        this.displayTransform = options.displayTransform || ((x) => x);
        this.options = {
            colorizer: null,
            ...options
        };
        this.bindEvents();
    }

    setValue(value) {
        const transformed = this.displayTransform(value);
        this.displays.forEach(element => {
            if (element) {
element.textContent = transformed;
if (this.options.colorizer) {
    const color = this.options.colorizer(transformed);
    if (color) element.style.color = color;
}
            }
        });
    }

    getValue() {
        return this.displays[0] ? this.displays[0].textContent : undefined;
    }

    updateClass(className) {
        this.displays.forEach(element => {
            if (element) {
                element.className = element.className.replace(/status-\w+/g, '');
                if (className) element.classList.add(className);
            }
        });
    }
}

/**
 * Composite control for managing multiple related UI elements.
 */
class CompositeControl extends BaseControl {
    constructor(options = {}) {
        super([], options);
        this.childControls = [];
        this.options = { ...options };
        this.bindEvents();
    }

    addChildControl(control) {
        this.childControls.push(control);
        control.setDisplayState(this.displayState);
    }

    setDisplayState(state) {
        super.setDisplayState(state);
        if (this.childControls) {
            this.childControls.forEach(control => control.setDisplayState(state));
        }
    }

    bindEvents() {
        this.childControls.forEach(control => control.bindEvents());
    }

    handleStatusUpdate(statusUpdate) {
        this.childControls.forEach(control => {
            if (control.handleStatusUpdate) control.handleStatusUpdate(statusUpdate);
        });
    }

    setValue(value) {
        this.childControls.forEach(control => control.setValue(value));
    }

    getValue() {
        return this.childControls.map(control => control.getValue());
    }

    onChange(callback) {
        this.childControls.forEach(control => control.onChange(callback));
    }
}

/**
 * GraphControl for plotting speed vs. rotation remainder.
 */
class GraphControl extends BaseControl {
    constructor(canvasElement, options = {}) {
        super(canvasElement, options);
        this.canvas = canvasElement;
        this.samples = [];
        this.setSpeed = 1.0;
        this.bgColor = '#3b82f60d';
        this.lineColor = '#10b981';
        this.gridColor = '#e5e7eb';
        this.pointColor = '#3b82f6';
        this.canvas.style.background = this.bgColor;
        this.canvas.style.borderRadius = '8px';
    }

    addSample(angle, speed, setSpeed) {
        if (Math.abs(angle - this.samples[this.samples.length - 2]?.angle) < 0.01) {
            this.samples.pop();
        }
        this.samples.push({ angle, speed });
        const firstIdx = this.samples.findIndex(s => s.angle > angle && s.angle < angle + Math.PI);
        if (firstIdx >= 0) this.samples = this.samples.slice(firstIdx);
        this.setSpeed = setSpeed;
        this.drawGraph();
    }

    drawGraph() {
        const rect = this.canvas.getBoundingClientRect();
        this.canvas.width = Math.round(rect.width);
        this.canvas.height = Math.round(rect.height);
        const width = this.canvas.width;
        const height = this.canvas.height;
        const ctx = this.canvas.getContext('2d');
        ctx.clearRect(0, 0, width, height);
        ctx.strokeStyle = this.gridColor;
        ctx.lineWidth = 1;
        for (let i = 1; i < 4; i++) {
            const x = (i / 4) * width;
            ctx.beginPath();
            ctx.moveTo(x, 0);
            ctx.lineTo(x, height);
            ctx.stroke();
        }
        for (let i = 1; i < 4; i++) {
            const y = (i / 4) * height;
            ctx.beginPath();
            ctx.moveTo(0, y);
            ctx.lineTo(width, y);
            ctx.stroke();
        }
        if (this.samples.length < 2) return;
        const minY = this.setSpeed / 2 - this.setSpeed * 0.15;
        const maxY = this.setSpeed * 2 + this.setSpeed * 0.15;
        ctx.strokeStyle = this.lineColor;
        ctx.lineWidth = 2;
        ctx.beginPath();
        let prevXFrac = null;
        for (let i = 0; i < this.samples.length; i++) {
            const s = this.samples[i];
            const xFrac = s.angle / (2 * Math.PI);
            const x = xFrac * width;
            const y = height - ((s.speed - minY) / (maxY - minY)) * height;
            if (i === 0 || (prevXFrac !== null && xFrac < prevXFrac)) {
                ctx.moveTo(x, y);
            } else {
                ctx.lineTo(x, y);
            }
            prevXFrac = xFrac;
        }
        ctx.stroke();
        if (this.samples.length > 0) {
            const s = this.samples[this.samples.length - 1];
            const xFrac = s.angle / (2 * Math.PI);
            const x = xFrac * width;
            const y = height - ((s.speed - minY) / (maxY - minY)) * height;
            ctx.beginPath();
            ctx.arc(x, y, 6, 0, 2 * Math.PI);
            ctx.fillStyle = this.pointColor;
            ctx.fill();
        }
    }
}
