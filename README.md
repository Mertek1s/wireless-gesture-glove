# Wireless Gesture Glove

Control your computer's mouse with one hand. The Wireless Gesture Glove tracks
hand orientation and fingertip touches and turns them into mouse movement and
clicks - no software required to install on the host.

## How it works

The system is split into two devices that communicate over a 2.4 GHz nRF24L01
radio link:

```
   GLOVE (ESP32)                          DONGLE (Arduino Leonardo)
   +------------------+                   +-----------------------+
   | BNO055 IMU       |                   | receive packet        |
   | finger touches   | -- nRF24 radio -> | map to cursor / clicks|
   | -> GesturePacket |                   | -> USB HID mouse      |
   +------------------+                   +-----------------------+
        on the hand                          plugged into the PC
```

- The **glove** carries an ESP32, a BNO055 IMU on the index finger, and touch
  contacts on the other fingertips. It senses orientation and touches, packs
  them into a `GesturePacket`, and transmits.
- The **dongle** is an Arduino Leonardo. It receives packets, maps hand
  orientation to cursor motion, and emits **USB HID mouse events**. Because the
  Leonardo's ATmega32U4 has native USB, the host sees a standard mouse - no
  drivers, no host software.

## Features

- Absolute "point where you want the cursor" control via a 9-axis IMU.
- Left and right click from fingertip touches.
- Slow (precision) pointer mode for fine control.
- Tracking pause/resume so you can rest your hand.
- Startup calibration that maps your comfortable hand range to the screen.
- Audio cues for calibration steps, mode changes, and status.
- Wireless battery-powered glove.

## Hardware

The full bill of materials, wiring tables, and schematics are in
[`docs/hardware.md`](docs/hardware.md).

| Device | Core parts |
|--------|------------|
| Glove  | ESP32 Dev Module, BNO055 IMU, nRF24L01+, 3.7 V LiPo + boost converter |
| Dongle | Arduino Leonardo (ATmega32U4), nRF24L01+ PA/LNA |

## Usage

### First-time calibration

On power-up the glove asks for a one-time calibration: trace a rectangle with your hand, following the screen edges: bottom-left -> top-left -> top-right -> bottom-right. This maps your comfortable range of motion onto the screen area.

### Gestures

| Gesture                               | Action |
|---------------------------------------|--------|
| Point with the index finger           | Move the cursor |
| Middle fingertip to thumb             | Left click |
| Ring fingertip to thumb               | Right click |
| Pinky fingertip to thumb, single tap  | Toggle slow (precision) mode |
| Pinky fingertip to thumb, hold ≥ 1.5s | Pause tracking (free your hand) |
| Pinky single tap while paused         | Resume tracking |
| Pinky hold ≥ 3s while paused          | Recalibrate |

## Repository layout

```
platformio.ini - product firmware project (glove + dongle environments)
src/glove/     - ESP32 glove firmware
src/dongle/    - Arduino Leonardo dongle firmware
lib/           - shared code (radio packet definition)
experiments/   - component bench tests (separate PlatformIO project)
```

## Build & flash

Built with [PlatformIO](https://platformio.org/). With the PlatformIO Core CLI:

```
pio run                      # build both firmwares
pio run -e glove -t upload   # flash the glove (ESP32)
pio run -e dongle -t upload  # flash the dongle (Leonardo)
```

Component bench tests live in their own project - see
[`experiments/README.md`](experiments/README.md).

## License

Released under the [MIT License](LICENSE).
