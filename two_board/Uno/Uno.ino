#include <Wire.h>

// ================================================================
// SIT315 M1
// INTERRUPT-DRIVEN SENSE-THINK-ACT SYSTEM
//
// Arduino Uno
//
// D2  = FC-51 IR          INT0
// D8  = Touch             PCINT0 / PB0
// D9  = Vibration         PCINT1 / PB1
// D10 = Nano LDR signal   PCINT2 / PB2
//
// D11 = Buzzer
// D12 = PHYSICAL HEARTBEAT LED
// D13 = BUILT-IN L ALARM LED
//
// A4/A5 = BH1750
//
// Timer1 = 1 Hz
//
// ALL ISRs ONLY SET FLAGS.
// ================================================================


// ================================================================
// PINS
// ================================================================

const uint8_t PIN_IR = 2;
const uint8_t PIN_TOUCH = 8;
const uint8_t PIN_VIBRATION = 9;
const uint8_t PIN_NANO = 10;

const uint8_t PIN_BUZZER = 11;

const uint8_t PIN_HEARTBEAT_LED = 12;

const uint8_t PIN_ALARM_LED = 13;


// ================================================================
// BH1750
// ================================================================

const uint8_t BH1750_ADDR = 0x23;

float lightLux = 0.0;


// ================================================================
// ISR FLAGS
// ================================================================

volatile bool irFlag = false;

volatile bool portBFlag = false;

volatile bool timerFlag = false;


// ================================================================
// PORT B STATE
// ================================================================

uint8_t previousPINB = 0;


// ================================================================
// SYSTEM STATE
// ================================================================

bool systemArmed = false;

bool alarmActive = false;

bool nanoDark = false;


// Heartbeat LED state
bool heartbeatState = false;


// Buzzer pulse state
bool buzzerPhase = false;


// Timer count
uint32_t timerSeconds = 0;


// ================================================================
// DEBOUNCE
// ================================================================

const uint32_t TOUCH_DEBOUNCE_MS = 200;

const uint32_t VIBRATION_DEBOUNCE_MS = 100;

const uint32_t TAMPER_SUPPRESS_MS = 1500;


uint32_t lastTouchMs = 0;

uint32_t lastVibrationMs = 0;

uint32_t touchHandledMs = 0;


// ================================================================
// ISR - IR
// ================================================================

void isrIR()
{
  irFlag = true;
}


// ================================================================
// ISR - PORT B
//
// D8 / D9 / D10
// ================================================================

ISR(PCINT0_vect)
{
  portBFlag = true;
}


// ================================================================
// ISR - TIMER1
// ================================================================

ISR(TIMER1_COMPA_vect)
{
  timerFlag = true;
}


// ================================================================
// RAW PIN CHANGE INTERRUPT CONFIGURATION
// ================================================================

void configurePinChangeInterrupts()
{
  noInterrupts();


  previousPINB = PINB;


  // Clear pending Port B PCINT flag
  PCIFR = _BV(PCIF0);


  // D8  = PCINT0
  // D9  = PCINT1
  // D10 = PCINT2

  PCMSK0 =
      _BV(PCINT0) |
      _BV(PCINT1) |
      _BV(PCINT2);


  // Enable Port B PCI group
  PCICR |= _BV(PCIE0);


  interrupts();
}


// ================================================================
// RAW TIMER1 CONFIGURATION - 1 Hz
// ================================================================

void configureTimer1_1Hz()
{
  noInterrupts();


  TCCR1A = 0;
  TCCR1B = 0;

  TCNT1 = 0;


  // -------------------------------------------------------------
  // 16 MHz / 1024
  // = 15625 counts per second
  //
  // 0 -> 15624 = 15625 counts
  // -------------------------------------------------------------

  OCR1A = 15624;


  // Clear pending compare flag
  TIFR1 = _BV(OCF1A);


  // CTC mode
  // WGM12 = 1

  TCCR1B |= _BV(WGM12);


  // Prescaler 1024
  // CS12 = 1
  // CS10 = 1

  TCCR1B |=
      _BV(CS12) |
      _BV(CS10);


  // Enable Compare A interrupt
  TIMSK1 |= _BV(OCIE1A);


  interrupts();
}


// ================================================================
// BH1750
// ================================================================

void bh1750Begin()
{
  Wire.beginTransmission(BH1750_ADDR);

  Wire.write(0x01);

  Wire.endTransmission();


  Wire.beginTransmission(BH1750_ADDR);

  Wire.write(0x10);

  Wire.endTransmission();


  delay(200);
}


