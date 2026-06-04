/**
 * Wireless Gesture Glove - touch module (glove).
 *
 * Reads the three fingertip-to-thumb contacts, debounces them and
 * classifies each as a tap or a hold. The glove uses this to sense the
 * finger gestures: clicks, the DPI toggle and tracking sleep.
 */
#pragma once

// The fingers wired to touch the thumb, the shared GND contact
enum Finger {
    FINGER_MIDDLE = 0,  // middle + thumb - left click
    FINGER_RING,        // ring + thumb - right click
    FINGER_PINKY,       // pinky + thumb - DPI toggle / tracking sleep
    FINGER_COUNT
};

// A gesture a finger has just completed, reported once by touchRead().
enum Gesture {
    GESTURE_NONE = 0,  // nothing completed since the last read
    GESTURE_TAP,       // contact made and released within the hold window
    GESTURE_HOLD       // contact held past the hold threshold
};

// Debounced touch state for every finger, returned by touchRead().
struct TouchState {
    bool    contact[FINGER_COUNT];  // true while the fingertip touches the thumb
    Gesture gesture[FINGER_COUNT];  // gesture completed since the last read
};

// Configures the contact pins as INPUT_PULLUP. Call once from setup().
void touchBegin();

// Samples and debounces the contacts and classifies taps and holds.
// Call every loop pass - tap/hold timing depends on a steady call rate.
TouchState touchRead();
