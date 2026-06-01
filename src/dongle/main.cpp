/**
 * Wireless Gesture Glove - dongle firmware (Arduino Leonardo).
 *
 * Receives gesture packets from the glove over the nRF24L01 radio link
 * and drives the PC's cursor as a USB HID mouse.
 */
#include <Arduino.h>

#include "radio.h"
#include "hid.h"
#include "buzzer.h"
#include "pointer.h"
#include <protocol.h>

// The BNO055 streams a few all-zero frames while its fusion warms up after
// power-on so the glove counts as ready only once this many consecutive
// non-zero orientation packets have arrived
constexpr uint8_t GLOVE_READY_PACKETS = 8;

// Wait for the glove link to come up and start streaming real orientation,
// printing progress so the user can see the warmup happen.
static void waitForGlove()
{
    Serial.println("waiting for glove link...");
    GesturePacket pkt;
    uint8_t good = 0;
    bool linked = false;
    uint32_t lastInfo = 0;

    while (good < GLOVE_READY_PACKETS)
    {
        if (!radioReceive(pkt) || pkt.version != PROTOCOL_VERSION)
        {
            continue;
        }
        if (!linked)
        {
            linked = true;
            Serial.println("glove link established");
        }

        bool warming = (pkt.eulerX == 0.0f && pkt.eulerY == 0.0f &&
                        pkt.eulerZ == 0.0f);
        good = warming ? 0 : good + 1;

        if (millis() - lastInfo >= 300)
        {
            lastInfo = millis();
            Serial.print(warming ? "  warming up sensor  " : "  orientation live  ");
            Serial.print("x=");
            Serial.print(pkt.eulerX, 1);
            Serial.print(" y=");
            Serial.print(pkt.eulerY, 1);
            Serial.print(" z=");
            Serial.println(pkt.eulerZ, 1);
        }
    }
    Serial.println("glove ready");
    buzzerGloveReady();
}

void setup()
{
    Serial.begin(115200);
    hidBegin();
    buzzerBegin();
    pointerBegin();

    while (!radioBegin())
    {
        Serial.println("nRF24L01 not detected - check wiring");
        delay(1000);
    }
    Serial.println("nRF24L01 ready");
    buzzerDongleReady();

    waitForGlove();

    // Block until the user gets through a valid square trace. The routine
    // already prints reasons and beeps the error tone on every rejection.
    while (!pointerCalibrate())
    {
        Serial.println("calibration failed - restarting");
    }
}

void loop()
{
    GesturePacket packet;
    if (!radioReceive(packet))
    {
        return;
    }
    if (packet.version != PROTOCOL_VERSION)
    {
        return; // firmware built against a different protocol
    }

    // Update buttons before the cursor so the click pin sees
    // the current contact state on the same packet.
    ScreenPos p = pointerUpdate(packet.eulerX, packet.eulerY);
    hidLeftButton(packet.contacts & CONTACT_MIDDLE);
    hidRightButton(packet.contacts & CONTACT_RING);
    hidMoveTo(p.x, p.y);
}
