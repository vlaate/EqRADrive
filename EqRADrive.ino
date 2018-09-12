/*
  Simple & easy to build Right Ascension motor drive for equatorial mounts.
  This is a simple motor drive to enable an inexpensive equatorial mount to track automatically on RA.
  It was tested on an Explore Scientific Exos Nano, and worked well with a 90mm Mak at 208x (moon) for visual, and with an 80mm F4.4 refractor for short exposure (8 second) EAA imaging.
  
  RA Drives are already sold commercially, by Celestron, Orion and others. Non-tinkerers may want to just buy one of those.
  If, however, you have an odd mount for which there are no "known to work" RA drives, or a mount with the wrong number of gear teeth for the available commercial drives, then you might find this useful. 

  Would it work for your mount?
  Measure how many turns of the RA slow motion control does it take to make your mount rotate 90º.
  If that is somewhere between 25 and 90 turns, it wil likely work with a 0.6 RPM geared 12V DC motor.

  The project provides:
  A rotating knob to adjust the motor speed to match sidereal tracking rate
  A 4 digit LED display showing the current motor speed (as a percentage of max speed)
  Ability to adjust the motor speed in 0.1% steps, and to toggle the display on/off pushing the rotary knob.
  4 buttons to force the RA speed to be: 2x forward, full speed forward, 2x reverse, and full speed reverse. These are meant to help you frame the observed object in the telescope (useful since you lose the mount's slow motion flex knob when you install the DC motor)
  Posibilities: The 2x forward and 2x reverse buttons could be easily fed from an ST4/RJ11 signal if you were to adventure into guiding.

  Copyright (c) 2018 Vladimir Atehortua. All rights reserved.
  This program is free software: you can redistribute it and/or modify
  it under the terms of the version 3 GNU General Public License as
  published by the Free Software Foundation.
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License along with this program.
  If not, see <http://www.gnu.org/licenses/>
*/

/**
    Hardware used:
    Arduino based on the ATmega328P (could be an Uno, Pro Mini or similar based on the ATmega328P)
    12V 0.6RPM DC motor (~$13). These are 12V "low speed high torque" (20Kg.cm) geared motors. I got mine for $11 from here: https://www.aliexpress.com/item/-/32719817076.html but that particular store is now gone, these are found elsewhere, like here: https://www.aliexpress.com/item/-/32816451726.html)
    VNH2SP30 (~$3) Motor Driver (a.k.a. "Monster") I used this one: https://www.aliexpress.com/item/-/32247122784.html 
    KY-040 Rotary Encoder (~$1), I used this one: https://www.aliexpress.com/item/-/32251159127.html 
    TM74HC595 based, 7 segment, 4 digit common anode LED display (~$1), like this one: https://www.aliexpress.com/item/-/1688259613.html
    4 Key Matrix Membrane Switch Keypad (~$1), like this one: https://www.aliexpress.com/item/-/1719136832.html
    A shaft couplers of the approrpiate size, matching your mount on one side, and the motor on the other side. Probably one of these: https://www.aliexpress.com/item/-/1784910763.html

    Recommended power source: Talentcell 12V battery + this buck adapter: https://www.aliexpress.com/item/-/32806774850.html

    Connections:
    Arduino pin 2 to KY-040 pin CLK
    Arduino pin 3 to KY-040 pin DT
    Arduino pin 4 to Led Display pin DIO
    Arduino pin 5 to Led Display pin RCLK
    Arduino pin 6 to Led Display pin SCLK
    Arduino pin 7 to VNH2SP30 pin INA
    Arduino pin 8 to VNH2SP30 pin INB
    Arduino pin 9 to VNH2SP30 pin PWM
    Arduino pin 12 to KY-040 pin SW
    Arduino pins A0,A1,A2,A3 (also GND) to the 4 Key Matrix Membrane Switch Keypad. These often differ, you're best testing the pin order yourself with a multimeter. 
*/

#include <TimerOne.h> // We need this for high resolution PWM

#define MOTOR_A1_PIN 7  	// Connected to pin INA of the VNH2SP30 motor DRIVER
#define MOTOR_B1_PIN 8		// Connected to pin INB of the VNH2SP30 motor DRIVER
#define PWM_OUTPUT_PIN 9	// Must be pin 9 or 10, the only Arduino pins for which High res PWM (TimerOne) works
#define PERIOD_MICROSEC 16666   // 16666us = 60Hz  A low frequency is needed to get the cheap DC motor to work at low duty cycles (10%) but too low frequencies produce visible vibrations in the telescope view. I found 60 Hz worked well for visual (C90) and shot exposure EAA/video (80mm refractor)
#define COUNTLIMIT 2046   // Must be 1023 or a multiple of it, since 1023 is the default resolution of TimerOne and it's enough for 0.1% resolution duty cycle.
                          // Thus, I'm using souble that as the count limit (2046), in order to debounce the KY rotary encoder: it always gives me 2 pulses per "click"
                          // Doing this is simpler than using debounce hardware (capacitors, resistors, etc).

