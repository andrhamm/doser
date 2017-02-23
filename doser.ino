#include <Button.h>        //https://github.com/JChristensen/Button

#define BUTTON_PIN 2       //Connect a tactile button switch (or something similar)
                           //from Arduino pin 2 to ground.
#define PULLUP true        //To keep things simple, we use the Arduino's internal pullup resistor.
#define INVERT false       //Since the pullup resistor will keep the pin high unless the
                           //switch is closed, this is negative logic, i.e. a high state
                           //means the button is NOT pressed. (Assuming a normally open switch.)
#define DEBOUNCE_MS 10     //A debounce time of 20 milliseconds usually works well for tactile button switches.

#define LED_PIN 13         //The standard Arduino "Pin 13" LED.
#define LONG_PRESS 500    //We define a "long press" to be 2000 milliseconds.
#define BLINK_INTERVAL 100 //In the BLINK state, switch the LED every 100 milliseconds.

Button myBtn(BUTTON_PIN, PULLUP, INVERT, DEBOUNCE_MS);    //Declare the button

//The list of possible states for the state machine. This state machine has a fixed
//sequence of states, i.e. ONOFF --> TO_BLINK --> BLINK --> TO_ONOFF --> ONOFF
//Note that while the user perceives two "modes", i.e. ON/OFF mode and rapid blink mode,
//two extra states are needed in the state machine to transition between these modes.
//enum {ONOFF, TO_BLINK, BLINK, TO_ONOFF};

enum STATES {
  TO_PH, PH, 
  MANUAL_DOSE, 
  TO_CALFOUR, CALFOUR, 
  TO_CALSEVEN, CALSEVEN, 
  TO_READONLY, READONLY
};
uint8_t STATE;                   //The current state machine state
boolean readOnlyState;           //The readonly state (false = dont auto dose)
boolean ledState;                //The current LED status
unsigned long ms;                //The current time from millis()
unsigned long msLast;            //The last time the LED was switched

void setup(void)
{
    Serial.begin(9600);
    pinMode(LED_PIN, OUTPUT);    //Set the LED pin as an output
    STATE = TO_PH;
}

void loop(void)
{
    ms = millis();               //record the current time
    myBtn.read();                //Read the button

    switch (STATE) {
      // TODO: startup screen
      
      case TO_PH:
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
        else {
          // Display current pH. ex: "4.01"
        }
        break;

      case MANUAL_DOSE:
        if (myBtn.isPressed()) {
          // Activate pump as long as button is held down
          Serial.println("Dose!");
          // Display "dOSE"
        }
        else {
          // Deactivate pump
          STATE = TO_PH;
        }
        break;

      case TO_CALFOUR:
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
        else if (ms - myBtn.lastChange() >= 5000) {
          STATE = TO_PH;
        } else {
          // Display "CAL4"
          // after 5 seconds go to PH
        }
        break;

      case TO_CALSEVEN:
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
        else if (ms - myBtn.lastChange() >= 5000) {
          STATE = TO_PH;
        }
        else {
          // Display "CAL7"
          // after 5 seconds go to PH
        }
        break;

      case TO_READONLY:
        if (myBtn.wasReleased()) {
          Serial.println("Switching to RO menu");
          STATE = READONLY;
        }
        break;

      case READONLY:
        if (myBtn.wasReleased()) {
          STATE = TO_PH;
        }
        else if (myBtn.pressedFor(LONG_PRESS)) {
          Serial.println("Inverting RO mode");
          readOnlyState = !readOnlyState;
          STATE = TO_READONLY;
        }
        else if (ms - myBtn.lastChange() >= 5000) {
          STATE = TO_PH;
        }
        else {
          // Display current readonly state
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

//Switch the LED on and off every BLINK_INETERVAL milliseconds.
void fastBlink()
{
  if (ms - msLast >= BLINK_INTERVAL)
    switchLED();
}

