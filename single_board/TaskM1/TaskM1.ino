
#include <Wire.h>

// ------------------------- Pin assignments ---------------------------------
const uint8_t PIN_IR         = 2;   
const uint8_t PIN_TOUCH      = 8;   
const uint8_t PIN_VIBRATION  = 9;  
const uint8_t PIN_ALARM_LED  = 13;  
const uint8_t PIN_STATUS_LED = 12;  
const uint8_t PIN_BUZZER     = 11; 

const bool BUZZER_IS_ACTIVE = true;
const uint16_t BUZZER_FREQ  = 2000;

const uint8_t BH1750_ADDR    = 0x23; 

volatile bool     irFlag        = false; 
volatile bool     touchFlag     = false; 
volatile bool     vibrationFlag = false; 
volatile uint32_t tickCount     = 0;     
volatile bool     timerFlag     = false; 

volatile uint8_t  prevPINB      = 0;

bool  systemArmed = false;   
bool  alarmActive = false;   
bool  buzzerOn    = false;   
float lightLux    = 0.0;

volatile uint32_t lastTouchMs     = 0;
volatile uint32_t lastVibrationMs = 0;
const uint32_t DEBOUNCE_MS = 200;

uint32_t touchHandledMs   = 0;
const uint32_t TAMPER_SUPPRESS_MS = 1500;

//----------------------------------INterrupt_functios---------------------------------------------

//Interupt function for when IR sensor pciks up somehting
void isrObstacle() 
{
  irFlag = true;                
}

//Interuppt function for when a touch or a vibration occurs(both sensors share this ISR function)
ISR(PCINT0_vect) //PCINT0_vect means port b, PCINT1_vect means port C and PCINT2_vect means port d. The physical port, not register
{
  uint8_t  pinb    = PINB;      // Copy the current HIGH/LOW states of all Port B pins

// Compare the current pin states with the previous states.
// Bits become 1 wherever a pin changed
  uint8_t  changed = pinb ^ prevPINB;  //XOR

// Save the current pin states so they can be used as the "previous" states
// during the next pin-change interrupt.
  prevPINB         = pinb;      
  uint32_t now     = millis(); 


//If D8 changed to HIGH:
    //touchFlag = true
  if ((changed & 0b00000001) && (pinb & 0b00000001)) 
  {
    if (now - lastTouchMs > DEBOUNCE_MS) {
      touchFlag   = true;
      lastTouchMs = now;
    }
  }

  //If D9 changed to LOW:
    //vibrationFlag = true
  if ((changed & 0b00000010) && !(pinb & 0b00000010)) {
    if (now - lastVibrationMs > DEBOUNCE_MS) {
      vibrationFlag   = true;
      lastVibrationMs = now;
    }
  }
}


//Interupt function for when 1 second passes
ISR(TIMER1_COMPA_vect) 
{
  tickCount++;
  timerFlag = true;             
}


//----------------------------------------------------------------------Setup---------------------------------------------------
void setup() 
{
  Serial.begin(9600);
  while (!Serial) { ; }         


  pinMode(PIN_IR,        INPUT);
  pinMode(PIN_TOUCH,     INPUT);
  pinMode(PIN_VIBRATION, INPUT);
  pinMode(PIN_ALARM_LED,  OUTPUT);
  pinMode(PIN_STATUS_LED, OUTPUT);
  pinMode(PIN_BUZZER,     OUTPUT);

  Wire.begin();
  bh1750Begin();


  attachInterrupt(digitalPinToInterrupt(PIN_IR), isrObstacle, FALLING);


  configurePinChangeInterrupts();


  configureTimer1_1Hz();

  Serial.println(F("=== SIT315 M1 Sense-Think-Act (4/4) online ==="));
  Serial.println(F("D2=FC-51 IR (INT0)  D8=TOUCH (PCI)  D9=VIBRATION (PCI)  I2C=BH1750"));
  Serial.println(F("System starts DISARMED. Touch the pad to arm."));
  Serial.println(F("While the alarm is ringing, touch silences it (stays armed).\n"));
}



/// -----------------------------------------------------------------------CONfig-----------------------------------------------


void configurePinChangeInterrupts() 
{
  prevPINB = PINB;

  PCICR |= 0b00000001;// enable group 0 for pin change interrupt
  PCMSK0 |= 0b00000011; //Select which of the pins in the group can actually trigger interuppt. 
}

//T
void configureTimer1_1Hz() 
{
  noInterrupts();

  TCCR1A = 0b00000000;  // clear Timer1 control register A
  TCCR1B = 0b00000000;  // clear Timer1 control register B
  TCNT1  = 0;           // start counting from zero

  OCR1A = 15624;        // compare value for 1-second interval

  TCCR1B |= 0b00001000; // enable CTC mode: WGM12 = 1
  TCCR1B |= 0b00000101; // prescaler 1024: CS12 = 1, CS10 = 1

  TIMSK1 |= 0b00000010; // enable Timer1 Compare-A interrupt

  interrupts();
}



