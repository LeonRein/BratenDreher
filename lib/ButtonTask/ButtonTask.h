#ifndef BUTTON_TASK_H
#define BUTTON_TASK_H

/**
 * @file ButtonTask.h
 * @brief On-board push button handling for the PD-Stepper board.
 *
 * The three buttons map to the physical layout on the board:
 *   SW1 (left)   - slower
 *   SW2 (middle) - start/stop
 *   SW3 (right)  - faster
 *
 * All three are active low with external pull-ups. Presses are debounced and
 * turned into commands posted through SystemCommand, exactly like the ones the
 * BLE client sends - so the web UI stays in sync automatically via the normal
 * status updates.
 *
 * Note that SW1 doubles as the OTA trigger: holding it during boot enters OTA
 * mode instead (see OTA.h). That only samples the pin once at startup, so there
 * is no conflict with its use as the "slower" button afterwards.
 */

#include <Arduino.h>
#include "Task.h"
#include "BoardPins.h"
#include "SystemCommand.h"
#include "dbg_print.h"

// Timing
#define BUTTON_POLL_INTERVAL_MS   10
#define BUTTON_DEBOUNCE_MS        30   // Mechanical bounce settles well inside this
#define BUTTON_REPEAT_DELAY_MS    700  // Hold this long before auto-repeat kicks in
#define BUTTON_REPEAT_INTERVAL_MS 250  // Then one step every this often

// How much one press of the left/right buttons changes the setpoint
#define BUTTON_SPEED_STEP_RPM 1.0f

class ButtonTask : public Task
{
public:
    static ButtonTask &getInstance()
    {
        static ButtonTask instance;
        return instance;
    }

private:
    enum class Action
    {
        SPEED_DOWN,
        TOGGLE,
        SPEED_UP
    };

    struct Button
    {
        uint8_t pin;
        Action action;
        bool repeats;     // Hold-to-repeat (speed buttons only)
        bool rawState;    // Last sampled level, true = pressed
        bool stableState; // Debounced level
        // True once we have seen this hold actually begin. A button that was
        // already down when the task started is deliberately not armed, so it
        // cannot auto-repeat until it has been released and pressed again.
        bool pressArmed;
        bool repeating;
        unsigned long lastEdgeMs;   // When rawState last changed
        unsigned long pressedAtMs;  // When the debounced press began
        unsigned long lastRepeatMs; // When the last repeat fired
    };

    static const size_t BUTTON_COUNT = 3;
    Button buttons[BUTTON_COUNT];

    ButtonTask();
    ~ButtonTask() {}
    ButtonTask(const ButtonTask &) = delete;
    ButtonTask &operator=(const ButtonTask &) = delete;

    void updateButton(Button &button, unsigned long now);
    void fire(Action action);

protected:
    void run() override;
};

#endif // BUTTON_TASK_H
