/**
 * Wireless Gesture Glove - pointer module (dongle).
 *
 * Owns the absolute pointing model: an interactive four-corner trace that
 * records where the user wants the screen edges in (heading, roll) space,
 * and the per-packet inverse-bilinear mapping from a live orientation to a
 * screen position. Because the corners are labelled by intent not by axis,
 * the mapping works whatever way the user's arm is rotated.
 *
 * The dongle calls pointerCalibrate() once at startup and again whenever a
 * recalibrate gesture is later bound to it.
 */
#pragma once

// Normalized screen position for hidMoveTo(): (0,0) top-left, (1,1) bottom-right.
struct ScreenPos {
    float x;
    float y;
};

// One-time setup. Does not touch the radio or the host cursor.
void pointerBegin();

// Runs the blocking square-trace calibration: polls the radio and only
// returns once four corners have been captured and the quad passes every
// sanity check. A rejected trace beeps the error tone and restarts from the
// first corner so in practice this loops until the user succeeds.
bool pointerCalibrate();

// True once a valid mapping exists; until then pointerUpdate() returns {0,0}
bool pointerCalibrated();

// Maps one orientation sample through the calibrated quad to a screen
// position, clamping out-of-quad points to the edge. Pass BNO055 eulerX
// (heading, left/right) and eulerY (roll, up/down); eulerZ is unused as it
// barely moves while pointing.
ScreenPos pointerUpdate(float yaw, float roll);

// Slow / precision-zoom mode. When enabled the cursor anchors at its current
// position and hand motion then covers only a fraction of the screen around
// it, so the same hand sweep gives finer control. Toggling it on does not move
// the cursor, toggling it off snaps back to the true pointed-at position. The
// call is idempotent so it is safe to push the current state every packet
void pointerSetPrecision(bool on);

// true while precision mode is active.
bool pointerPrecision();

// Forgets the smoothing filter so the next pointerUpdate() jumps straight to
// the live orientation instead of slewing toward it. Call when resuming from
// sleep so the cursor snaps to where the hand now points.
void pointerResetFilter();