const int membraneButtonPins[] = {A1,A0,A3,A2}; // {key1, key2, key3, key4}  pins of the 4 membrane keypad buttons in mine, but online sources said order was different. Best test yourself with a multimeter and rearrange the order in this line of code.  

/** Code for the 7 Segment Display
 *  It's a common anode, TM74HC595 based, 7 segment 4 digit LED display I purchased from:
 *  https://www.aliexpress.com/item/-/1688259613.html
 */

#define RCLK_PIN 5	// Connected to pin RCLK 
#define SCLK_PIN 6	// Connected to pin SCLK
#define DIO_PIN 4	// Connected to pin DIO

boolean displayIsOn = true;
const static byte bits[] = // used to map a byte (integer digit) to the bits needed to display it on 7 segment led
    {
        0b11000000, //0
        0b11111001, //1
        0b10100100, //2
        0b10110000, //3
        0b10011001, //4
        0b10010010, //5
        0b10000010, //6
        0b11111000, //7
        0b10000000, //8
        0b10010000, //9
    };

void display (float percentage) // displays a percentage number from 0f to 1.0f on the 4 digit display as a percentage, for example: 0.1599 is displayed as "15.99")
  {
  int firstDigit = percentage * 10;
  int secondDigit = percentage * 100 - (firstDigit * 10);
  int thirdDigit = percentage * 1000 - ((firstDigit * 100) + (secondDigit * 10));
  int fourthDigit = percentage * 10000 - ((firstDigit * 1000) + (secondDigit * 100) + (thirdDigit * 10));

  byte digits[] = {firstDigit,secondDigit,thirdDigit,fourthDigit};
  for (int i = 0; i < 4; i++)
    {
    displayNumber(digits[i], i, i == 1);
    }
  }

const byte displays[] = {0b00001000, 0b00000100, 0b00000010, 0b00000001}; 
/**
 * Display a digit on one of the four 7 segment displays. 
 * "number" is the number to be displayed (from 0 to 9)
 * "digit" is the identifier of which of the four 7 segment displays to send the number to. 0 is leftmost, 4 is rightmost
 * "withDecimalDot" indicates whether to ilumitate the decimal dot led for this particular number
 */
void displayNumber(byte number, byte digit, boolean withDecimalDot)
{
  if (displayIsOn)
    {
    digitalWrite(RCLK_PIN, LOW); 
    shiftOut(DIO_PIN, SCLK_PIN, MSBFIRST, withDecimalDot? bits[number] & 0b01111111 : bits[number]); 
    shiftOut(DIO_PIN, SCLK_PIN, MSBFIRST, displays[digit]); 
    digitalWrite(RCLK_PIN, HIGH); 
    delay(1); 
    }
    else
    {
    digitalWrite(RCLK_PIN, LOW); 
    shiftOut(DIO_PIN, SCLK_PIN, MSBFIRST, 0b11111111); 
    shiftOut(DIO_PIN, SCLK_PIN, MSBFIRST, displays[digit]); 
    digitalWrite(RCLK_PIN, HIGH); 
    delay(1); 
    }
}


/** Code for the KY-040 Rotary Encoder
 * Based on code from http://domoticx.com/arduino-rotary-encoder-keyes-ky-040/ 
 */

#define KY40_PIN_A   2  // Connected to CLK pin of the KY-040 Rotary encoder
#define KY40_PIN_B   3  // Connected to DT pin of the KY-040 Rotary encoder
#define KY40_BUTTON_PIN   12  // Connected to SW pin of the KY-040 Rotary encoder
// Don't forget to connect VCC (sometimes labeled "+") KY-040 to to the Arduino VCC

long timeOfLastEvent = 0; // used to debounce the rotary encoder in software
boolean pwmUpdateNeeded = true;
static  byte abOld;     // Initialize state
volatile int count = COUNTLIMIT / 13;     // current rotary count, initialized at 1/13 (or 7.6%) of max.
int old_count;     // old rotary count

