#pragma once

#include "synth.h"

extern synth *rf_synth;
extern double target_freqs[];
extern int current_freq_num;

extern bool key_down;     // Whether to transmit continuously
extern int morse_rate;    // Morse rate in words per minute

extern const int fox_len; // Length of fox_string
extern char fox_string[]; // String to send as fox identifier

extern const int call_len; // Length of callsign
extern char callsign[];   // String to send as callsign

extern const int First_RF_Pin;
extern const int Second_RF_Pin;


void initMorseRate(uint32_t WPM);