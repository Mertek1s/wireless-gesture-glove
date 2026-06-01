/**
 * Wireless Gesture Glove - HID module (dongle).
 *
 * Drives the cursor as an absolute USB mouse (HID-Project) and maps finger
 * contacts to buttons. See hid.h for the interface.
 */
#include "hid.h"

#include <Arduino.h>
#include <HID-Project.h>

// Release a held button only after this many contact-free packets so one
// dropped packet is not read as a release. Kept low so two fast taps stay
// two clicks instead of merging into a hold.
static constexpr uint8_t RELEASE_GRACE_PACKETS = 2;

// After a click pin the cursor in place for this long so every hit of a
// double or triple-click lands on the same pixel. Windows discards double
// clicks whose hits are more than a few pixels apart.
static constexpr uint32_t CLICK_PIN_MS = 350;

// Dragging only begins once a button has been held this long and the cursor
// has moved past DRAG_ESCAPE_FRAC of the screen so a quick tap with a little
// hand wobble can never turn into a drag
static constexpr uint32_t DRAG_ARM_MS = 350;
static constexpr float DRAG_ESCAPE_FRAC = 0.03f;

static bool gLeftHeld = false, gRightHeld = false;
static uint8_t gLeftZero = 0, gRightZero = 0;

static bool gPinned = false;
static bool gFollowing = false; // pin broken, follow the hand until release
static bool gWasHeld = false;
static float gAnchorX = 0.5f, gAnchorY = 0.5f;
static float gPrevX = 0.5f, gPrevY = 0.5f;
static uint32_t gPinUntil = 0;
static uint32_t gHoldStart = 0;

void hidBegin()
{
    AbsoluteMouse.begin();
}

void hidMoveTo(float nx, float ny)
{
    nx = constrain(nx, 0.0f, 1.0f);
    ny = constrain(ny, 0.0f, 1.0f);

    uint32_t now = millis();
    bool held = gLeftHeld || gRightHeld;
    if (held && !gWasHeld)
        gHoldStart = now;
    gWasHeld = held;
    if (held)
        gPinUntil = now + CLICK_PIN_MS;

    if ((int32_t)(now - gPinUntil) >= 0)
    {
        gPinned = false; // window over: resume free movement
        gFollowing = false;
    }
    else if (!gFollowing)
    {
        if (!gPinned)
        {
            gAnchorX = gPrevX; // pin where the click actually landed
            gAnchorY = gPrevY;
            gPinned = true;
        }
        bool movedAway = fabsf(nx - gAnchorX) > DRAG_ESCAPE_FRAC ||
                         fabsf(ny - gAnchorY) > DRAG_ESCAPE_FRAC;
        bool dragArmed = (int32_t)(now - gHoldStart) >= (int32_t)DRAG_ARM_MS;
        // Break the pin for a deliberate drag (held long enough) or to move
        // on to the next target (button already released). Once broken stay
        // free until the window ends, so a drag never stutters.
        if (movedAway && (!held || dragArmed))
        {
            gPinned = false;
            gFollowing = true;
        }
        if (gPinned)
        {
            nx = gAnchorX;
            ny = gAnchorY;
        }
    }

    gPrevX = nx;
    gPrevY = ny;

    // HID-Project maps -32768..32767 across the screen (top-left to
    // bottom-right), so 0..1 becomes that full signed range.
    int ax = (int)lroundf(nx * 65535.0f) - 32768;
    int ay = (int)lroundf(ny * 65535.0f) - 32768;
    AbsoluteMouse.moveTo(constrain(ax, -32768, 32767),
                         constrain(ay, -32768, 32767));
}

static void stickyButton(uint8_t button, bool contactNow,
                         bool &held, uint8_t &zeroStreak)
{
    if (contactNow)
    {
        zeroStreak = 0;
        if (!held)
        {
            AbsoluteMouse.press(button);
            held = true;
        }
    }
    else if (held && ++zeroStreak >= RELEASE_GRACE_PACKETS)
    {
        AbsoluteMouse.release(button);
        held = false;
        zeroStreak = 0;
    }
}

void hidLeftButton(bool pressed)
{
    stickyButton(MOUSE_LEFT, pressed, gLeftHeld, gLeftZero);
}

void hidRightButton(bool pressed)
{
    stickyButton(MOUSE_RIGHT, pressed, gRightHeld, gRightZero);
}
