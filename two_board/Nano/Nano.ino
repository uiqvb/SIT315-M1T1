#include <Arduino.h>
#include "sam.h"

// ================================================================
// NANO 33 IoT - LDR WORKER NODE
//
// A0 = LDR analogue input
// D4 = digital DARK/BRIGHT signal to Uno D10
//
// TC3 = 100 Hz internal hardware timer
//
// ISR ONLY:
//   clears timer interrupt
//   sets sampleFlag
//
// ADC reading and decisions happen in loop().
// ================================================================

const uint8_t PIN_LDR = A0;
const uint8_t PIN_UNO_SIGNAL = 4;

volatile bool sampleFlag = false;


// ---------------- LDR state ----------------

bool darkState = false;


// ---------------- Calibration ----------------

bool calibrating = true;

uint32_t calibrationTotal = 0;
uint16_t calibrationSamples = 0;

const uint16_t CALIBRATION_COUNT = 100;

uint16_t baseline = 0;
uint16_t darkThreshold = 0;
uint16_t brightThreshold = 0;


// ---------------- Stability filtering ----------------

uint8_t darkCounter = 0;
uint8_t brightCounter = 0;

const uint8_t REQUIRED_SAMPLES = 5;


// ---------------- Serial status ----------------

uint8_t printCounter = 0;


// ================================================================
// TC3 ISR
// ================================================================

void TC3_Handler()
{
  // Required hardware acknowledgement
  TC3->COUNT16.INTFLAG.reg = TC_INTFLAG_MC0;

  // Flag only
  sampleFlag = true;
}


// ================================================================
// RAW TC3 CONFIGURATION - 100 Hz
// ================================================================

void configureTC3_100Hz()
{
  // Enable TC3 peripheral bus
  PM->APBCMASK.reg |= PM_APBCMASK_TC3;


  // Feed GCLK0 to TC3
  GCLK->CLKCTRL.reg =
      GCLK_CLKCTRL_CLKEN |
      GCLK_CLKCTRL_GEN_GCLK0 |
      GCLK_CLKCTRL_ID(GCM_TCC2_TC3);

  while (GCLK->STATUS.bit.SYNCBUSY);


  // Reset TC3
  TC3->COUNT16.CTRLA.reg = TC_CTRLA_SWRST;

  while (TC3->COUNT16.STATUS.bit.SYNCBUSY);
  while (TC3->COUNT16.CTRLA.bit.SWRST);


  // 16-bit mode
  // Match Frequency mode
  // Prescaler 64
  //
  // 48 MHz / 64 = 750000 Hz
  // 750000 / 100 = 7500
  // CC0 = 7499

  TC3->COUNT16.CTRLA.reg =
      TC_CTRLA_MODE_COUNT16 |
      TC_CTRLA_WAVEGEN_MFRQ |
      TC_CTRLA_PRESCALER_DIV64;

  while (TC3->COUNT16.STATUS.bit.SYNCBUSY);


  TC3->COUNT16.COUNT.reg = 0;

  while (TC3->COUNT16.STATUS.bit.SYNCBUSY);


  TC3->COUNT16.CC[0].reg = 7499;

  while (TC3->COUNT16.STATUS.bit.SYNCBUSY);


  // Clear old interrupt
  TC3->COUNT16.INTFLAG.reg = TC_INTFLAG_MC0;

  // Enable compare interrupt
  TC3->COUNT16.INTENSET.reg = TC_INTENSET_MC0;


  // NVIC
  NVIC_DisableIRQ(TC3_IRQn);
  NVIC_ClearPendingIRQ(TC3_IRQn);
  NVIC_SetPriority(TC3_IRQn, 3);
  NVIC_EnableIRQ(TC3_IRQn);


  // Start TC3
  TC3->COUNT16.CTRLA.bit.ENABLE = 1;

  while (TC3->COUNT16.STATUS.bit.SYNCBUSY);
}


// ================================================================
// CHANGE LIGHT STATE
// ================================================================

