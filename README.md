# Blind-Aid Project 🦯

A head-worn **rotating ultrasonic obstacle detector** for blind and visually impaired people. An HC-SR04 sensor sits on a stepper motor shaft. When something gets too close, the device tells the wearer to **stop**, sweeps a full circle to look around, and then signals **which direction is free** using buzzer beep codes and LEDs.

> Built by Ayushman Sengupta as a learning project in electronics and embedded software.
> **Scope:** this covers the front-facing sensing and alert part only 

## Demo

`BLIND_AID_PROJECT_DEMONSTRATION[1].mp4` is in this repo. The full project report is in `Blind_Aid_Project_Ayushman_Sengupta.docx`.

## How it works

1. The motor holds still with the sensor facing forward.
2. If something is closer than `TRIGGER_CM`, the **STOP signal** plays: rapid high-pitched beeps and both LEDs flash.
3. The sensor sweeps the full circle, measuring every 18°. It uses the median of 3 readings per spot.
4. If the front clears during the scan (someone just walked past), it **does nothing** and keeps listening.
5. Otherwise it picks the free direction and plays the code below.
6. It then waits for the path in front to clear before it listens again, so it alerts once per obstacle.

### Signals

| Signal | Meaning | LEDs |
|---|---|---|
| Rapid high beeps | **STOP** (obstacle ahead, scanning) | both flash |
| 1 beep | **Left** is free | left on |
| 2 beeps | **Right** is free | right on |
| 3 beeps | **Behind** is free | both on |
| 4 long low beeps | **Blocked** (nothing free) | alternate |

If both left and right are free, the side with more clearance wins. A near tie goes to the wider gap, and a final tie goes left (`TIE_GOES_LEFT`).

## Hardware

| Part | Notes |
|---|---|
| Arduino Uno R4 Minima | 5V logic |
| CNC Shield V3 | stepper driver carrier |
| A4988 driver | in the **X** slot, microstepping jumpers **empty** (full step) |
| NEMA 17 stepper (17HS4401) | sensor taped to the shaft |
| HC-SR04 ultrasonic sensor | 100 µF capacitor across VCC/GND recommended |
| Passive buzzer | driven with a 2 kHz square wave |
| 2 × LED + 2 × 470 Ω resistor | left / right indicators |
| 12 V DC adapter + female DC jack | to the shield screw terminal |
| NPN transistor (e.g. 2N2222) + 1 kΩ + 1N4148 | optional, recommended buzzer driver |

## Wiring

| Part | Connects to |
|---|---|
| Motor driver (X) | STEP = D2, DIR = D5, EN = D8 (built into the shield) |
| HC-SR04 | Trig = D9, Echo = D10, VCC = 5V, GND = GND |
| Buzzer | signal = D11, other leg = GND |
| Left LED | A0 → 470 Ω → LED long leg, short leg → GND |
| Right LED | A1 → 470 Ω → LED long leg, short leg → GND |
| Motor power | 12 V adapter → shield screw terminal (**+ to +, − to −**) |

The full diagram is in `circuit_diagram.pdf`. 


## Setup

1. Wire everything as above **with 12 V disconnected**.
2. **Set the A4988 current limit.** `Vref = I × 8 × Rsense`. With R068 sense resistors, 0.65 V gives about 1.2 A. Measure on the driver's potentiometer with USB powered, and turn it with a tiny screwdriver.
3. Point the sensor **straight ahead** before powering on, because that position becomes 0.
4. Open `firmware/blind_aid.ino` in the Arduino IDE, select **Arduino UNO R4 Minima**, and upload.
5. Connect 12 V, and open the Serial Monitor at 9600 baud to watch the logs.

### Settings to tweak (top of the sketch)

| Setting | Default | Meaning |
|---|---|---|
| `TRIGGER_CM` | 80 | obstacle closer than this starts a scan (use 20–40 for desk testing) |
| `FREE_CM` | 40 | a direction counts as free if nothing is closer |
| `LEFT_DIR` | HIGH | flip if left and right come out swapped |
| `STEP_US` | 2000 | raise to 2500 if the motor skips steps |
| `ALERT_FACING_FREE` | false | true = turn the sensor toward the free spot |
| `RUN_SELFTEST` | false | true = play every signal once at power-up |

## ⚠️ Safety

- **Never plug or unplug the motor while 12 V is on.** It can kill the driver.
- **Check polarity.** Reversed 12 V destroys the A4988.
- Sparks mean a short circuit, so power off and recheck. A hot HC-SR04 means VCC and GND are swapped.
- Wire the buzzer through a transistor, because Uno pins give only about 8 mA.
- This is a prototype. **Do not rely on it as a primary mobility aid.**



- Front-mounted only: it covers one rotating sensor on the head.
- The ultrasonic sensor struggles with soft, angled or very thin objects.
- The motor wiring to a head-worn rig needs care so it doesn't tangle. The scan is limited to ±180° for this reason.

## Contributing

Issues and pull requests are welcome. Ideas: a battery-powered version, a haptic motor option, a smaller driver board, an obstacle height estimate.

