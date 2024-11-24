/*
  Code to interactively test various ways of generating RF signals in the 80m band 
  (primarily 3.5-3.6 MHz) using a Raspberry Pi Pico 2 (or Pi Pico). Intended to 
  eventually be used in a foxoring transmitter, but could perhaps also be useful 
  for other QRP transmitters.
  
  The operation is controlled via a serial port terminal. Type "?"" or "help" to get information
  about what commands are available. A PIO is used to toggle one or two pins for single-ended or 
  differential output. Most modes use DMA to send pre-calculated waveforms to the PIO. 

  Supported modes:

  0. A PIO CLKDIV is used to set the frequency. The output is a jittery square wave, so
     lots of HD3 and spurious overtones. Single ended output. Poor frequency resolution.
     But simple.
  1. A (jittery) square wave is streamed via DMA to the PIO. Dithering can be applied to 
     reduce distortion and spurious tones. The frequency resolution is significantly improved
     compared to mode 0. The output is differential.
  2. A sigma-delta modulated waveform is sent via DMA to the PIO. HD3 is highly attenuated.
     Dithering can be applied to reduce spurious tones. The output is differential.
  3. Trinary quantization is used, otherwise the same as mode 2.
  4. Same as mode 2, except that key clicks are reduced by smooth transitions between 
     transmission and silence.
  5. Same as mode 3, except that key clicks are reduced by smooth transitions between 
     transmission and silence.

  Mode 5 is the default.

  Parameters that can be changed include:
  - Frequency
  - Encoding mode (see above)
  - Send morse or continuously
  - Morse rate
  - Morse string to be repeated
  - Call sign
  - Amount of dithering
  - Amplitude of the sinewave
  - Amplitude of HD3 compensation
  - Phase of HD3 compensation
  - Buffer size
  - Silent output (useful e.g. for output impedance measurement)

  The processor clock is expected to be 200 MHz, but other frequencies are supported by 
  changing the constant at the top of synth.cpp.

  A low-pass filter (and a balun) should be placed between the output pins and the antenna
  if this is to be used as a transmitter. A HAM license is probably needed to be allowed to 
  use this as a transmitter.

  The code works on both Pi Pico and Pi Pico 2, but the Pico 2 is preferred as it has lower
  power consumtion, more memory (allows longer buffers) and a floating point unit (greatly speeds
  up buffer re-calculation).

  The code can be compiled in the Arduino environment using Earle F. Philhower's 
  Raspberry Pi Pico board package.

  A potentially interesting piece of code is that for approximating doubles with rational numbers
  in farey.cpp and farey.h. See:
  https://axotron.se/blog/fast-algorithm-for-rational-approximation-of-floating-point-numbers/
  for more information.

  The code is released under the MIT license, except the cmdArduino code which has its own license.

  Per Magnusson, SA5BYZ, 2024

*/

#include <LiquidCrystal.h>
#include <Bounce2.h>
#include <elapsedMillis.h>
#include "Wire.h"
#include "cmdArduino.h"
#include "commands.h"
#include "transmitter_PiPico.h"


double target_freqs[] =  {
  3579900,
  3530000,
  3550000,
  3570000,
  3600000,
};

static const int n_freqs = sizeof(target_freqs)/sizeof(target_freqs[0]);

int current_freq_num = 0;


bool key_down = false; // Whether to transmit continuously
int morse_rate = 0;   // Morse rate in words per minute, 0 means unset

const int fox_len = 10;    // Length of fox_string
char fox_string[fox_len+1] = "MOS";  // String to send as fox identifier
const int call_len = 20;   // Length of callsign
char callsign[call_len+1] = "SA5BYZ";   // String to send as callsign


static const int Button1_Pin = 15;
static const int LCD_RS_Pin = 2;
static const int LCD_EN_Pin = 3;
static const int LCD_D4_Pin = 4;
static const int LCD_D5_Pin = 5;
static const int LCD_D6_Pin = 6;
static const int LCD_D7_Pin = 7;
static const int LED_Pin = 25;
static const int Resistor_Pin = 1; // To periodically pull power from the power bank so that it does not power off
static const int Morse_Debug_Pin = 26;
const int First_RF_Pin = 21;
const int Second_RF_Pin = First_RF_Pin+1;

synth *rf_synth = NULL;