void changeLightState(bool newDarkState)
{
  if (newDarkState == darkState)
    return;


  darkState = newDarkState;


  if (darkState)
  {
    // DARK -> Uno receives HIGH
    digitalWrite(PIN_UNO_SIGNAL, HIGH);

    Serial.println(
      F("[NANO EVENT] DARK -> D4 HIGH -> Uno D10")
    );
  }
  else
  {
    // BRIGHT -> Uno receives LOW
    digitalWrite(PIN_UNO_SIGNAL, LOW);

    Serial.println(
      F("[NANO EVENT] BRIGHT -> D4 LOW")
    );
  }
}


// ================================================================
// PROCESS LDR
// ================================================================

void processLDR()
{
  uint16_t reading = analogRead(PIN_LDR);


  // --------------------------------------------------------------
  // First second = calibration
  // KEEP LDR UNCOVERED
  // --------------------------------------------------------------

  if (calibrating)
  {
    calibrationTotal += reading;

    calibrationSamples++;


    if (calibrationSamples >= CALIBRATION_COUNT)
    {
      baseline =
          calibrationTotal / CALIBRATION_COUNT;


      // DARK below 70% of baseline
      darkThreshold =
          (uint32_t)baseline * 70 / 100;


      // Return to BRIGHT above 85%
      brightThreshold =
          (uint32_t)baseline * 85 / 100;


      calibrating = false;

      darkState = false;

      digitalWrite(PIN_UNO_SIGNAL, LOW);


      Serial.println();
      Serial.println(F("=== CALIBRATION COMPLETE ==="));

      Serial.print(F("Baseline = "));
      Serial.println(baseline);

      Serial.print(F("Dark threshold = "));
      Serial.println(darkThreshold);

      Serial.print(F("Bright threshold = "));
      Serial.println(brightThreshold);

      Serial.println();
      Serial.println(F("Cover LDR to trigger DARK."));
      Serial.println();
    }

    return;
  }


  // --------------------------------------------------------------
  // BRIGHT -> DARK
  // --------------------------------------------------------------

  if (!darkState)
  {
    if (reading < darkThreshold)
    {
      darkCounter++;


      if (darkCounter >= REQUIRED_SAMPLES)
      {
        darkCounter = 0;

        changeLightState(true);
      }
    }
    else
    {
      darkCounter = 0;
    }
  }


  // --------------------------------------------------------------
  // DARK -> BRIGHT
  // --------------------------------------------------------------

  else
  {
    if (reading > brightThreshold)
    {
      brightCounter++;


      if (brightCounter >= REQUIRED_SAMPLES)
      {
        brightCounter = 0;

        changeLightState(false);
      }
    }
    else
    {
      brightCounter = 0;
    }
  }


  // Serial diagnostic every ~0.5 seconds

  printCounter++;


  if (printCounter >= 50)
  {
    printCounter = 0;


    Serial.print(F("[NANO] LDR="));
    Serial.print(reading);

    Serial.print(F(" state="));

    Serial.println(
      darkState ? F("DARK") : F("BRIGHT")
    );
  }
}


// ================================================================
// SETUP
// ================================================================

void setup()
{
  Serial.begin(115200);

  analogReadResolution(10);


  pinMode(PIN_UNO_SIGNAL, OUTPUT);

  // Start BRIGHT / no-event
  digitalWrite(PIN_UNO_SIGNAL, LOW);


  Serial.println();
  Serial.println(F("================================"));
  Serial.println(F(" NANO 33 IoT LDR WORKER NODE"));
  Serial.println(F("================================"));

  Serial.println(F("A0 = LDR"));
  Serial.println(F("D4 = output to Uno D10"));
  Serial.println(F("TC3 = 100 Hz"));

  Serial.println();

  Serial.println(
    F("Leave LDR UNCOVERED for first second.")
  );

  Serial.println();


  configureTC3_100Hz();
}


// ================================================================
// LOOP
// ================================================================

void loop()
{
  bool doSample = false;


  noInterrupts();

  if (sampleFlag)
  {
    sampleFlag = false;

    doSample = true;
  }

  interrupts();


  if (doSample)
  {
    // ADC + decisions outside ISR
    processLDR();
  }
}