float bh1750ReadLux()
{
  if (
    Wire.requestFrom(
      BH1750_ADDR,
      (uint8_t)2
    ) != 2
  )
  {
    return -1.0;
  }


  uint16_t raw =
      ((uint16_t)Wire.read() << 8)
      |
      Wire.read();


  return raw / 1.2;
}


// ================================================================
// ALARM HELPER
// ================================================================

void triggerAlarm(const __FlashStringHelper *reason)
{
  Serial.print(F("        -> "));
  Serial.print(reason);


  if (systemArmed)
  {
    alarmActive = true;

    // Start buzzer immediately
    buzzerPhase = true;


    Serial.println(F(" => ALARM ON"));
  }
  else
  {
    Serial.println(
      F(" => ignored, system DISARMED")
    );
  }
}


// ================================================================
// IR
// ================================================================

void handleIR()
{
  if (!irFlag)
    return;


  noInterrupts();

  irFlag = false;

  interrupts();


  Serial.println(
    F("[EVENT] IR D2 -> intrusion")
  );


  triggerAlarm(F("IR intrusion"));
}


// ================================================================
// TOUCH
// ================================================================

void handleTouch()
{
  uint32_t now = millis();


  if (
    now - lastTouchMs
    <
    TOUCH_DEBOUNCE_MS
  )
  {
    return;
  }


  lastTouchMs = now;

  touchHandledMs = now;


  Serial.println(F("[EVENT] TOUCH D8"));


  // -------------------------------------------------------------
  // Alarm currently active:
  // touch silences it, but remains armed.
  // -------------------------------------------------------------

  if (alarmActive)
  {
    alarmActive = false;

    buzzerPhase = false;


    Serial.println(
      F("        -> alarm silenced; system stays ARMED")
    );

    return;
  }


  // -------------------------------------------------------------
  // Otherwise toggle arm/disarm.
  // -------------------------------------------------------------

  systemArmed = !systemArmed;


  Serial.print(F("        -> system "));

  Serial.println(
    systemArmed
      ? F("ARMED")
      : F("DISARMED")
  );
}


// ================================================================
// VIBRATION
// ================================================================

void handleVibration()
{
  uint32_t now = millis();


  if (
    now - lastVibrationMs
    <
    VIBRATION_DEBOUNCE_MS
  )
  {
    return;
  }


  lastVibrationMs = now;


  // Suppress vibration caused by touching panel

  if (
    now - touchHandledMs
    <
    TAMPER_SUPPRESS_MS
  )
  {
    Serial.println(
      F("[EVENT] VIBRATION D9 -> suppressed after touch")
    );

    return;
  }


  Serial.println(
    F("[EVENT] VIBRATION D9 -> tamper")
  );


  triggerAlarm(F("tamper"));
}


// ================================================================
// NANO / LDR
// ================================================================

void handleNano(bool dark)
{
  nanoDark = dark;


  if (dark)
  {
    Serial.println(
      F("[EVENT] NANO D10 -> DARK")
    );


    triggerAlarm(
      F("remote LDR event")
    );
  }
  else
  {
    Serial.println(
      F("[EVENT] NANO D10 -> BRIGHT")
    );
  }
}


// ================================================================
// SHARED PORT B PROCESSING
// ================================================================

void handlePortB()
{
  if (!portBFlag)
    return;


  uint8_t currentPINB;


  // Get current port state and clear flag

  noInterrupts();

  portBFlag = false;

  currentPINB = PINB;

  interrupts();


  // XOR shows which pins changed

  uint8_t changed =
      currentPINB ^ previousPINB;


  previousPINB = currentPINB;


  // -------------------------------------------------------------
  // D8 = PB0 = Touch
  //
  // LOW -> HIGH
  // -------------------------------------------------------------

  if (changed & _BV(PB0))
  {
    if (currentPINB & _BV(PB0))
    {
      handleTouch();
    }
  }


  // -------------------------------------------------------------
  // D9 = PB1 = Vibration
  //
  // YOUR tested sensor:
  //
  // resting   = LOW
  // vibration = HIGH
  //
  // LOW -> HIGH
  // -------------------------------------------------------------

  if (changed & _BV(PB1))
  {
    if (currentPINB & _BV(PB1))
    {
      handleVibration();
    }
  }


  // -------------------------------------------------------------
  // D10 = PB2 = Nano
  //
  // LOW  = BRIGHT
  // HIGH = DARK
  // -------------------------------------------------------------

  if (changed & _BV(PB2))
  {
    bool dark =
        (currentPINB & _BV(PB2)) != 0;


    handleNano(dark);
  }
}


// ================================================================
// TIMER1 PERIODIC EVENT
// ================================================================