// On interrupt, read input pins, compute new state, and adjust count
enum { upMask = 0x66, downMask = 0x99 };
void pinChangeISR() {
  byte abNew = (digitalRead(KY40_PIN_A) << 1) | digitalRead(KY40_PIN_B);
  byte criterion = abNew ^ abOld;
  if (criterion == 1 || criterion == 2) {
    if (upMask & (1 << (2 * abOld + abNew / 2)))
      count++;
    else count--;       // upMask = ~downMask
  }
  abOld = abNew;        // Save new state

pwmUpdateNeeded = true;
if (count < 0) count =0;
if (count > COUNTLIMIT) count = COUNTLIMIT;
}

long lastpressed = 0;
void checkKYbutton()	// if the button on the KY-040 encoder is pressed, we use it to toggle on/off the led display (to avoid disturbing dark adaptation)
{
  boolean button = digitalRead(KY40_BUTTON_PIN);
  if (millis() - lastpressed > 500 && !button) // simple software debouncing of the pushbutton
  {
    displayIsOn = !displayIsOn;
    lastpressed = millis();
  }
}

/************************************************************************
 * Main Arduino setup and loop code
 */

boolean forward = true;
boolean stopped = false;
boolean speed2x = false;
boolean speedFull = false;
float dutyCycle = 8.0f;  //default motor speed

void setup()                         
{
  // setting up the membrane keypad pins:
  for(int i=0; i<sizeof(membraneButtonPins) / sizeof(int); i++)
  {
    pinMode(membraneButtonPins[i], INPUT_PULLUP); 
  }  
  
  // setting up display pins:
  pinMode(RCLK_PIN, OUTPUT);
  pinMode(SCLK_PIN, OUTPUT);
  pinMode(DIO_PIN, OUTPUT);
  
  // Setting up interrupts for rotary encoder (user input):
  pinMode(KY40_BUTTON_PIN, INPUT_PULLUP);
  pinMode(KY40_PIN_A, INPUT_PULLUP);
  pinMode(KY40_PIN_B, INPUT_PULLUP);
  attachInterrupt(0, pinChangeISR, CHANGE); // Set up pin-change interrupts
  attachInterrupt(1, pinChangeISR, CHANGE);

  // Setup Timer1 for PWM: 
  Timer1.initialize(PERIOD_MICROSEC);
    
  // Setup VNH2SP30 motor driver pins: 
  pinMode(MOTOR_A1_PIN, OUTPUT);
  pinMode(MOTOR_B1_PIN, OUTPUT);

  Serial.begin(9600);              
}

void loop() 
{
  checkKYbutton(); // check if the button for toggling LED display was pushed and do accordingly.

  int membraneButtonStatus[] = {1,1,1,1};
  for (int i = 0; i < sizeof(membraneButtonPins) / sizeof(int); i++)	// check the status of the 4 membrane buttons 
  {
    int newStatus = digitalRead(membraneButtonPins[i]);
    membraneButtonStatus[i] = newStatus;
  }

  boolean oldForward = forward;
  forward = membraneButtonStatus[0] + membraneButtonStatus[1] >= 2;
  boolean oldSpeed2x = speed2x;
  speed2x = membraneButtonStatus[1] + membraneButtonStatus[2] < 2;
  boolean oldSpeedFull = speedFull;
  speedFull = membraneButtonStatus[0] + membraneButtonStatus[3] < 2;

  if (forward != oldForward || speed2x != oldSpeed2x || speedFull != oldSpeedFull) // if state changed on one of the buttons
  {
    pwmUpdateNeeded = true;	
  }

  if (pwmUpdateNeeded) // If either an interrupt (from rotary encoder) or one of the above 4 pushbuttons signaled the PWM speed needs to be updated
  {
    pwmUpdateNeeded = false;
    if(forward)
    {
//      Serial.println("forward");
      digitalWrite(MOTOR_A1_PIN, LOW); 
      digitalWrite(MOTOR_B1_PIN, HIGH);
    }
    else if(!stopped)
    {
//      Serial.println("reverse");
      digitalWrite(MOTOR_A1_PIN, HIGH);
      digitalWrite(MOTOR_B1_PIN, LOW);      
    }
    else
    {
//      Serial.println("stop");
      digitalWrite(MOTOR_A1_PIN, LOW);
      digitalWrite(MOTOR_B1_PIN, LOW);            
    }
  
    dutyCycle = ((float) count*100) / ((float)COUNTLIMIT);
    int pwm_duty_cycle = (dutyCycle / 100) * 1023;
    if (speed2x) { pwm_duty_cycle *= 2; }
    else if (speedFull) {pwm_duty_cycle = 1023;}
    Timer1.pwm(PWM_OUTPUT_PIN, pwm_duty_cycle);
  }

 display(dutyCycle/100.0);	// display the duty cycle as percentage on the 7 segment leds.
}
