/**
 * Wireless Gesture Glove - buzzer module (dongle).
 *
 * Drives the passive buzzer wired to pin 18. Provides short, recognisable
 * audio cues so the user does not need to watch the serial monitor to
 * know whether a calibration step succeeded.
 */
#pragma once

// Initialises the buzzer pin. Call once from setup().
void buzzerBegin();

// Single tone - dongle powered up and the radio is ready.
void buzzerDongleReady();

// Rising two-tone - the glove link is up and real orientation is streaming.
void buzzerGloveReady();

// Short single tone - "ready, point at the next corner".
void buzzerPrompt();

// Single bright chirp - "corner captured".
void buzzerCornerCaptured();

// Descending two-tone "beep boop" - calibration step rejected,
// restarting from the first corner.
void buzzerError();

// Ascending three-tone fanfare - all four corners accepted, the
// calibration is locked in.
void buzzerCalibrationDone();

// Short two-tone cue for the slow/precision toggle: descending for "slowed",
// ascending for "back to normal"
void buzzerPrecision(bool on);

// Descending three-tone - tracking paused (sleep).
void buzzerSleep();

// Ascending three-tone - tracking resumed (wake).
void buzzerWake();
