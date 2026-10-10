# SIT315 M1.T1: Interrupt-Driven Intrusion and Tamper Alarm

An interrupt-driven Sense-Think-Act security panel for SIT315 Concurrent and Distributed
Programming, Module 1. The repository holds two versions of the same system.

| Version | Folder | Boards | Sensors |
|---|---|---|---|
| Single board (first submission, 4/4) | `single_board/` | Arduino Uno | IR, touch, vibration, BH1750 light |
| Two board (upgrade) | `two_board/` | Arduino Uno + Arduino Nano 33 IoT | the four above, plus an LDR read by the Nano |

The single board version is kept exactly as it was first submitted, with its own
[README](single_board/README.md). Everything below describes the two board version.

---

## Demo videos

| Video | Link |
|---|---|
| Two board system | https://deakin.au.panopto.com/Panopto/Pages/Viewer.aspx?id=5f44e258-a0e5-4595-9141-b4df00968388 |
| Single board system (first submission) | to be added |

---

## What the two board version adds

The Uno still runs the alarm. A second board, a Nano 33 IoT, reads an analogue light sensor (LDR),
decides on its own whether the room is bright or dark, and reports that decision to the Uno as one
digital signal. From the Uno's side the Nano looks like a fourth digital sensor.

```
        NANO 33 IoT (worker)                         UNO (main)
   SAMD21, 48 MHz, 3.3 V logic               ATmega328P, 16 MHz, 5 V logic

   LDR --> A0                                 D2  <-- IR         (INT0)
           |                                  D8  <-- touch      (PCINT0)
   TC3 interrupt, 100 Hz                      D9  <-- vibration  (PCINT1)
           |                           +----> D10 <-- Nano       (PCINT2)
   read ADC in loop()                  |      A4/A5 <-> BH1750   (I2C)
   calibrate, threshold, filter        |
           |                           |      Timer1 interrupt, 1 Hz
           v                           |
          D4 --------------------------+      D11 --> buzzer
          LOW  = BRIGHT                       D12 --> heartbeat LED
          HIGH = DARK                         D13 --> alarm LED

          GND ------------------------------- GND
```

Only two wires join the boards: the signal wire and a common ground. The signal runs one way,
from the 3.3 V board to the 5 V board. A 3.3 V HIGH is above the ATmega328P input threshold, and
nothing at 5 V is ever driven into the Nano, whose pins are not 5 V tolerant.

---

## The Nano: analogue in, digital out

Sketch: `two_board/Nano/Nano.ino`

| Step | What happens |
|---|---|
| Sample clock | TC3 raises an interrupt 100 times a second. The handler clears the interrupt flag and sets `sampleFlag`. Nothing else. |
| Read | `loop()` sees the flag and calls `analogRead(A0)` (10 bit, 0 to 1023). |
| Calibrate | The first 100 samples (one second) are averaged into a baseline for the room it is sitting in. |
| Threshold | DARK when the reading falls below 70 % of the baseline. Back to BRIGHT only above 85 %. The gap stops the output flickering near one threshold. |
| Filter | A change needs 5 samples in a row (50 ms) on the far side of the threshold. One noisy sample does nothing. |
| Report | D4 is driven HIGH for DARK and LOW for BRIGHT. |

TC3 is set up through the SAMD21 registers directly:

```
48 MHz / 64 (prescaler) = 750 000 counts per second
750 000 / 100 Hz        = 7500 counts per interrupt
CC0                     = 7499
```

`PM->APBCMASK` switches the timer's bus clock on, `GCLK->CLKCTRL` feeds it the 48 MHz clock,
`TC3->COUNT16.CTRLA` selects 16 bit match-frequency mode with the prescaler, `INTENSET` enables
the compare interrupt, and the NVIC calls enable the interrupt line in the core.

---

## The Uno: four interrupt sources

Sketch: `two_board/Uno/Uno.ino`

| Interrupt | Trigger | What the handler does |
|---|---|---|
| `INT0` via `attachInterrupt` | D2 falling edge | `irFlag = true` |
| `PCINT0_vect` | any change on D8, D9 or D10 | `portBFlag = true` |
| `TIMER1_COMPA_vect` | every 1.000 s | `timerFlag = true` |

Every handler is one line. All reading, deciding, printing and I2C happens in `loop()`.

### Three pins on one vector

D8, D9 and D10 are all on Port B, so one pin-change vector serves all three:

