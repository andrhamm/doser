#include <Button.h>        //https://github.com/JChristensen/Button
#include <avr/eeprom.h> 

#define PH_PIN 8
#define RELAY_PIN 3        //Relay for activating dosing
#define BUTTON_PIN 2       //Connect a tactile button switch (or something similar)
                           //from Arduino pin 2 to ground.
#define PULLUP true        //To keep things simple, we use the Arduino's internal pullup resistor.
#define INVERT false       //Since the pullup resistor will keep the pin high unless the
                           //switch is closed, this is negative logic, i.e. a high state
                           //means the button is NOT pressed. (Assuming a normally open switch.)
#define DEBOUNCE_MS 10     //A debounce time of 20 milliseconds usually works well for tactile button switches.
#define LED_PIN 13         //The standard Arduino "Pin 13" LED.
#define LONG_PRESS 500     //We define a "long press" to be 500 milliseconds.
#define BLINK_INTERVAL 50  //In the BLINK state, switch the LED every 50 milliseconds.

//EEPROM trigger check
#define WRITE_CHECK      0x1234 
#define LEOPHI_VERSION   0x0002


Button myBtn(BUTTON_PIN, PULLUP, INVERT, DEBOUNCE_MS);    //Declare the button

//The list of possible states for the state machine.
enum STATES {
  HELLO_VERSION,
  TO_PH, PH, 
  MANUAL_DOSE, 
  TO_CALFOUR, CALFOUR, 
  TO_CALSEVEN, CALSEVEN, 
  TO_READONLY, READONLY
};
uint8_t STATE;                   //The current state machine state
boolean readOnlyState;           //The readonly state (false = dont auto dose)
boolean ledState;                //The current LED status
unsigned long startMs;
unsigned long ms;                //The current time from millis()
unsigned long msLast;            //The last time the LED was switched     

unsigned long adcMillis = 0;       
int statusInterval = 1000;       // interval at which to blink or send updates(milliseconds)
int adcReadInterval = 20;        // Our ADC read routine should be cycling at about ~50hz (20ms)

const int adcMaxStep = 1023; //added this to allow for variable bit ranges (Remember Newer AVR adc architecture(e.g Atmega vs At90s) is meant for a divisor of 2^n-1!!!)
const float vRef = 5.00; //Set our voltage reference (what is out Voltage at the Vin (+) of the ADC in this case an atmega32u4)
const float opampGain = 5.25; //what is our Op-Amps gain (stage 1)

//Rolling average this should act as a digital filter (IE smoothing)
const int numPasses = 200; //Larger number = slower response but greater smoothing effect

int passes[numPasses];    //Our storage array
int passIndex = 0;          //what pass are we on? this one
long total = 0;              //running total
int pHSmooth = 0;           //Our smoothed pHRaw number

//pH calc globals
float milliVolts, pH; //using floats will transmit as 4 bytes over I2C

//Continuous reading flag
bool continousFlag,statusGFlag;


//Our parameter, for ease of use and eeprom access lets use a struct
struct parameters_T
{
  unsigned int WriteCheck;
  int pH7Cal, pH4Cal, pH10Cal;
  bool continous, statusLEDG;
  float pHStep;
} 
params;



void setup(void)
{
    Serial.begin(9600);
    
    pinMode(RELAY_PIN, OUTPUT);  //Set the relay pin as an output
    pinMode(LED_PIN, OUTPUT);    //Set the LED pin as an output
    pinMode(PH_PIN, INPUT);      //Set the PH probe pin as an input

    eeprom_read_block(&params, (void *)0, sizeof(params));
    continousFlag = params.continous;
    statusGFlag = params.statusLEDG;
    if (params.WriteCheck != WRITE_CHECK) {
      reset_Params();
    }
    // initialize smoothing variables to 0: 
    for (int thisPass = 0; thisPass < numPasses; thisPass++)
      passes[thisPass] = 0;
    
    startMs = 0;
    STATE = HELLO_VERSION;
}