LiquidCrystal lcd(LCD_RS_Pin, LCD_EN_Pin, LCD_D4_Pin, LCD_D5_Pin, LCD_D6_Pin, LCD_D7_Pin);
Bounce btn1 = Bounce();

elapsedMillis global_time;

uint32_t resistor_time;


// Morse code constants
int32_t msPerUnit = 120;           // Morse rate, 120 ms per unit = 10 WPM
int32_t msPerDot = msPerUnit;      // Duration of a dot
int32_t msPerDash = msPerUnit * 3; // Duration of a dash
int32_t msPiecePause = msPerUnit;  // Pause between pieces of a character
int32_t msCharPause = msPerUnit * 3; // Pause between characters
int32_t msWordPause = msPerUnit * 7; // Pause between words

// Conversion table from ASCII to morse code.
// Dashes are encoded as ones, and dots as zeros in the LSBs.
// The MorseLengths array tells how many pieces each character has.
static const uint8_t MorseCodes[] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x06, 0x12, 0x09, 0x09, 0x0C, 0x00, 0x1E,
  0x16, 0x2D, 0x00, 0x0A, 0x33, 0x21, 0x15, 0x12, 0x1F, 0x0F,
  0x07, 0x03, 0x01, 0x00, 0x10, 0x18, 0x1C, 0x1E, 0x38, 0x2A,
  0x00, 0x00, 0x00, 0x0C, 0x1A, 0x01, 0x08, 0x0A, 0x04, 0x00,
  0x02, 0x06, 0x00, 0x00, 0x07, 0x05, 0x04, 0x03, 0x02, 0x07,
  0x06, 0x0D, 0x02, 0x00, 0x01, 0x01, 0x01, 0x03, 0x09, 0x0B,
  0x0C, 0x00, 0x00, 0x00, 0x00, 0x0D, 0x1E, 0x01, 0x08, 0x0A,
  0x04, 0x00, 0x02, 0x06, 0x00, 0x00, 0x07, 0x05, 0x04, 0x03,
  0x02, 0x07, 0x06, 0x0D, 0x02, 0x00, 0x01, 0x01, 0x01, 0x03,
  0x09, 0x0B, 0x0C, 0x00, 0x00, 0x00, 0x15, 0x00
};

static const uint8_t MorseLengths[] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x05, 0x06, 0x05, 0x07, 0x05, 0x00, 0x06,
  0x05, 0x06, 0x00, 0x05, 0x06, 0x06, 0x06, 0x05, 0x05, 0x05,
  0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x06, 0x06,
  0x00, 0x00, 0x00, 0x06, 0x06, 0x02, 0x04, 0x04, 0x03, 0x01,
  0x04, 0x03, 0x04, 0x02, 0x04, 0x03, 0x04, 0x02, 0x02, 0x03,
  0x04, 0x04, 0x03, 0x03, 0x01, 0x03, 0x04, 0x03, 0x04, 0x04,
  0x04, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06, 0x02, 0x04, 0x04,
  0x03, 0x01, 0x04, 0x03, 0x04, 0x02, 0x04, 0x03, 0x04, 0x02,
  0x02, 0x03, 0x04, 0x04, 0x03, 0x03, 0x01, 0x03, 0x04, 0x03,
  0x04, 0x04, 0x04, 0x00, 0x00, 0x00, 0x05, 0x00
};


void lcd_print_frequency();


void start_transmitting()
{
  if(!rf_synth) {
    // Initialize synth object, should not be necessary here
    rf_synth = new synth(First_RF_Pin, target_freqs[current_freq_num]);
  }
  rf_synth->enable_output();
}


void stop_transmitting()
{
  rf_synth->disable_output();
}


