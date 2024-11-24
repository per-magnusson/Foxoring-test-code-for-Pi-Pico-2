#include <pico/stdlib.h>
#include "commands.h"
#include "transmitter_PiPico.h"

void CmdPrintHelp(int argc, char **argv);
void CmdPrintStatus(int argc, char **argv);
void CmdKeyDown(int argc, char **argv);
void CmdMorseRate(int argc, char **argv);
void CmdFox(int argc, char **argv);
void CmdCall(int argc, char **argv);
void CmdDither(int argc, char **argv);
void CmdAmpl(int argc, char **argv);
void CmdAmplHD3(int argc, char **argv);
void CmdPhaseHD3(int argc, char **argv);
void CmdFreq(int argc, char **argv);
void CmdMode(int argc, char **argv);
void CmdBufsize(int argc, char **argv);
void CmdDefault(int argc, char **argv);
void CmdOff(int argc, char **argv);

void PrintNumArgError(int argc, char **argv, int expectedArgc);
int32_t Str2Num(const char *str, uint8_t base);
double Str2Double(char *str);


// Define all the commands that can be sent from a terminal.
void RegisterCommands() {
  cmd.add("?", CmdPrintHelp);
  cmd.add("help", CmdPrintHelp);
  cmd.add("stat", CmdPrintStatus);
  cmd.add("keydown", CmdKeyDown);
  cmd.add("rate", CmdMorseRate);
  cmd.add("fox", CmdFox);
  cmd.add("call", CmdCall);
  cmd.add("dither", CmdDither);
  cmd.add("ampl", CmdAmpl);
  cmd.add("ampl3", CmdAmplHD3);
  cmd.add("ph3", CmdPhaseHD3);
  cmd.add("freq", CmdFreq);
  cmd.add("mode", CmdMode);
  cmd.add("bufsize", CmdBufsize);
  cmd.add("default", CmdDefault);
  cmd.add("off", CmdOff);
}


void CmdPrintHelp(int argc, char **argv) {
  const int num_args = 1;
  
  if(argc != num_args) {
    PrintNumArgError(argc, argv, num_args);
    return;
  }

  Serial.println("******");
  Serial.println("Compiled: " __DATE__ ", " __TIME__ " ");
  Serial.println("Commands:");
  Serial.println("  ? or help - Print this help text");
  Serial.println("  stat - Print the current status");
  Serial.println("  keydown val - transmit continuously (val = 1) or normally (val = 0)");
  Serial.println("  rate wpm - set the morse rate to wpm words per minute");
  Serial.println("  fox str - set str as fox identifier, e.g. MOS");
  Serial.println("  fox num - set 0 <= num <= 7 as fox number. 0 gives MO, 1 gives MOE etc");
  Serial.println("  fox     - print the current fox string");
  Serial.println("  call str - set str as call sign, e.g. SA5BYZ");
  Serial.println("  call     - send no call sign");
  Serial.println("  dither val - set the amount of dither, 0.0 to 2.0");
  Serial.println("  ampl val - set the amplitude, 0.0 to 2.0");
  Serial.println("  ampl3 val - set the amplitude of HD3, -0.5 to 0.5");
  Serial.println("  ph3 val - set the phase of HD3, degrees");
  Serial.println("  freq val - set the frequency, Hz");
  Serial.println("  mode val - set the signal generation mode:");
  Serial.println("             0 - CLKDIV, 1 - comparator, 2 - binary sigma delta,");
  Serial.println("             3 - trinary sigma delta, 4 - click free binary sigma delta,");
  Serial.println("             5 - click free trinary sigma delta");
  Serial.println("  bufsize val - set max number of words in buffer");
  Serial.println("  default - set all parameters to default values");
  Serial.println("  off val - turn output off");
  Serial.println("            0 - turn output on");
  Serial.println("            1 - one high, one low");
  Serial.println("            2 - both low");
  Serial.println("            3 - both high");
  Serial.println("            4 - both high-Z");
}


void PrintStatus()
{
  Serial.print("Key down: ");
  key_down ? Serial.println("Yes") : Serial.println("No");
  if(!key_down) {
    Serial.print("Morse rate: ");
    Serial.println(morse_rate);
    Serial.print("Fox: ");
    Serial.println(fox_string);
    Serial.print("Call: ");
    Serial.println(callsign);
  }
  Serial.print("CPU_freq: ");
  Serial.println(CPU_freq_actual);
  if(rf_synth->get_mode() != 0) {
    Serial.print("Dither: ");
    Serial.println(rf_synth->get_dither_amplitude());
    Serial.print("Amplitude: ");
    Serial.println(rf_synth->get_amplitude());
    Serial.print("HD3 amplitude: ");
    Serial.println(rf_synth->get_hd3_amplitude(), 4);
    Serial.print("HD3 phase: ");
    Serial.println(rf_synth->get_hd3_phase()*180/M_PI);
    Serial.print("N words: ");
    Serial.println(rf_synth->get_n_words());
    Serial.print("N periods: ");
    Serial.println(rf_synth->get_n_periods());
  } else {
    Serial.print("Divider: ");
    float clkdiv = round(256.0*CPU_freq_actual/(2.0*rf_synth->get_frequency_exact()))/256.0;
    float intpart = floor(clkdiv);
    float numerator = (clkdiv - intpart)*256;
    Serial.print((int)intpart);
    Serial.print(" + ");
    Serial.print((int)numerator);
    Serial.println("/256");
  }
  Serial.print("RF frequency: ");
  Serial.println(rf_synth->get_frequency_exact());
  Serial.print("Mode: ");
  Serial.println(rf_synth->get_mode_str());
}


