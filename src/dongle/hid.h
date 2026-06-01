/**
 * Wireless Gesture Glove - HID module (dongle).
 *
 * Wraps the ATmega32U4's native USB HID as an absolute-position mouse: the
 * cursor goes straight to where the finger points, so the host PC needs no
 * drivers. On Windows an absolute pointer maps to the primary display.
 */
#pragma once

// Starts the USB HID mouse interface. Call once from setup().
void hidBegin();

// Places the cursor at a normalized screen position: (0,0) top-left,
// (1,1) bottom-right. Values are clamped to that range.
void hidMoveTo(float nx, float ny);

// Holds or releases the left mouse button.
void hidLeftButton(bool pressed);

// Holds or releases the right mouse button.
void hidRightButton(bool pressed);