void setup()
{
  // Wait for the serial port
  Serial.begin(115200);
  cmd.begin(115200, &Serial);
  rp2040.enableDoubleResetBootloader();
  pinMode(LED_Pin, OUTPUT);
  
  int ii = 0;
  while (ii++<20) {
    digitalWrite(LED_Pin, HIGH);
    sleep_ms(50);
    digitalWrite(LED_Pin, LOW);
    sleep_ms(50);
  }
  //delay(3000);
  digitalWrite(LED_Pin, LOW);

  RegisterCommands();

  Serial.println("Initializing...");
  Serial.flush();

  
  pinMode(Morse_Debug_Pin, OUTPUT);
  digitalWrite(Morse_Debug_Pin, HIGH);
  
  pinMode(Resistor_Pin, OUTPUT);
  digitalWrite(Resistor_Pin, HIGH);
  resistor_time = global_time;

  btn1.attach(Button1_Pin, INPUT);
  btn1.interval(10);

  digitalWrite(Morse_Debug_Pin, LOW);
  
  morse_rate = 10;
  initMorseRate(morse_rate);

  start_transmitting();
  Serial.println("synth created");

  lcd.begin(20, 4);
  lcd_print_frequency();
  Serial.println("End of setup");
  Serial.flush();

}


// Change the rate of the morse code in words per minute.
void initMorseRate(uint32_t WPM)
{
  if(WPM < 5) {
    WPM = 5;
  }
  if(WPM > 100) {
    WPM = 100;
  }
  msPerUnit = 1000 * 60 / (WPM * 50); // ms per minute / WPM / units per word
  msPerDot = msPerUnit;      // Duration of a dot
  msPerDash = msPerUnit * 3; // Duration of a dash
  msPiecePause = msPerUnit;  // Pause between pieces of a character
  msCharPause = msPerUnit * 3; // Pause between characters
  msWordPause = msPerUnit * 7; // Pause between words
}


void lcd_print_frequency()
{
  uint32_t f;
  f = target_freqs[current_freq_num];
  lcd.clear();
  lcd.print(f);
}


void next_frequency()
{
  current_freq_num++;
  if(current_freq_num >= n_freqs) {
    current_freq_num = 0;
  }
  lcd_print_frequency();
}


void loop()
{
  static uint32_t state1 = 0;
  static uint32_t state2 = 0;

  cmd.poll();

  if (digitalRead(Resistor_Pin) == HIGH) {
    if (global_time > resistor_time + 1000) {
      //test_rational_approx();
      digitalWrite(Resistor_Pin, LOW);
      digitalWrite(LED_Pin, LOW);
      resistor_time = global_time;
    }
  } else {
    if (global_time > resistor_time + 9000) {
      digitalWrite(Resistor_Pin, HIGH);
      digitalWrite(LED_Pin, HIGH);
      resistor_time = global_time;
    }
  }

  btn1.update();
  if(btn1.fell()) {
    next_frequency();
  }

  if(key_down) {
    start_transmitting();
    return;
  }

  if (state1 < 10) {
    initMorseRate(morse_rate); // Normal speed
    switch (state2) {
      case 0:
        // beginning of transmission, send morse letter M
        if (sendMorseString(fox_string)) {
          state2++;
        }
        break;
      case 1:
        // Pause
        if (sendWordPause()) {
          state2 = 0;
          state1++;
        }
        break;
      default:
        state2 = 0;
    }
  } else {
    if(strlen(callsign) == 0) {
      // No callsign to transmit
      state2 = 0;
      state1 = 0;      
    } else {
      initMorseRate(2*morse_rate); // Fast
      switch (state2) {
        case 0:
          // Send  a string
          if (sendMorseString(callsign)) {
            state2++;
          }
          break;
        case 1:
          // Pause
          digitalWrite(LED_Pin, HIGH);
          digitalWrite(Resistor_Pin, HIGH);
          if (sendWordPause()) {
            digitalWrite(LED_Pin, LOW);
            digitalWrite(Resistor_Pin, LOW);
            state2 = 0;
            state1 = 0;
          }
          break;
        default:
          state2 = 0;
      }
    }
  }
}


// Send a string of morse characters.
// Keep calling this function many times per unit interval until it returns true to signal that the transmission is done.
int sendMorseString(const char *str)
{
  static int state = 0;
  static uint32_t charNo = 0;
  uint8_t currentChar;
  static uint8_t currentCode;
  static uint8_t currentLen;

  if (charNo >= strlen(str)) {
    // Done sending all characters, clean up and return 1
    charNo = 0;
    return 1;
  }

  if (state == 0) {
    // Start sending next character
    currentChar = str[charNo];
    if (currentChar == ' ') {
      // Space, send a pause
      state = 2;
      sendPause(msWordPause - msCharPause);
    } else {
      // Not a space
      currentCode = MorseCodes[currentChar];
      currentLen = MorseLengths[currentChar];
      if (currentLen > 0) {
        // Valid character
        state = 1;
        sendMorseLetter(currentCode, currentLen);
      } else {
        // Invalid character, just advance to the next one and let next call take care of it
        charNo++;
      }
    }

  } else if (state == 1) {
    // We are already sending a character, see if we are done with this one
    if (sendMorseLetter(currentCode, currentLen)) {
      // Yes, we are done with the character, advance to next one
      charNo++;
      state = 0;
    }

  } else if (state == 2) {
    // We are sending a pause
    if (sendPause(msWordPause - msCharPause)) {
      // Done with the pause
      charNo++;
      state = 0;
    }
  }
  return 0;
}