void CmdPrintStatus(int argc, char **argv) {
  const int num_args = 1;
  
  if(argc != num_args) {
    PrintNumArgError(argc, argv, num_args);
    return;
  }
  PrintStatus();
}


void CmdKeyDown(int argc, char **argv) {
  if(argc > 2) {
    // More than one argument
    PrintNumArgError(argc, argv, 2);
    return;
  }
  if(argc == 1) {
    // No arguments means key down
    key_down = true;
    return;
  }
  if(argv[1][0] == '1') {
    key_down = true;
  } else {
    key_down = false;
  }
}


void CmdMorseRate(int argc, char **argv) {
  const int num_args = 2;
  int rate;

  if(argc != num_args) {
    PrintNumArgError(argc, argv, num_args);
    return;
  }
  rate = Str2Num(argv[1], 10);
  if(rate < 5 || rate > 100) {
    Serial.print("Morse rate must be between 5 and 100");
    return;
  }
  morse_rate = rate;
  initMorseRate(morse_rate);
}


// Copy a string to fox_str, respecting the length of the arrays
void FoxCopy(const char *str)
{
  for(int ii=0; ii <= fox_len; ii++) {
    fox_string[ii] = str[ii];
    if(!str[ii]) {
      break;
    }
  }
  fox_string[fox_len-1] = '\0';
}


void CmdFox(int argc, char **argv) {
  if(argc == 1) {
    // No argument, show the string
    Serial.print("Fox string: '");
    Serial.print(fox_string);
    Serial.println("'");
    return;
  }
  if(argc > 2) {
    PrintNumArgError(argc, argv, 2);
    return;
  }
  // One argument
  char c0;
  c0 = argv[1][0];
  if(c0 >= '0' && c0 <= '7' && strlen(argv[1]) == 1) {
    // Interpret as a number
    int n;
    n = Str2Num(argv[1], 10);
    switch(n) {
      case 0:
        FoxCopy("MO");
        break;
      case 1:
        FoxCopy("MOE");
        break;
      case 2:
        FoxCopy("MOI");
        break;
      case 3:
        FoxCopy("MOS");
        break;
      case 4:
        FoxCopy("MOH");
        break;
      case 5:
        FoxCopy("MO5");
        break;
      case 6:
        FoxCopy("MON");
        break;
      case 7:
        FoxCopy("MOD");
        break;
      default:
        FoxCopy("MO");
        break;
    }
  } else {
    // Use the strign directly
    FoxCopy(argv[1]);
  }
}


// Copy a string to call_str, respecting the length of the arrays
void CallCopy(const char *str)
{
  for(int ii=0; ii <= call_len; ii++) {
    callsign[ii] = str[ii];
    if(!str[ii]) {
      break;
    }
  }
  callsign[call_len-1] = '\0';
}


void CmdCall(int argc, char **argv) {
  if(argc == 1) {
    // No argument, send no call sign
    CallCopy("");
    return;
  }
  if(argc > 2) {
    PrintNumArgError(argc, argv, 2);
    return;
  }
  // One argument
  CallCopy(argv[1]);
}


void CmdDither(int argc, char **argv) {
  if(argc == 1) {
    // No argument, print current value
    Serial.println(rf_synth->get_dither_amplitude());
    return;
  }
  if(argc > 2) {
    PrintNumArgError(argc, argv, 2);
    return;
  }
  // One argument
  double v = Str2Double(argv[1]);
  if(v >= 0 && v <= 3) {
    rf_synth->set_dither_amplitude(v);
    rf_synth->apply_settings();
  } else {
    Serial.println("Invalid dither value");
  }
}


void CmdAmpl(int argc, char **argv) {
  if(argc == 1) {
    // No argument, print current value
    Serial.println(rf_synth->get_amplitude());
    return;
  }
  if(argc > 2) {
    PrintNumArgError(argc, argv, 2);
    return;
  }
  // One argument
  double v = Str2Double(argv[1]);
  if(v >= 0 && v <= 2) {
    rf_synth->set_amplitude(v);
    rf_synth->apply_settings();
  } else {
    Serial.println("Invalid amplitude value");
  }
}