```cpp
PCMSK0 = _BV(PCINT0) | _BV(PCINT1) | _BV(PCINT2);   // D8, D9, D10
PCICR |= _BV(PCIE0);                                // enable the Port B group
```

`handlePortB()` works out which pin moved by comparing the port against the last copy it kept:

```cpp
uint8_t changed = currentPINB ^ previousPINB;   // 1 in every bit that moved
```

Touch and vibration act only on a LOW to HIGH change of their own bit. The Nano pin acts on both
edges, because both directions carry information: HIGH means the room went dark, LOW means it
came back.

### Timer1

CTC mode, prescaler 1024, `OCR1A = 15624`:

```
16 000 000 / 1024 = 15 625 counts per second, counted as 0 to 15624
```

Once a second the main loop toggles the heartbeat LED, flips the buzzer phase, reads the BH1750
and prints a status line.

### Behaviour

| Action | Result |
|---|---|
| Touch pad (disarmed) | system arms |
| Touch pad (armed, no alarm) | system disarms |
| Touch pad (alarm sounding) | alarm silenced, stays armed |
| IR obstacle while armed | alarm on |
| Vibration while armed | alarm on (tamper) |
| Vibration within 1.5 s of a touch | logged as suppressed, no alarm |
| LDR covered while armed | Nano reports DARK, alarm on |
| LDR uncovered | Nano reports BRIGHT, logged |
| Any sensor while disarmed | logged, then ignored |

---

## Wiring

### Uno

| Component | Pin | Notes |
|---|---|---|
| FC-51 IR proximity sensor | D2 | INT0, FALLING. Idles HIGH, LOW on detection. |
| TTP223B touch pad | D8 | PCINT0. HIGH when touched. |
| SW-420 vibration switch | D9 | PCINT1. HIGH on vibration. |
| Nano signal | D10 | PCINT2. HIGH = DARK, LOW = BRIGHT. |
| Buzzer | D11 | Pulses with the 1 Hz tick while the alarm is on. |
| Heartbeat LED + resistor | D12 | Toggles once a second. |
| Alarm LED | D13 | The Uno's onboard LED. |
| BH1750 light sensor | A4 / A5 | I2C, address `0x23`. |

### Nano 33 IoT

| Component | Pin | Notes |
|---|---|---|
| LDR voltage divider | A0 | Reading falls as the light falls. |
| Signal to Uno | D4 | Goes to Uno D10. |
| Ground | GND | Joined to Uno GND. |

---

## Build and upload

Both sketches use only what ships with the board packages. No extra libraries.

Uno:

1. Open `two_board/Uno/Uno.ino` in the Arduino IDE.
2. Tools > Board > Arduino Uno, then pick its COM port.
3. Upload.
4. Serial Monitor at 115200 baud.

Nano 33 IoT:

1. Install "Arduino SAMD Boards" from the Boards Manager if it is not there already.
2. Open `two_board/Nano/Nano.ino`.
3. Tools > Board > Arduino Nano 33 IoT, then pick its COM port.
4. Upload.
5. Serial Monitor at 115200 baud.

Leave the LDR uncovered for the first second after the Nano starts. That second is the
calibration.

---

## Serial output

Uno:

```
[TIMER 12s] armed=Y alarm=N nano=BRIGHT light=41.67 lx
[EVENT] NANO D10 -> DARK
        -> remote LDR event => ALARM ON
[TIMER 13s] armed=Y alarm=Y nano=DARK light=40.83 lx
[EVENT] TOUCH D8
        -> alarm silenced; system stays ARMED
[EVENT] NANO D10 -> BRIGHT
```

Nano:

```
=== CALIBRATION COMPLETE ===
Baseline = 812
Dark threshold = 568
Bright threshold = 690

[NANO] LDR=809 state=BRIGHT
[NANO EVENT] DARK -> D4 HIGH -> Uno D10
[NANO] LDR=213 state=DARK
[NANO EVENT] BRIGHT -> D4 LOW
```

The numbers in these two blocks show the format. The lux and LDR values depend on the room.

---

## Repository layout

```
.
├── README.md
├── single_board/
│   ├── README.md              write-up of the first submission
│   ├── TaskM1/TaskM1.ino      Uno, three interrupt sources
│   └── images/
└── two_board/
    ├── Uno/Uno.ino            main board
    └── Nano/Nano.ino          LDR worker node
```

---

SIT315 Concurrent and Distributed Programming, Deakin University, Module 1, Task M1.T1
