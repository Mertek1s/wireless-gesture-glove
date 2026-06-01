#include <Arduino.h>
#include <math.h>

#include "pointer.h"
#include "radio.h"
#include "buzzer.h"
#include <protocol.h>

// ---- Tunables ------------------------------------------------------------

// Input smoothing blend, scaled by how fast the hand moves: gentle when near-still (kills jitter),
// aggressive when moving. Reaches ALPHA_MAX once the target is ERR_REF_DEG away.
static constexpr float ALPHA_MIN = 0.12f;
static constexpr float ALPHA_MAX = 0.60f;
static constexpr float ERR_REF_DEG = 6.0f;

// A tap is rejected unless the last STABILITY_SAMPLES readings held steadier
// than STABILITY_MAX_STD_DEG - i.e the hand was still when the corner was set.
static constexpr uint8_t STABILITY_SAMPLES = 8;
static constexpr float STABILITY_MAX_STD_DEG = 1.5f;

// Reject a trace whose corners are closer than this: the user tapped roughly
// the same spot more than once.
static constexpr float MIN_CORNER_SEPARATION_DEG = 10.0f;

// Whole-quad sanity: minimum area and maximum longest/shortest edge ratio, rejecting tiny or sliver-thin traces.
static constexpr float MIN_AREA_DEG2 = 100.0f;
static constexpr float MAX_ASPECT_RATIO = 8.0f;

// No tap for this long restarts the trace from the first corner
static constexpr uint32_t CORNER_TIMEOUT_MS = 20000;

// Pinky+thumb must be held (not tapped) this long to cancel a trace
static constexpr uint32_t CANCEL_HOLD_MS = 1500;

// Cap on the "wait for all fingers released" step so a dead radio cannot hang the device.
static constexpr uint32_t RELEASE_WAIT_TIMEOUT_MS = 5000;

// ---- Calibration state ---------------------------------------------------

enum Corner
{
    CORNER_BL = 0,
    CORNER_TL,
    CORNER_TR,
    CORNER_BR,
    CORNER_COUNT
};
static const char *const CORNER_LABEL[CORNER_COUNT] = {
    "bottom-left", "top-left", "top-right", "bottom-right"};

// a captured corner: .y = heading (unwrapped against the first sample to
// avoid the 360->0 seam), .p = roll.
struct Vec2
{
    float y;
    float p;
};

static bool gCalibrated = false;
static Vec2 gCorner[CORNER_COUNT];
static float gYawRef = 0.0f;

// bilinear coefficients recomputed after each accepted trace.
static float gAy, gBy, gCy;
static float gAp, gBp, gCp;

// smoothed orientation carried between pointerUpdate() calls.
static float gYawFilt = 0.0f;
static float gRollFilt = 0.0f;
static bool gHaveFilt = false;

// ---- Helpers -------------------------------------------------------------

// Shift an angle to within +/-180 deg of a reference
static float unwrap(float angle, float ref)
{
    while (angle - ref > 180.0f)
    {
        angle -= 360.0f;
    }
    while (angle - ref < -180.0f)
    {
        angle += 360.0f;
    }
    return angle;
}

static float clamp01(float v)
{
    if (v < 0.0f)
        return 0.0f;
    if (v > 1.0f)
        return 1.0f;
    return v;
}

static float dist2(const Vec2 &a, const Vec2 &b)
{
    float dy = a.y - b.y;
    float dp = a.p - b.p;
    return dy * dy + dp * dp;
}

// Signed area of the quad in trace order via the shoelace formula
static float shoelace(const Vec2 q[CORNER_COUNT])
{
    float s = 0.0f;
    for (uint8_t i = 0; i < CORNER_COUNT; ++i)
    {
        const Vec2 &a = q[i];
        const Vec2 &b = q[(i + 1) % CORNER_COUNT];
        s += a.y * b.p - b.y * a.p;
    }
    return 0.5f * s;
}

