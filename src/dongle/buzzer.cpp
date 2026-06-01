#include <Arduino.h>

#include "buzzer.h"

// Passive buzzer pin. tone() drives any digital pin, so no PWM channel is
// reserved - on the ATmega32U4 tone() multiplexes through Timer 3, leaving
// millis() and delay() untouched.
static constexpr uint8_t BUZZER_PIN = 18;

void buzzerBegin()
{
    pinMode(BUZZER_PIN, OUTPUT);
    noTone(BUZZER_PIN);
}

void buzzerDongleReady()
{
    tone(BUZZER_PIN, 1000, 120);
    delay(140);
}

void buzzerGloveReady()
{
    tone(BUZZER_PIN, 1400, 80);
    delay(90);
    tone(BUZZER_PIN, 1900, 100);
    delay(120);
}

void buzzerPrompt()
{
    tone(BUZZER_PIN, 1200, 50);
    delay(60);
}

void buzzerCornerCaptured()
{
    tone(BUZZER_PIN, 2000, 80);
    delay(90);
}

void buzzerError()
{
    tone(BUZZER_PIN, 600, 120);
    delay(140);
    tone(BUZZER_PIN, 300, 200);
    delay(220);
}

void buzzerCalibrationDone()
{
    tone(BUZZER_PIN, 1500, 80);
    delay(90);
    tone(BUZZER_PIN, 2000, 80);
    delay(90);
    tone(BUZZER_PIN, 2500, 120);
    delay(140);
}