void loop(void)
{
    ms = millis();               //record the current time
    myBtn.read();                //Read the button

    if (ms - adcMillis > adcReadInterval) {
      // save the last time you blinked the LED 
      adcMillis = ms;
  
      total = total - passes[passIndex];
      //grab our pHRaw this should pretty much always be updated due to our Oversample ISR
      //and place it in our passes array this mimics an analogRead on a pin
      digitalWrite(4, HIGH); 
      passes[passIndex] = smoothADCRead(PH_PIN);
      digitalWrite(4, LOW); //these will show our sampling fQ checking with scope!
      total = total + passes[passIndex];
      passIndex = passIndex + 1;
      //Now handle end of array and make our rolling average portion
      if (passIndex >= numPasses) {
        passIndex = 0;
      }
      
      pHSmooth = total/numPasses;
    }  
  
    switch (STATE) {
      case HELLO_VERSION:
        turnOnLED();
      
        if (startMs == 0) {
          startMs = ms;
        } 
        
        unsigned long sinceStartup;
        sinceStartup = ms - startMs;
        
        if (sinceStartup >= 6000) {
          STATE = TO_PH;
        }
        if (sinceStartup >= 4000) {
          // Display "v1.0"
          Serial.println("Display: v1.0");
        } 
        else if (sinceStartup >= 2000) {
          // Display "tHiS"
          Serial.println("Display: tHiS");
        } 
        else {
          // Display "dOSE"
          Serial.println("Display: dOSE");
        }
        
        break;
        
      case TO_PH:
        turnOffLED();
        if (myBtn.isReleased()) {
          Serial.println("Switching to PH view");
          STATE = PH;
        }
        break;

      // This is the main screen
      case PH:
        if (myBtn.wasReleased()) {
          STATE = TO_CALFOUR;
        } 
        else if (myBtn.pressedFor(LONG_PRESS)) {
          // Manual dosing for pump priming & testing
          STATE = MANUAL_DOSE;
        }
        else if (myBtn.isPressed()) {
          fastBlink();
        }
        else {
          // Display current pH. ex: "4.01"
          turnOffLED();
        }
        break;

      case MANUAL_DOSE:
        if (myBtn.isPressed()) {
          // Activate pump as long as button is held down
          Serial.println("Display: dOSE");
          // Display "dOSE"
          fastBlink();
        }
        else {
          // Deactivate pump
          STATE = TO_PH;
        }
        break;

      case TO_CALFOUR:
        turnOffLED();
        if (myBtn.wasReleased()) {
          Serial.println("Switching to CAL4 menu");
          STATE = CALFOUR;
        }
        break;

      case CALFOUR:
        if (myBtn.wasReleased()) {
          STATE = TO_CALSEVEN;
        }
        else if (myBtn.pressedFor(LONG_PRESS)) {
          // Execute 4.0 calibration
          Serial.println("Calibrating pH 4.0!");
          STATE = TO_CALFOUR;
        }
        else if (myBtn.isPressed()) {
          fastBlink();
        }
        else if (ms - myBtn.lastChange() >= 5000) {
          // after 5 seconds go to PH
          STATE = TO_PH;
        } else {
          // Display "CAL4"
          slowBlink();
        }
        break;

      case TO_CALSEVEN:
        turnOffLED();
        if (myBtn.wasReleased()) {
          Serial.println("Switching to CAL7 menu");
          STATE = CALSEVEN;
        }
        break;

      case CALSEVEN:
        if (myBtn.wasReleased()) {
          STATE = TO_READONLY;
        }
        else if (myBtn.pressedFor(LONG_PRESS)) {
          // Execute 7.0 calibration
          Serial.println("Calibrating pH 7.0!");
          STATE = TO_CALSEVEN;
        }
        else if (myBtn.isPressed()) {
          fastBlink();
        }
        else if (ms - myBtn.lastChange() >= 5000) {
          STATE = TO_PH;
        }
        else {
          // Display "CAL7"
          slowBlink();
        }
        break;

      case TO_READONLY:
        turnOffLED();
        if (myBtn.wasReleased()) {
          Serial.println("Switching to RO menu");
          STATE = READONLY;
        }
        break;

      case READONLY:
        if (myBtn.wasReleased() || ms - myBtn.lastChange() >= 5000) {
          STATE = TO_PH;
        }
        else if (myBtn.pressedFor(LONG_PRESS)) {
          Serial.println("Inverting RO mode");
          readOnlyState = !readOnlyState;
          STATE = TO_READONLY;
        }
        else if (myBtn.isPressed()) {
          fastBlink();
        }
        else {
          // Display current readonly state
           slowBlink();
        }
        break;
    }
}

//Reverse the current LED state. If it's on, turn it off. If it's off, turn it on.
void switchLED()
{
  msLast = ms;                 //record the last switch time
  ledState = !ledState;
  digitalWrite(LED_PIN, ledState);
}

void turnOffLED()
{
  msLast = ms;                 //record the last switch time
  ledState = false;
  digitalWrite(LED_PIN, ledState);
}

void turnOnLED()
{
  msLast = ms;                 //record the last switch time
  ledState = true;
  digitalWrite(LED_PIN, ledState);
}

//Switch the LED on and off every LONG_PRESS milliseconds.
void slowBlink()
{
  if (ms - msLast >= LONG_PRESS)
    switchLED();
}

//Switch the LED on and off every BLINK_INTERVAL milliseconds.
void fastBlink()
{
  if (ms - msLast >= BLINK_INTERVAL)
    switchLED();
}

void calcpH()
{
 float temp = ((((vRef*(float)params.pH7Cal)/adcMaxStep)*1000)- calcMilliVolts(pHSmooth))/opampGain;
 pH = 7-(temp/params.pHStep);
}

float calcMilliVolts(int numToCalc)
{
 float calcMilliVolts = (((float)numToCalc/adcMaxStep)*vRef)*1000; //pH smooth is our rolling average mine as well use it
 return calcMilliVolts;
}

int smoothADCRead(int whichPin)
{
  //lets just take a reading and drop it, this should eliminate any ADC multiplexer issues
  int throwAway = analogRead(whichPin);
  int smoothADCRead = analogRead(whichPin);
  return smoothADCRead;
}

void reset_Params(void)
{
  //Restore to default set of parameters!
  params.WriteCheck = WRITE_CHECK;
  params.statusLEDG = true;
  params.continous = false; //toggle continuous readings
  params.pH7Cal = 512; //assume ideal probe and amp conditions 1/2 of 1024
  params.pH4Cal = 382; //using ideal probe slope we end up this many 10bit units away on the 4 scale
  params.pH10Cal = 890;//using ideal probe slope we end up this many 10bit units away on the 10 scale
  params.pHStep = 59.16;//ideal probe slope
  eeprom_write_block(&params, (void *)0, sizeof(params)); //write these settings back to eeprom
}