//----------------------------------------------loop---------------------------------------------------------------

void loop() {
  handleEvents(); // handles flag changes
  handlePeriodic();   //handles periodic events
  updateActuators();  
}



//-------------------------------------------------------fucntion inside loop---------------------------------------------

void handleEvents() {
  if (touchFlag==true) 
  {

    //Set the flag to false again
    touchFlag = false;   
    
    
    touchHandledMs = millis(); 
    if (alarmActive==true) 
    {
      alarmActive = false; //Set the alarmActive flag to false or OFF
      Serial.println(F("[EVENT] TOUCH (D8) -> alarm silenced, system stays ARMED"));
    } else 
    {
      systemArmed = !systemArmed; //toggle systemArmed flag
      Serial.print(F("[EVENT] TOUCH (D8) -> system "));
      Serial.println(systemArmed ? F("ARMED") : F("DISARMED"));
    }
  }

  
  if (vibrationFlag==true) 
  {

    //Set the flag to false again
    vibrationFlag = false;


    if (millis() - touchHandledMs < TAMPER_SUPPRESS_MS) //if the vibration is 1.5s of a touch, then move on. 
    {
      Serial.println(F("[EVENT] VIBRATION (D9) -> suppressed (own touch)")); 
    } 
    else //else run the actual if-else condition
    {
      Serial.println(F("[EVENT] VIBRATION (D9) -> tamper detected"));
      if (systemArmed==true) //if system is armed
      {
        alarmActive = true; //then make alarm active
        Serial.println(F("        -> ARMED + tamper => ALARM ON"));
      } else 
      {
        Serial.println(F("        -> ignored (system disarmed)"));
      }
    }
  }

  if (irFlag==true) 
  {

    //Set the flag to false again
    irFlag = false;


    Serial.println(F("[EVENT] OBSTACLE on D2 (INT0) -> intrusion"));
    if (systemArmed) 
    {
      alarmActive = true;
      Serial.println(F("        -> ARMED + intrusion => ALARM ON"));
    } else {
      Serial.println(F("        -> ignored (system disarmed)"));
    }
  }
}



void handlePeriodic() 
{
  if (!timerFlag) return;            // nothing to do until a second elapses
  timerFlag = false;

  // Safely copy the multi-byte shared counter (disable IRQs for the read).
  uint32_t ticks;
  noInterrupts();
  ticks = tickCount; // if isr runs then tickcount may get stuck, so uses stable local copy
  interrupts();
  
  lightLux = bh1750ReadLux();

  digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));

  buzzerOn = !buzzerOn;

  Serial.print(F("[TIMER ")); Serial.print(ticks);
  Serial.print(F("s] armed=")); Serial.print(systemArmed ? F("Y") : F("N"));
  Serial.print(F(" alarm="));   Serial.print(alarmActive ? F("Y") : F("N"));
  Serial.print(F(" light="));   Serial.print(lightLux); //prints the lux
  Serial.println(F(" lx"));
}

// ------------------------- ACT --------------------------------------------
void updateActuators() 
{
  digitalWrite(PIN_ALARM_LED, alarmActive ? HIGH : LOW);

  // Buzzer beeps in time with the 1 Hz tick while the alarm is active, and is
  // silent otherwise. Pulsing carries much better than a continuous tone.
  bool shouldSound = alarmActive && buzzerOn;

  if (BUZZER_IS_ACTIVE) {
    digitalWrite(PIN_BUZZER, shouldSound ? HIGH : LOW);
  } else {
    // tone() drives Timer2 - deliberately NOT Timer1, so it cannot disturb the
    // 1 Hz periodic interrupt this program depends on.
    if (shouldSound) tone(PIN_BUZZER, BUZZER_FREQ);
    else             noTone(PIN_BUZZER);
  }
}


// ------------------------- helper BH1750 fucntions ------------------------------


void bh1750Begin() {
  Wire.beginTransmission(BH1750_ADDR);
  Wire.write(0x01);            // power on
  Wire.endTransmission();
  Wire.beginTransmission(BH1750_ADDR);
  Wire.write(0x10);            // continuous high-resolution mode
  Wire.endTransmission();
  delay(200);                  // settle time - safe here, we are still in setup()
}

float bh1750ReadLux() {
  if (Wire.requestFrom(BH1750_ADDR, (uint8_t)2) != 2) return -1.0;
  uint16_t raw = ((uint16_t)Wire.read() << 8) | Wire.read();
  return raw / 1.2;            // datasheet conversion
}
