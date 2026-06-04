/**
 * Wireless Gesture Glove - mode module (dongle).
 *
 * Turns the raw pinky+thumb contact into taps and holds and runs the
 * tracking/sleep state machine. See mode.h for the interface.
 */
#include "mode.h"

#include <Arduino.h>

// A press released within this window is a tap. Anything longer is a hold,
// or -between this and the hold threshold- a dead zone that does nothing,
// so a slow release can never be mistaken for a tap.
static constexpr uint32_t TAP_MAX_MS = 500;

// In TRACKING a pinky hold this long pauses tracking.
static constexpr uint32_t SLEEP_HOLD_MS = 1500;

// In SLEEPING a pinky hold this long asks for a recalibration.
static constexpr uint32_t RECAL_HOLD_MS = 3000;

static DongleMode gMode = MODE_TRACKING;
static bool gPrecision = false;
static bool gPinkyPrev = true;    // true so a press already down at start is ignored
static uint32_t gPinkyDownAt = 0; // millis() the current press began
static bool gHoldHandled = true;  // a hold/swallow already consumed this press

void modeBegin()
{
    gMode = MODE_TRACKING;
    gPrecision = false;
    gPinkyPrev = true; // swallow whatever is held as setup() ends
    gPinkyDownAt = 0;
    gHoldHandled = true;
}

void modeAfterCalibration()
{
    gMode = MODE_TRACKING;
    gPrecision = false;
    gPinkyPrev = true; // the recalibrate hold may still be down - wait for release
    gHoldHandled = true;
}

ModeUpdate modeTick(bool pinky)
{
    ModeUpdate out;
    out.enteredSleep = false;
    out.resumed = false;
    out.precisionToggled = false;
    out.recalibrate = false;

    uint32_t now = millis();
    bool pressed = pinky && !gPinkyPrev;
    bool released = !pinky && gPinkyPrev;

    if (pressed)
    {
        gPinkyDownAt = now;
        gHoldHandled = false;
    }

    // Holds fire the moment their threshold is crossed, while still held, so
    // the user gets feedback without having to release first.
    if (pinky && !gHoldHandled)
    {
        uint32_t heldFor = now - gPinkyDownAt;
        if (gMode == MODE_TRACKING && heldFor >= SLEEP_HOLD_MS)
        {
            gMode = MODE_SLEEPING;
            gPrecision = false; // sleeping always returns to normal sensitivity
            gHoldHandled = true;
            out.enteredSleep = true;
        }
        else if (gMode == MODE_SLEEPING && heldFor >= RECAL_HOLD_MS)
        {
            gHoldHandled = true;
            out.recalibrate = true;
        }
    }

    // Taps fire on release - only a quick press that did not already trigger
    // a hold counts.
    if (released)
    {
        bool wasTap = !gHoldHandled && (now - gPinkyDownAt) <= TAP_MAX_MS;
        if (wasTap)
        {
            if (gMode == MODE_TRACKING)
            {
                gPrecision = !gPrecision;
                out.precisionToggled = true;
            }
            else
            {
                gMode = MODE_TRACKING;
                out.resumed = true;
            }
        }
        gHoldHandled = false;
    }

    gPinkyPrev = pinky;

    out.mode = gMode;
    out.precision = gPrecision;
    return out;
}