// Convex and non-self-intersecting iff every consecutive edge cross product
// shares a sign. The small epsilon ignores near-collinear corners.
static bool isConvex(const Vec2 q[CORNER_COUNT])
{
    bool seenPos = false;
    bool seenNeg = false;
    for (uint8_t i = 0; i < CORNER_COUNT; ++i)
    {
        const Vec2 &a = q[i];
        const Vec2 &b = q[(i + 1) % CORNER_COUNT];
        const Vec2 &c = q[(i + 2) % CORNER_COUNT];
        float ex = b.y - a.y, ey = b.p - a.p;
        float fx = c.y - b.y, fy = c.p - b.p;
        float cross = ex * fy - ey * fx;
        if (cross > 0.0001f)
            seenPos = true;
        if (cross < -0.0001f)
            seenNeg = true;
    }
    return seenPos != seenNeg;
}

// Longest/shortest edge length ratio (returns huge on a zero-length edge).
static float aspectRatio(const Vec2 q[CORNER_COUNT])
{
    float minLen2 = 1e30f;
    float maxLen2 = 0.0f;
    for (uint8_t i = 0; i < CORNER_COUNT; ++i)
    {
        float d2 = dist2(q[i], q[(i + 1) % CORNER_COUNT]);
        if (d2 < minLen2)
            minLen2 = d2;
        if (d2 > maxLen2)
            maxLen2 = d2;
    }
    if (minLen2 < 1e-6f)
        return 1e30f;
    return sqrtf(maxLen2 / minLen2);
}

// ---- Stability ring buffer -----------------------------------------------
// Last STABILITY_SAMPLES orientation samples used to gate each tap
static Vec2 gRing[STABILITY_SAMPLES];
static uint8_t gRingHead = 0;
static bool gRingPrimed = false;

static void ringReset()
{
    gRingHead = 0;
    gRingPrimed = false;
}

static void ringPush(float heading, float roll)
{
    if (!gRingPrimed)
    {
        // Prime the whole buffer with the first sample so std-dev starts at zero
        for (uint8_t i = 0; i < STABILITY_SAMPLES; ++i)
        {
            gRing[i].y = heading;
            gRing[i].p = roll;
        }
        gRingHead = 0;
        gRingPrimed = true;
        return;
    }
    gRing[gRingHead].y = heading;
    gRing[gRingHead].p = roll;
    gRingHead = (gRingHead + 1) % STABILITY_SAMPLES;
}

static Vec2 ringMean()
{
    float sy = 0.0f, sp = 0.0f;
    for (uint8_t i = 0; i < STABILITY_SAMPLES; ++i)
    {
        sy += gRing[i].y;
        sp += gRing[i].p;
    }
    return Vec2{sy / STABILITY_SAMPLES, sp / STABILITY_SAMPLES};
}

// worst of the two per-axis standard deviations over the ring
static float ringWorstStd()
{
    Vec2 m = ringMean();
    float vy = 0.0f, vp = 0.0f;
    for (uint8_t i = 0; i < STABILITY_SAMPLES; ++i)
    {
        float dy = gRing[i].y - m.y;
        float dp = gRing[i].p - m.p;
        vy += dy * dy;
        vp += dp * dp;
    }
    float stdY = sqrtf(vy / STABILITY_SAMPLES);
    float stdP = sqrtf(vp / STABILITY_SAMPLES);
    return stdY > stdP ? stdY : stdP;
}

// Returns nullptr if the finished quad is usable. Else why it was rejected.
static const char *validateQuad(const Vec2 q[CORNER_COUNT])
{
    if (!isConvex(q))
    {
        return "shape not convex (corners traced out of order?)";
    }
    if (fabsf(shoelace(q)) < MIN_AREA_DEG2)
    {
        return "square too small";
    }
    if (aspectRatio(q) > MAX_ASPECT_RATIO)
    {
        return "shape too skewed";
    }
    return nullptr;
}

// forward bilinear map of the unit square onto the captured quad
// (BL=00, BR=10, TL=01, TR=11):
// H(u,v) = BL.y + gAy*u + gBy*v + gCy*u*v (roll likewise with gA/B/Cp)
// pointerUpdate() inverts this per packet
static void precomputeBilinear()
{
    const Vec2 &BL = gCorner[CORNER_BL];
    const Vec2 &BR = gCorner[CORNER_BR];
    const Vec2 &TL = gCorner[CORNER_TL];
    const Vec2 &TR = gCorner[CORNER_TR];

    gAy = BR.y - BL.y;
    gBy = TL.y - BL.y;
    gCy = BL.y - BR.y - TL.y + TR.y;

    gAp = BR.p - BL.p;
    gBp = TL.p - BL.p;
    gCp = BL.p - BR.p - TL.p + TR.p;
}