void CmdAmplHD3(int argc, char **argv) {
  if(argc == 1) {
    // No argument, print current value
    Serial.println(rf_synth->get_hd3_amplitude(), 4);
    return;
  }
  if(argc > 2) {
    PrintNumArgError(argc, argv, 2);
    return;
  }
  // One argument
  double v = Str2Double(argv[1]);
  if(v >= -0.5 && v <= 0.5) {
    rf_synth->set_hd3_amplitude(v);
    rf_synth->apply_settings();
  } else {
    Serial.println("Invalid HD3 amplitude value");
  }
}


void CmdPhaseHD3(int argc, char **argv) {
  if(argc == 1) {
    // No argument, print current value
    Serial.println(rf_synth->get_hd3_phase());
    return;
  }
  if(argc > 2) {
    PrintNumArgError(argc, argv, 2);
    return;
  }
  // One argument
  double v = Str2Double(argv[1]);
  if(v >= -400 && v <= 400) {
    rf_synth->set_hd3_phase(v*M_PI/180);
    rf_synth->apply_settings();
  } else {
    Serial.println("Invalid HD3 amplitude value");
  }
}

void CmdFreq(int argc, char **argv) {
  if(argc == 1) {
    // No argument, print current value
    Serial.println(rf_synth->get_frequency());
    return;
  }
  if(argc > 2) {
    PrintNumArgError(argc, argv, 2);
    return;
  }
  // One argument
  double v = Str2Double(argv[1]);
  if(v >= 100e3 && v <= 20e6) {
    rf_synth->set_frequency(v);
    rf_synth->apply_settings();
  } else {
    Serial.println("Invalid frequency value");
  }
}


void CmdMode(int argc, char **argv) {
  if(argc == 1) {
    // No argument, print current value
    Serial.println(rf_synth->get_mode_str());
    return;
  }
  if(argc != 2) {
    PrintNumArgError(argc, argv, 2);
    return;
  }
  int m = Str2Num(argv[1], 10);
  if(m > 5 || m < 0) {
    Serial.print("Mode must be between 0 and 5");
    return;
  }
  rf_synth->set_mode(m);
  rf_synth->apply_settings();
}


void CmdBufsize(int argc, char **argv) {
  if(argc == 1) {
    // No argument, print current value
    Serial.println(rf_synth->get_max_words());
    return;
  }
  if(argc != 2) {
    PrintNumArgError(argc, argv, 2);
    return;
  }
  int v = Str2Num(argv[1], 10);
  if(v > 10000 || v < 2) {
    Serial.print("Bufsize must be between 2 and 10000");
    return;
  }
  rf_synth->set_max_words(v);
  rf_synth->apply_settings();
}


void CmdDefault(int argc, char **argv) {
  rf_synth->set_dither_amplitude(1.0);
  rf_synth->set_amplitude(1.0);
  rf_synth->set_frequency(3579900.0);
  rf_synth->set_mode(5);
  rf_synth->set_max_words(max_words);
  rf_synth->apply_settings();
}


void CmdOff(int argc, char **argv) {
  if(argc != 2) {
    PrintNumArgError(argc, argv, 2);
    return;
  }
  int m = Str2Num(argv[1], 10);
  if(m > 4 || m < 0) {
    Serial.print("Parameter must be between 0 and 4");
    return;
  }
  if(m == 0) {
    // Turn on RF
    rf_synth->restore_out_pins();
  } else {
    pinMode(First_RF_Pin, OUTPUT);
    pinMode(Second_RF_Pin, OUTPUT);
    if(m == 1) {
      // One high, one low
      digitalWrite(First_RF_Pin, HIGH);
      digitalWrite(Second_RF_Pin, LOW);
    } else if(m == 2) {
      // Both low
      digitalWrite(First_RF_Pin, LOW);
      digitalWrite(Second_RF_Pin, LOW);
    } else if(m == 3) {
      // Both high
      digitalWrite(First_RF_Pin, HIGH);
      digitalWrite(Second_RF_Pin, HIGH);
    } else if(m == 4) {
      // Both high-Z
      pinMode(First_RF_Pin, INPUT);
      pinMode(Second_RF_Pin, INPUT);
    }
  }
}



// Utility function to print an error message if the number of arguments 
// to a command is incorrect.
void PrintNumArgError(int argc, char **argv, int expectedArgc) {
  expectedArgc--; // argc counts the command iteself, decrease to just count arguments
  Serial.print("#Error: ");
  Serial.print(argv[0]);
  Serial.print(" requires ");
  Serial.print(expectedArgc);
  Serial.print(" argument");
  if(expectedArgc != 1) {
    Serial.write('s');
  }
  Serial.print(". Received ");
  Serial.print(argc-1);
  Serial.println(".");
}


int32_t Str2Num(const char *str, uint8_t base)
{
    return strtol(str, NULL, base);
}


double Str2Double(char *str)
{
    return strtod(str, NULL);
}