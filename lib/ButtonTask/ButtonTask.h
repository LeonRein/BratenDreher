#ifndef BUTTON_TASK_H
#define BUTTON_TASK_H

/**
 * @file ButtonTask.h
 * @brief On-board push button handling for the PD-Stepper board.
 *
 * The three buttons map to the physical layout on the board:
 *   SW1 (left)   - slower (ten more seconds per rotation)
 *   SW2 (middle) - start/stop, double press reverses direction
 *   SW3 (right)  - faster (ten fewer seconds per rotation)
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
#define BUTTON_DOUBLE_PRESS_MS    400  // Two presses within this count as a double

// How much one press of the left/right buttons changes the rotation time.
// Stepping in seconds per rotation rather than RPM keeps the step meaningful
// across the whole range - a fixed RPM step is far too coarse when slow.
#define BUTTON_PERIOD_STEP_S 10.0f

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
        SPEED_UP,
        REVERSE
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

    // A press of the middle button cannot be acted on immediately, because a
    // second press within BUTTON_DOUBLE_PRESS_MS means "reverse" instead of
    // "start/stop". The toggle is therefore held back until that window has
    // passed. The resulting delay is imperceptible for a rotisserie, and it
    // keeps one gesture mapping to exactly one action - the alternative
    // (toggling at once, then undoing it) would briefly start or stop the
    // motor on every direction change.
    bool togglePending;
    unsigned long togglePendingAtMs;

    ButtonTask();
    ~ButtonTask() {}
    ButtonTask(const ButtonTask &) = delete;
    ButtonTask &operator=(const ButtonTask &) = delete;

    void updateButton(Button &button, unsigned long now);
    void handleTogglePress(unsigned long now);
    void updatePendingToggle(unsigned long now);
    void fire(Action action);

protected:
    void run() override;
};

#endif // BUTTON_TASK_H
