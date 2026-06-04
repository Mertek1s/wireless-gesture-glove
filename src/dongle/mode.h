/**
 * Wireless Gesture Glove - mode module (dongle).
 *
 * The dongle's operating-mode state machine. The glove only sends the raw
 * pinky+thumb contact bit, so all of its tap/hold timing happens here: a tap
 * and two hold durations drive every transition between tracking, the slow
 * precision pointer and a paused sleep state.
 *
 *   TRACKING: - tap -> toggle slow/precision mode
 *             - hold 1.5 s -> SLEEPING
 *
 *   SLEEPING: - tap -> resume TRACKING (cursor snaps to the hand)
 *             - hold 3 s -> request a recalibration
 *
 * Calibration itself is handled by main() blocking in pointerCalibrate(),
 * so this machine only ever toggles between TRACKING and SLEEPING.
 */
#pragma once

#include <stdint.h>

enum DongleMode
{
    MODE_TRACKING = 0, // driving the cursor from the hand orientation
    MODE_SLEEPING // tracking paused: cursor frozen, clicks ignored
};

// Result of one modeTick(). The transition flags are one-shot, each is true
// only on the single tick its transition happened so the caller can fire a
// buzzer cue or side effect exactly once.
struct ModeUpdate
{
    DongleMode mode; // mode after this tick
    bool precision; // slow / precision-zoom pointer active (TRACKING only)
    bool enteredSleep; // tracking was just paused
    bool resumed; // just woke from sleep
    bool precisionToggled; // slow mode was just flipped by a tap
    bool recalibrate; // user asked to recalibrate: caller runs pointerCalibrate()
};

// Resets to TRACKING with precision off. Call once from setup().
void modeBegin();

// Advances the machine with the live pinky+thumb contact (raw, one packet's
// worth). Call once for every received packet - hold/tap timing relies on a
// steady call rate, matched here by the glove's ~50 Hz transmit cadence.
ModeUpdate modeTick(bool pinkyContact);

// Call after a blocking recalibration returns: drops back to TRACKING and
// ignores the pinky until it is released so the recalibrate hold that is
// likely still down is not read as a fresh gesture.
void modeAfterCalibration();