void handleTimer()
{
  if (!timerFlag)
    return;


  noInterrupts();

  timerFlag = false;

  interrupts();


  timerSeconds++;


  // -------------------------------------------------------------
  // D12 PHYSICAL HEARTBEAT LED
  //
  // Toggle once every Timer1 event.
  // -------------------------------------------------------------

  heartbeatState = !heartbeatState;


  // -------------------------------------------------------------
  // Buzzer phase
  // -------------------------------------------------------------

  buzzerPhase = !buzzerPhase;


  // -------------------------------------------------------------
  // BH1750 read
  //
  // Happens OUTSIDE the Timer ISR.
  // -------------------------------------------------------------

  lightLux = bh1750ReadLux();


  // -------------------------------------------------------------
  // Serial status
  // -------------------------------------------------------------

  Serial.print(F("[TIMER "));
  Serial.print(timerSeconds);
  Serial.print(F("s] "));


  Serial.print(F("armed="));

  Serial.print(
    systemArmed ? F("Y") : F("N")
  );


  Serial.print(F(" alarm="));

  Serial.print(
    alarmActive ? F("Y") : F("N")
  );


  Serial.print(F(" nano="));

  Serial.print(
    nanoDark ? F("DARK") : F("BRIGHT")
  );


  Serial.print(F(" light="));


  if (lightLux < 0)
  {
    Serial.println(F("BH1750 ERROR"));
  }
  else
  {
    Serial.print(lightLux);

    Serial.println(F(" lx"));
  }
}


// ================================================================
// ACTUATORS
// ================================================================

void updateActuators()
{
  // -------------------------------------------------------------
  // D12 physical LED = HEARTBEAT
  // -------------------------------------------------------------

  digitalWrite(
    PIN_HEARTBEAT_LED,
    heartbeatState ? HIGH : LOW
  );


  // -------------------------------------------------------------
  // D13 built-in L LED = ALARM
  // -------------------------------------------------------------

  digitalWrite(
    PIN_ALARM_LED,
    alarmActive ? HIGH : LOW
  );


  // -------------------------------------------------------------
  // D11 buzzer
  // -------------------------------------------------------------

  bool sound =
      alarmActive &&
      buzzerPhase;


  digitalWrite(
    PIN_BUZZER,
    sound ? HIGH : LOW
  );
}


// ================================================================
// SETUP
// ================================================================

void setup()
{
  Serial.begin(115200);


  // -------------------------------------------------------------
  // INPUTS
  // -------------------------------------------------------------

  pinMode(PIN_IR, INPUT);

  pinMode(PIN_TOUCH, INPUT);

  pinMode(PIN_VIBRATION, INPUT);

  pinMode(PIN_NANO, INPUT);


  // -------------------------------------------------------------
  // OUTPUTS
  // -------------------------------------------------------------

  pinMode(PIN_BUZZER, OUTPUT);

  pinMode(PIN_HEARTBEAT_LED, OUTPUT);

  pinMode(PIN_ALARM_LED, OUTPUT);


  digitalWrite(PIN_BUZZER, LOW);

  digitalWrite(PIN_HEARTBEAT_LED, LOW);

  digitalWrite(PIN_ALARM_LED, LOW);


  // -------------------------------------------------------------
  // I2C
  // -------------------------------------------------------------

  Wire.begin();

  bh1750Begin();


  // -------------------------------------------------------------
  // IR external interrupt
  //
  // FC-51:
  // normal    = HIGH
  // detection = LOW
  //
  // therefore FALLING.
  // -------------------------------------------------------------

  attachInterrupt(
    digitalPinToInterrupt(PIN_IR),
    isrIR,
    FALLING
  );


  // Raw-register interrupts

  configurePinChangeInterrupts();

  configureTimer1_1Hz();


  // Initial Nano state

  nanoDark =
      digitalRead(PIN_NANO) == HIGH;


  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F(" SIT315 INTERRUPT SECURITY SYSTEM"));
  Serial.println(F("========================================"));

  Serial.println(F("D2  = IR"));
  Serial.println(F("D8  = Touch"));
  Serial.println(F("D9  = Vibration"));
  Serial.println(F("D10 = Nano/LDR"));

  Serial.println(F("D11 = Buzzer"));
  Serial.println(F("D12 = Physical HEARTBEAT LED"));
  Serial.println(F("D13 = Built-in ALARM LED"));

  Serial.println();

  Serial.println(F("Timer1 = 1 Hz"));

  Serial.println();

  Serial.println(F("System starts DISARMED."));
  Serial.println(F("Touch to ARM."));
  Serial.println();
}


// ================================================================
// LOOP
// ================================================================

void loop()
{
  // SENSE / THINK

  handleIR();

  handlePortB();

  handleTimer();


  // ACT

  updateActuators();
}