// Send three dots to identify control number 3.
// Keep calling this function many times per unit interval until it returns true to signal that the transmission is done.
int sendThird()
{
  return sendMorseLetter(0x00, 3);
}


// Send the letter M (--) as morse.
// Keep calling this function many times per unit interval until it returns true to signal that the transmission is done.
int sendM()
{
  return sendMorseLetter(0x03, 2);
}


// Send the letter O (---) as morse.
// Keep calling this function many times per unit interval until it returns true to signal that the transmission is done.
int sendO()
{
  return sendMorseLetter(0x07, 3);
}


// Add the delay difference between a character delay and a word delay.
// Keep calling this function repeatedly until it returns true to signal that the delay is done.
int sendWordPause()
{
  return sendPause(msWordPause - msCharPause);
}


// Add a delay of 'pause' ms.
// Keep calling this function repeatedly until it returns true to signal that the delay is done.
int sendPause(uint32_t pause)
{
  static int state = 0;
  static uint32_t nextEventTime;

  if (state == 0) {
    nextEventTime = global_time + pause;
    state = 1;
  } else {
    if (global_time > nextEventTime) {
      // We are done
      state = 0;
      return 1;
    }
  }
  return 0;
}


// Send a morse letter encoded as the 'len' LSBs of 'code'. A dot is encoded as a 0 and a dash as a 1.
// The most significant of the bits is sent first, so P (.--.) would be specified as code = 0x06 and len = 4.
// Keep calling this function repeatedly until it returns true to signal that the transmission is done.
// Changing whether a signal is sent or not can only happen when the function is called. So it should be called
// at least five times more frequently than the 'msPerUnit' constant specifies.
int sendMorseLetter(uint32_t code, uint32_t len)
{
  static int state = 0;
  static int currentBit = 0;
  static uint32_t nextEventTime;
  static uint32_t stateBegin;

  if (state == 0) {
    /*
    Serial.print(F("Code: "));
    Serial.print(code, BIN);
    Serial.print(F(" Len: "));
    Serial.println(len, DEC);
    */
    // Start of transmission of this character, start transmitting
    stateBegin = global_time;
    start_transmitting();
    currentBit = len;
    state = 1;
    if (code & (1 << (currentBit - 1))) {
      // Transmitting a dash
      nextEventTime = stateBegin + msPerDash;
    } else {
      // Transmitting a dot
      nextEventTime = stateBegin + msPerDot;
    }

  } else {
    // Transmission has already started
    if (global_time < nextEventTime) {
      // It is not yet time to change anything, return immediately
      return 0;
    }
    // Time to do something
    switch (state) {

      case 1:
        // End transmission of this piece of a character
        stateBegin = nextEventTime;
        stop_transmitting();
        currentBit--;
        state = 2;
        nextEventTime = stateBegin + msPiecePause;
        break;

      case 2:
        // End of pause after a piece of a character
        stateBegin = nextEventTime;
        // Was it the last piece?
        if (currentBit == 0) {
          // Yes, need to wait some more for spacing between characters
          nextEventTime = stateBegin + msCharPause - msPiecePause;
          state = 3;
        } else {
          // No, start next piece of this character
          start_transmitting();
          state = 1;
          if (code & (1 << (currentBit - 1))) {
            // Transmitting a dash
            nextEventTime = stateBegin + msPerDash;
          } else {
            // Transmitting a dot
            nextEventTime = stateBegin + msPerDot;
          }
        }
        break;

      case 3:
        // We have reached the end of the character and the pause after it
        state = 0;
        return 1;
    }
  }
  return 0;
}
