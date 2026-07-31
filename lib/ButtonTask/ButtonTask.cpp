#include "ButtonTask.h"

ButtonTask::ButtonTask()
    : Task("Button_Task", 3072, 1, 0) // 3KB stack, priority 1, core 0
{
    // Order matches the board silkscreen: SW1 left, SW2 middle, SW3 right.
    // Only the speed buttons auto-repeat - repeating a start/stop toggle would
    // be actively dangerous.
    buttons[0] = {SW1_PIN, Action::SPEED_DOWN, true, false, false, false, false, 0, 0, 0};
    buttons[1] = {SW2_PIN, Action::TOGGLE, false, false, false, false, false, 0, 0, 0};
    buttons[2] = {SW3_PIN, Action::SPEED_UP, true, false, false, false, false, 0, 0, 0};
}

void ButtonTask::run()
{
    dbg_println("Button Task started");

    for (size_t i = 0; i < BUTTON_COUNT; i++)
    {
        pinMode(buttons[i].pin, INPUT); // External pull-ups on the board
    }

    // Let the inputs settle, then seed the debounced state from the actual
    // levels. Seeding rather than assuming "released" means a button that is
    // already held at startup does not fire until it has been released first -
    // which matters for SW1, since that is how OTA mode is requested.
    vTaskDelay(pdMS_TO_TICKS(50));

    const unsigned long startMs = millis();
    for (size_t i = 0; i < BUTTON_COUNT; i++)
    {
        buttons[i].rawState = (digitalRead(buttons[i].pin) == LOW);
        buttons[i].stableState = buttons[i].rawState;
        buttons[i].pressArmed = false; // Not a press we witnessed starting
        buttons[i].lastEdgeMs = startMs;
    }

    while (true)
    {
        const unsigned long now = millis();
        for (size_t i = 0; i < BUTTON_COUNT; i++)
        {
            updateButton(buttons[i], now);
        }
        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_INTERVAL_MS));
    }
}

void ButtonTask::updateButton(Button &button, unsigned long now)
{
    const bool raw = (digitalRead(button.pin) == LOW); // Active low

    // Any change restarts the settling window.
    if (raw != button.rawState)
    {
        button.rawState = raw;
        button.lastEdgeMs = now;
        return;
    }

    if ((now - button.lastEdgeMs) < BUTTON_DEBOUNCE_MS)
    {
        return; // Still bouncing
    }

    // The level has held steady long enough to be believed.
    if (raw != button.stableState)
    {
        button.stableState = raw;
        if (raw)
        {
            button.pressArmed = true;
            button.pressedAtMs = now;
            button.repeating = false;
            fire(button.action); // Act on press, not release
        }
        else
        {
            button.pressArmed = false;
        }
        return;
    }

    if (!button.stableState || !button.repeats || !button.pressArmed)
    {
        return;
    }

    // Held down: repeat so the speed can be run up without dozens of presses.
    if (!button.repeating)
    {
        if ((now - button.pressedAtMs) >= BUTTON_REPEAT_DELAY_MS)
        {
            button.repeating = true;
            button.lastRepeatMs = now;
            fire(button.action);
        }
    }
    else if ((now - button.lastRepeatMs) >= BUTTON_REPEAT_INTERVAL_MS)
    {
        button.lastRepeatMs = now;
        fire(button.action);
    }
}

void ButtonTask::fire(Action action)
{
    SystemCommand &systemCommand = SystemCommand::getInstance();

    switch (action)
    {
    case Action::SPEED_UP:
        systemCommand.sendCommand(StepperCommand::ADJUST_SPEED, BUTTON_SPEED_STEP_RPM);
        dbg_println("Button: speed up");
        break;

    case Action::SPEED_DOWN:
        systemCommand.sendCommand(StepperCommand::ADJUST_SPEED, -BUTTON_SPEED_STEP_RPM);
        dbg_println("Button: speed down");
        break;

    case Action::TOGGLE:
        systemCommand.sendCommand(StepperCommand::TOGGLE_ENABLED);
        dbg_println("Button: toggle motor");
        break;
    }
}