// ---- Public API ----------------------------------------------------------
void pointerBegin()
{
    gCalibrated = false;
    gHaveFilt = false;
    ringReset();
}

bool pointerCalibrated()
{
    return gCalibrated;
}

static void announce(uint8_t cornerIdx)
{
    Serial.print("calibration: point at the ");
    Serial.print(CORNER_LABEL[cornerIdx]);
    Serial.println(" corner and tap middle+thumb");
}

static void fail(const char *reason)
{
    Serial.print("calibration error: ");
    Serial.println(reason);
    Serial.println("restarting from the first corner");
    buzzerError();
}

// Block until every finger is released so a still-held cancel/click from a
// previous attempt does not read as a fresh tap. Bails out after the timeout.
static void waitForAllReleased()
{
    Serial.println("(release all fingers to begin)");
    uint32_t start = millis();
    GesturePacket pkt;
    while (millis() - start < RELEASE_WAIT_TIMEOUT_MS)
    {
        if (radioReceive(pkt) && pkt.version == PROTOCOL_VERSION)
        {
            if (pkt.contacts == 0)
            {
                return;
            }
        }
    }
}

bool pointerCalibrate()
{
    gCalibrated = false;
    gHaveFilt = false;

    for (;;)
    { // any rejection restarts the whole trace
        Serial.println("---- calibration: trace the screen rectangle ----");
        Serial.println("order: bottom-left -> top-left -> top-right -> bottom-right");
        Serial.println("tap middle+thumb at each corner; hold pinky+thumb to cancel");

        waitForAllReleased();
        buzzerPrompt();

        ringReset();
        bool haveYawRef = false;
        bool prevMiddle = false;
        uint32_t pinkyDownMs = 0; // 0 == pinky not currently held
        uint8_t cornerIdx = 0;
        uint32_t lastTapMs = millis();

        announce(cornerIdx);

        bool restart = false;
        while (cornerIdx < CORNER_COUNT && !restart)
        {
            GesturePacket pkt;
            if (!radioReceive(pkt))
            {
                if (millis() - lastTapMs > CORNER_TIMEOUT_MS)
                {
                    fail("timed out waiting for a tap");
                    restart = true;
                }
                continue;
            }
            if (pkt.version != PROTOCOL_VERSION)
            {
                continue;
            }

            if (!haveYawRef)
            {
                gYawRef = pkt.eulerX;
                haveYawRef = true;
            }
            // eulerX = heading (left/right), eulerY = roll (up/down); eulerZ is unused (see pointer.h)
            float heading = unwrap(pkt.eulerX, gYawRef);
            float roll = pkt.eulerY;
            ringPush(heading, roll);

            bool curMiddle = pkt.contacts & CONTACT_MIDDLE;
            bool curPinky = pkt.contacts & CONTACT_PINKY;
            bool middleEdge = curMiddle && !prevMiddle;
            prevMiddle = curMiddle;

            // cancel only on a continuous pinky hold of CANCEL_HOLD_MS
            if (curPinky)
            {
                if (pinkyDownMs == 0)
                {
                    pinkyDownMs = millis();
                }
                else if (millis() - pinkyDownMs >= CANCEL_HOLD_MS)
                {
                    fail("cancelled by the user");
                    restart = true;
                    continue;
                }
            }
            else
            {
                pinkyDownMs = 0;
            }

            if (!middleEdge)
            {
                if (millis() - lastTapMs > CORNER_TIMEOUT_MS)
                {
                    fail("timed out waiting for a tap");
                    restart = true;
                }
                continue;
            }

            // A tap landed - validate stability then separation
            if (ringWorstStd() > STABILITY_MAX_STD_DEG)
            {
                fail("hand was moving when you tapped");
                restart = true;
                continue;
            }

            Vec2 captured = ringMean();
            for (uint8_t i = 0; i < cornerIdx; ++i)
            {
                if (sqrtf(dist2(captured, gCorner[i])) < MIN_CORNER_SEPARATION_DEG)
                {
                    fail("corner too close to a previous one");
                    restart = true;
                    break;
                }
            }
            if (restart)
                continue;

            gCorner[cornerIdx] = captured;
            Serial.print("  captured ");
            Serial.print(CORNER_LABEL[cornerIdx]);
            Serial.print(": heading=");
            Serial.print(captured.y, 2);
            Serial.print("  roll=");
            Serial.println(captured.p, 2);
            buzzerCornerCaptured();

            ++cornerIdx;
            lastTapMs = millis();
            if (cornerIdx < CORNER_COUNT)
            {
                announce(cornerIdx);
                buzzerPrompt();
            }
        }

        if (restart)
        {
            delay(400); // let the error tone finish before re-prompting
            continue;
        }

        const char *reason = validateQuad(gCorner);
        if (reason != nullptr)
        {
            fail(reason);
            delay(400);
            continue;
        }

        precomputeBilinear();
        gCalibrated = true;

        Serial.println("calibration complete:");
        for (uint8_t i = 0; i < CORNER_COUNT; ++i)
        {
            Serial.print("  ");
            Serial.print(CORNER_LABEL[i]);
            Serial.print(": heading=");
            Serial.print(gCorner[i].y, 2);
            Serial.print("  roll=");
            Serial.println(gCorner[i].p, 2);
        }
        buzzerCalibrationDone();
        return true;
    }
}

