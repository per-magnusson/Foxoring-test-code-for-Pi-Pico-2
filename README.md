# Foxoring-test-code-for-Pi-Pico-2
Code to interactively test various ways of generating RF signals in the 80m band 
(primarily 3.5-3.6 MHz) using a Raspberry Pi Pico 2 (or Pi Pico). Intended to 
eventually be used in a foxoring transmitter, but could perhaps also be useful 
for other QRP transmitters.

The operation is controlled via a serial port terminal. Type "?" or "help" to get information
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
