#pragma once

#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"
#include "pio_stream.h"
#include "farey.h"
#include <cmath>
#include <stdio.h>

extern double CPU_freq_actual;
extern const int max_words;

void dma_handler();

class synth {
  public:
    synth(const uint8_t first_rf_pin, double frequency_Hz);
    ~synth();
    void disable_output();
    void enable_output();
    void set_dither_amplitude(float a) {dither_amplitude = a; needs_recalculation = true;};
    float get_dither_amplitude() {return dither_amplitude;};
    void set_amplitude(float a) {amplitude = a; needs_recalculation = true;};
    float get_amplitude() {return amplitude;};
    void set_hd3_amplitude(float a) {hd3_amplitude = a; needs_recalculation = true;};
    float get_hd3_amplitude() {return hd3_amplitude;};
    void set_hd3_phase(float p) {hd3_phase_rad = p; needs_recalculation = true;};
    float get_hd3_phase() {return hd3_phase_rad;};
    void set_frequency(double f) {frequency = f; needs_recalculation = true;};
    double get_frequency() {return frequency;};
    double get_frequency_exact();
    void set_mode(int m);
    int  get_mode() {return mode;};
    const char *get_mode_str();
    int get_n_words() {return n_words;};
    int get_n_periods() {return n_periods;};
    void set_max_words(int m) {max_words_limit = m; needs_recalculation = true;};
    int get_max_words() {return max_words_limit;};
    void calculate_buffers();
    void apply_settings();
    void restore_out_pins();
    
  private:
    static const uint8_t bits_per_word = 32u;

    uint8_t m_first_rf_pin;
    PIO pio = pio0;
    uint32_t sm;
    uint pio_prog_offset;
    const pio_program_t *pio_program;
    dma_channel_config synth_dma_cfg, restart_dma_cfg;
    float dither_amplitude;
    float amplitude;
    float hd3_amplitude;
    float hd3_phase_rad;
    int max_words_limit;
    double frequency;
    int mode; // 0 - CLKDIV, 1 - comparator, 2 - binary sigma delta, 3 - trinary sigma delta, 
              // 4 - click free binary sigma delta, 5 - click free trinary sigma delta
    int n_words, n_periods;
    bool needs_recalculation;

    void add_pio_program(const pio_program_t *prog);
    void remove_pio_program();
    void fill_synth_buffer_silent();
    void fill_synth_buffer_sigma_delta();
    void fill_synth_buffer_sigma_delta_3s();
    void fill_synth_buffer_compare();
    void setup_dma();
    void unclaim_dma();
};