ScreenPos pointerUpdate(float yaw, float roll)
{
    if (!gCalibrated)
    {
        return ScreenPos{0.0f, 0.0f};
    }

    float yRaw = unwrap(yaw, gYawRef);
    if (!gHaveFilt)
    {
        gYawFilt = yRaw;
        gRollFilt = roll;
        gHaveFilt = true;
    }
    else
    {
        float ey = yRaw - gYawFilt;
        float er = roll - gRollFilt;
        float a = ALPHA_MIN + (ALPHA_MAX - ALPHA_MIN) *
                                  clamp01(sqrtf(ey * ey + er * er) / ERR_REF_DEG);
        gYawFilt += a * ey;
        gRollFilt += a * er;
    }

    // Invert the bilinear map: solve A*v^2 + B*v + C = 0 for v in [0,1] then
    // back-substitute for u. (1e-5 epsilons guard float divisions on degenerate quads.)
    float dy = gYawFilt - gCorner[CORNER_BL].y;
    float dp = gRollFilt - gCorner[CORNER_BL].p;

    float A = gBp * gCy - gCp * gBy;
    float B = gBp * gAy - gAp * gBy + gCp * dy - gCy * dp;
    float C = gAp * dy - gAy * dp;

    float v;
    if (fabsf(A) < 1e-5f)
    {
        v = (fabsf(B) < 1e-5f) ? 0.0f : (-C / B); // parallelogram: linear
    }
    else
    {
        float disc = B * B - 4.0f * A * C;
        if (disc < 0.0f)
        {
            v = (fabsf(B) < 1e-5f) ? 0.0f : (-C / B); // outside the quad
        }
        else
        {
            float r = sqrtf(disc);
            float v1 = (-B + r) / (2.0f * A);
            float v2 = (-B - r) / (2.0f * A);
            bool in1 = (v1 >= 0.0f && v1 <= 1.0f);
            bool in2 = (v2 >= 0.0f && v2 <= 1.0f);

            if (in1 && !in2)
                v = v1;
            else if (in2 && !in1)
                v = v2;
            else
            { // both or neither in range: take the closer one
                float c1 = (v1 < 0.0f) ? -v1 : (v1 > 1.0f ? v1 - 1.0f : 0.0f);
                float c2 = (v2 < 0.0f) ? -v2 : (v2 > 1.0f ? v2 - 1.0f : 0.0f);
                v = (c1 <= c2) ? v1 : v2;
            }
        }
    }

    // back out u from whichever denominator is better conditioned.
    float denomY = gAy + gCy * v;
    float denomP = gAp + gCp * v;
    float u;
    if (fabsf(denomY) >= fabsf(denomP))
    {
        u = (fabsf(denomY) < 1e-5f) ? 0.0f : (dy - gBy * v) / denomY;
    }
    else
    {
        u = (fabsf(denomP) < 1e-5f) ? 0.0f : (dp - gBp * v) / denomP;
    }

    u = clamp01(u);
    v = clamp01(v);

    // v=0 is the bottom row; the host origin is top-left, so flip y
    return ScreenPos{u, 1.0f - v};
}
