#include <arduino.h>
#include <cstdlib>
#include "synth.h"
#include "toggle.h"
#include "commands.h"

double CPU_freq_actual = 200e6;

const int max_words = 15000;

// These variables have to be outside the class as they are used by the interrupt handler
static uint32_t synth_dma;
static uint32_t restart_dma;
static uint32_t synth_buffer[max_words] __attribute__((aligned(4)));
static uint32_t synth_buffer_ramp_up[max_words] __attribute__((aligned(4)));
static uint32_t synth_buffer_ramp_down[max_words] __attribute__((aligned(4)));
static uint32_t synth_buffer_silent[max_words] __attribute__((aligned(4)));
static uint32_t *synth_buffer_ptr[1];
static uint32_t *synth_buffer_ramp_up_ptr[1];
static uint32_t *synth_buffer_ramp_down_ptr[1];
static uint32_t *synth_buffer_silent_ptr[1];
static bool enable_transmit = false;


void synth::fill_synth_buffer_silent()
{
  synth_buffer_ptr[0] = synth_buffer;
  synth_buffer_ramp_up_ptr[0] = synth_buffer_ramp_up;
  synth_buffer_ramp_down_ptr[0] = synth_buffer_ramp_down;
  synth_buffer_silent_ptr[0] = synth_buffer_silent;
  for(int ii=0; ii < max_words; ii++) {
    synth_buffer_silent[ii] = 0;
  }
}


// Windowing function that takes a number between 0 and n_max and returns a smooth (raised cosine) taper
// value based on it. If falling is true, the taper goes from 1 to 0, otherwise from 0 to 1.
double taper(int n, int n_max, bool falling)
{
  double k, nf;

  nf = (double)n/(double)n_max;
  if(falling) {
    nf = 1.0 - nf;
  }
  k = 0.5*(1.0 - cos(nf * M_PI));
  return k;
}


// Use sigma-delta modulation to do 1-bit quantization of a sinusoid into the synth buffer
// based on the parameters already stored in the object.
// Also fill the ramp-up and ramp-down buffers.
void synth::fill_synth_buffer_sigma_delta()
{
  double phase, phase_increment;
  double sample, sample_up, sample_down;
  double acc, acc_up, acc_down;
  double out, out_up, out_down;
  double delta_dly, delta_dly_up, delta_dly_down;
  double dither;
  double epsilon = 1e-5; // To get a little bit away from the zero crossings
  uint32_t word, word_up, word_down;

  fill_synth_buffer_silent();
  phase_increment = 2 * M_PI * n_periods / ((double)n_words * 16.0);
  phase = 0;
  acc = 0;
  out = 0;
  delta_dly = 0;
  acc_up = 0;
  out_up = 0;
  delta_dly_up = 0;
  acc_down = 0;
  out_down = 0;
  delta_dly_down = 0;
  dither = 0;

  // Iterate over 32-bit words in the buffer
  for(int ii=0; ii < n_words; ii++) {
    word = 0;
    word_up = 0;
    word_down = 0;
    // Iterate over pairs of bits in the word.
    // Each bit is written first normally and then inverted in the neighboring bit to form a differential signal
    for(int jj=0; jj < 16; jj++) {
      phase = (ii*16 + jj)*phase_increment + epsilon;
      sample = amplitude * sin(phase) + hd3_amplitude*sin(3*phase + hd3_phase_rad);
      sample_up = sample * taper(ii*16 + jj, n_words*16, false);
      sample_down = sample * taper(ii*16 + jj, n_words*16, true);
      acc = sample + delta_dly;
      acc_up = sample_up + delta_dly_up;
      acc_down = sample_down + delta_dly_down;
      dither = rand()/(double)RAND_MAX; // 0 - 1
      dither = (dither - 0.5)*2*dither_amplitude;
      if(acc + dither > 0) {
        out = 1;
        word |= 1<<(2*jj);
      } else {
        out = -1;
        word |= 1<<(2*jj+1);
      }

      if(acc_up + dither > 0) {
        out_up = 1;
        word_up |= 1<<(2*jj);
      } else {
        out_up = -1;
        word_up |= 1<<(2*jj+1);
      }

      if(acc_down + dither > 0) {
        out_down = 1;
        word_down |= 1<<(2*jj);
      } else {
        out_down = -1;
        word_down |= 1<<(2*jj+1);
      }           
      delta_dly = acc - out;
      delta_dly_up = acc_up - out_up;
      delta_dly_down = acc_down - out_down;
    }
    if(ii < max_words) {
      synth_buffer[ii] = word;
      if(mode >= 4) {
        synth_buffer_ramp_up[ii] = word_up;
        synth_buffer_ramp_down[ii] = word_down;
      } else {
        synth_buffer_ramp_up[ii] = word;
        synth_buffer_ramp_down[ii] = 0;
      }
    }
  }
}


// Use sigma-delta modulation to do 1.5-bit quantization (3 levels) of a sinusoid into the synth buffer.
// based on the parameters already stored in the object.
// Also fill the ramp-up and ramp-down buffers.
void synth::fill_synth_buffer_sigma_delta_3s()
{
  double phase, phase_increment, dither;
  double sample, sample_up, sample_down;
  double acc, acc_up, acc_down;
  double out, out_up, out_down;
  double delta_dly, delta_dly_up, delta_dly_down;
  double epsilon = 1e-5; // To get a little bit away from the zero crossings
  uint32_t word, word_up, word_down;
  int last_equal, last_equal_up, last_equal_down; // Switch between keeping both high and both low when they shall be equal

  fill_synth_buffer_silent();
  phase_increment = 2 * M_PI * n_periods / ((double)n_words * 16.0);
  phase = 0;
  acc = 0;
  out = 0;
  delta_dly = 0;
  acc_up = 0;
  out_up = 0;
  delta_dly_up = 0;
  acc_down = 0;
  out_down = 0;
  delta_dly_down = 0;
  dither = 0;
  last_equal = 1;
  last_equal_up = 1;
  last_equal_down = 1;
  // Iterate over 32-bit words in the buffer
  for(int ii=0; ii < n_words; ii++) {
    word = 0;
    word_up = 0;
    word_down = 0;
    // Iterate over pairs of bits in the word.
    // Each bit is written first normally and then inverted in the neighboring bit to form a differential signal
    for(int jj=0; jj < 16; jj++) {
      phase = (ii*16 + jj)*phase_increment + epsilon;
      sample = amplitude * sin(phase) + hd3_amplitude*sin(3*phase + hd3_phase_rad);
      sample_up = sample * taper(ii*16 + jj, n_words*16, false);
      sample_down = sample * taper(ii*16 + jj, n_words*16, true);
      acc = sample + delta_dly;
      acc_up = sample_up + delta_dly_up;
      acc_down = sample_down + delta_dly_down;
      dither = rand()/(double)RAND_MAX; // 0 - 1
      dither = (dither - 0.5)*2*dither_amplitude;

      if(acc + dither > 1.0/3.0) {
        out = 1;
        word |= 1<<(2*jj);
      } else if(acc + dither > -1.0/3.0) {
        out = 0;
        if(last_equal == 0) {
          word |= 3<<(2*jj);
          last_equal = 1;
        } else {
          last_equal = 0;
        }
      } else {
        out = -1;
        word |= 1<<(2*jj+1);
      } 

      if(acc_up + dither > 1.0/3.0) {
        out_up = 1;
        word_up |= 1<<(2*jj);
      } else if(acc_up + dither > -1.0/3.0) {
        out_up = 0;
        if(last_equal_up == 0) {
          word_up |= 3<<(2*jj);
          last_equal_up = 1;
        } else {
          last_equal_up = 0;
        }
      } else {
        out_up = -1;
        word_up |= 1<<(2*jj+1);
      } 

      if(acc_down + dither > 1.0/3.0) {
        out_down = 1;
        word_down |= 1<<(2*jj);
      } else if(acc_down + dither > -1.0/3.0) {
        out_down = 0;
        if(last_equal_down == 0) {
          word_down |= 3<<(2*jj);
          last_equal_down = 1;
        } else {
          last_equal_down = 0;
        }
      } else {
        out_down = -1;
        word_down |= 1<<(2*jj+1);
      } 

      delta_dly = acc - out;
      delta_dly_up = acc_up - out_up;
      delta_dly_down = acc_down - out_down;
    }
    if(ii < max_words) {
      synth_buffer[ii] = word;
      if(mode >= 4) {
        synth_buffer_ramp_up[ii] = word_up;
        synth_buffer_ramp_down[ii] = word_down;
      } else {
        synth_buffer_ramp_up[ii] = word;
        synth_buffer_ramp_down[ii] = 0;
      }
    }
  }
}


// Do 1-bit quantization of a sinusoid into the synth buffer based on the parameters already stored in the object.
void synth::fill_synth_buffer_compare()
{
  double phase = 0, phase_increment, sample, dither;
  uint32_t word;
  double epsilon = 1e-5; // To get a little bit away from the zero crossings

  fill_synth_buffer_silent();
  phase_increment = 2 * M_PI * n_periods / ((double)n_words * 16.0);
  // Iterate over 32-bit words in the buffer
  for(int ii=0; ii < n_words; ii++) {
    word = 0;
    // Iterate over pairs of bits in the word.
    // Each bit is written first normally and then inverted in the neighboring bit to form a differential signal
    for(int jj=0; jj < 16; jj++) {
      phase = (ii*16 + jj)*phase_increment;
      sample = amplitude * sin(phase + epsilon);
      dither = rand()/(double)RAND_MAX; // 0 - 1
      dither = (dither - 0.5)*2*dither_amplitude;      
      if(sample + dither > 0) {
        word |= 1<<(2*jj);
      } else {
        word |= 1<<(2*jj+1);
      } 
    }
    if(ii < max_words) {
      synth_buffer[ii] = word;
      synth_buffer_ramp_up[ii] = word;
      synth_buffer_ramp_down[ii] = 0;
    }
  }
}


// Inspired by:
// https://101-things.readthedocs.io/en/latest/ham_transmitter.html
// https://github.com/dawsonjon/101Things/blob/master/18_transmitter/nco.cpp
// and
// https://gregchadwick.co.uk/blog/playing-with-the-pico-pt2/
// https://github.com/GregAC/pico-stuff/blob/main/pwm_dma/pwm_dma_interrupt_sequence.c
// and
// https://github.com/raspberrypi/pico-examples/blob/master/dma/channel_irq/channel_irq.c


void dma_irq_handler()
{
  static int dma_state = 0;
  // 0 - silent
  // 1 - transmitting
  /*
  digitalWrite(26, HIGH);
  digitalWrite(26, LOW);
  digitalWrite(26, HIGH);
*/
  if(dma_channel_get_irq0_status(restart_dma)) {
    dma_hw->ints0 = 1u << restart_dma; // Acknowledge interrupt
    if(!dma_channel_is_busy(restart_dma)) {
      if(enable_transmit) {
        if(dma_state == 1) {
          dma_channel_set_read_addr(restart_dma, synth_buffer_ptr, false);
        } else if(dma_state == 0){
          dma_channel_set_read_addr(restart_dma, synth_buffer_ramp_up_ptr, false);
          digitalWrite(26, HIGH);
          dma_state = 1;
        }
      } else {
        if(dma_state == 0) {
          dma_channel_set_read_addr(restart_dma, synth_buffer_silent_ptr, false);
        } else if(dma_state == 1){
          dma_channel_set_read_addr(restart_dma, synth_buffer_ramp_down_ptr, false);
          digitalWrite(26, LOW);
          dma_state = 0;
        }
      }
    }
  }
}


void synth::disable_output()
{
  if(enable_transmit && mode == 0) {
    pio_sm_set_consecutive_pindirs(pio, sm, m_first_rf_pin, 2, false);
  }
  enable_transmit = false;
}


void synth::enable_output()
{
  if(!enable_transmit && mode == 0) {
    pio_sm_set_consecutive_pindirs(pio, sm, m_first_rf_pin, 2, true);
  }
  enable_transmit = true;
}


double synth::get_frequency_exact()
{
  if(mode != 0) {
    return CPU_freq_actual * (double) n_periods / (16 * (double) n_words);
  } else {
    float clkdiv = round(256.0*CPU_freq_actual/(2.0*frequency))/256.0;
    return CPU_freq_actual/(2*clkdiv);
  }
}


void synth::set_mode(int m)
{
  if(m >= 0 && m <= 5) {
    mode = m;
    needs_recalculation = true;
  } else {
    Serial.println("Attempted to set invalid mode");
  }
}


const char *synth::get_mode_str()
{
  switch(mode) {
    case 0:
      return "CLKDIV";
    case 1:
      return "Comparator";
    case 2:
      return "Binary sigma delta";
    case 3:
      return "Trinary sigma delta";
    case 4:
      return "Click-free binary sigma delta";
    case 5:
      return "Click-free trinary sigma delta";
    default:
      return "???";
  }
}


// (Re)calculate the buffers
void synth::calculate_buffers()
{
  rational_t PperW; // Periods per 32-bit word as a rational number
  uint32_t n_mult;

  Serial.println("Calculating buffers...");

  PperW = rational_approximation(frequency * 16.0 / (double)CPU_freq_actual, min(max_words, max_words_limit));
  n_periods = PperW.numerator;
  n_words = PperW.denominator;

  Serial.print("n_words = ");
  Serial.println(get_n_words());
  Serial.print("n_periods = ");
  Serial.println(get_n_periods());

  n_mult = floor(max_words/n_words);
  // Make the buffer at least half of max_words so that the interrupt has plenty of time to do its job. 
  n_periods *= n_mult;
  n_words *= n_mult;

  
  Serial.print("n_words = ");
  Serial.println(get_n_words());
  Serial.print("n_periods = ");
  Serial.println(get_n_periods());

  if(mode == 1) {
    fill_synth_buffer_compare();
  } else if(mode == 2 or mode == 4) {
    fill_synth_buffer_sigma_delta();
  } else {
    fill_synth_buffer_sigma_delta_3s();
  }
  needs_recalculation = false;
}


// Apply the frequency, mode etc settings, i.e. calculate new waveforms based on the settings.
// But only if necessary;
void synth::apply_settings()
{
  if(!needs_recalculation) {
    return;
  }
  if(synth_dma < 1000) {
    // dma_channel_abort does not seem to work for chained DMAs
    // Write zeros to the control registers as recommended here:
    // (https://forums.raspberrypi.com/viewtopic.php?t=330119)
    // https://forums.raspberrypi.com/viewtopic.php?t=337439
    Serial.println("Waiting for DMAs to stop...");
    hw_clear_bits(&dma_hw->ch[synth_dma].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    hw_clear_bits(&dma_hw->ch[restart_dma].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    do {
      // This loop might not be necessary
      dma_channel_abort(synth_dma);
      dma_channel_abort(restart_dma);
    } while(dma_channel_is_busy(synth_dma) || dma_channel_is_busy(restart_dma));
   unclaim_dma(); 
  }

  remove_pio_program();
  if(mode == 0) {
    add_pio_program(&toggle_program);
    float clkdiv = CPU_freq_actual/(2.0*frequency);
    toggle_program_init(pio, sm, pio_prog_offset, m_first_rf_pin, clkdiv);
  } else {
    Serial.println("Adding PIO program...");
    add_pio_program(&pio_serialiser_program);
    pio_serialiser_program_init(pio, sm, pio_prog_offset, m_first_rf_pin, 1.0); 
    calculate_buffers();
    // Restart the DMAs
    Serial.println("Restarting DMAs");
    setup_dma();
  }
  PrintStatus();
}


void synth::remove_pio_program()
{
  if(pio_program != NULL) {
    pio_remove_program(pio, pio_program, pio_prog_offset);
    pio_sm_unclaim(pio, sm);
  }
  pio_program = NULL;
}


void synth::add_pio_program(const pio_program_t *prog)
{
  pio_program = prog;
  pio_prog_offset = pio_add_program(pio, pio_program);
  sm = pio_claim_unused_sm(pio, true);
  // Need to call the correct PIO init function after this function has been called.
}


synth::synth(const uint8_t first_rf_pin, double frequency_a)
{
  m_first_rf_pin = first_rf_pin;
  frequency = frequency_a;
  dither_amplitude = 1.0;
  max_words_limit = max_words;
  amplitude = 1.0;
  hd3_amplitude = 0.045;
  hd3_phase_rad = -35.0 * M_PI/180.0;
  mode = 5;
  n_words = max_words; // Dummy value for now
  needs_recalculation = true;

  calculate_buffers();

  // The PIO contains a very simple program that waits for a pin to go high
  // then repeatedly reads a 32-bit word from the FIFO and sends 2 bits per 
  // clock to two IO pins.
  add_pio_program(&pio_serialiser_program);
  pio_serialiser_program_init(pio, sm, pio_prog_offset, first_rf_pin, 1.0); 
  setup_dma();
}


void synth::setup_dma()
{
  // Configure DMA from memory to PIO SM TX FIFO
  synth_dma = dma_claim_unused_channel(true);
  restart_dma = dma_claim_unused_channel(true);
  synth_dma_cfg = dma_channel_get_default_config(synth_dma);
  channel_config_set_transfer_data_size(&synth_dma_cfg, DMA_SIZE_32);
  channel_config_set_read_increment(&synth_dma_cfg, true);
  channel_config_set_write_increment(&synth_dma_cfg, false);
  channel_config_set_dreq(&synth_dma_cfg, pio_get_dreq(pio, sm, true)); // Do a DMA transfer each time the PIO FIFO requests it
  channel_config_set_chain_to(&synth_dma_cfg, restart_dma);
  // Write to the SM TX FIFO, provide the buffer address, n_words x 32 bit transfers, do not yet start
  dma_channel_configure(synth_dma, &synth_dma_cfg, &pio->txf[sm], synth_buffer, n_words, false);

  // Use a second DMA to reconfigure the first
  restart_dma_cfg = dma_channel_get_default_config(restart_dma);
  channel_config_set_transfer_data_size(&restart_dma_cfg, DMA_SIZE_32);
  channel_config_set_read_increment(&restart_dma_cfg, true); // increment the read address, needed for the DMA handler to have proper effect
  channel_config_set_write_increment(&restart_dma_cfg, false); // do not increment the write address
  dma_channel_set_irq0_enabled(restart_dma, true);
  irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler); 
  irq_set_enabled(DMA_IRQ_0, true);
  // Write to the DMA read pointer, provide the buffer address, 2 words x 32 bit, start
  dma_channel_configure(restart_dma, &restart_dma_cfg, &dma_hw->ch[synth_dma].al3_read_addr_trig, synth_buffer_ramp_up_ptr, 1, true);  
}


void synth::unclaim_dma()
{
  dma_channel_cleanup(synth_dma);
  dma_channel_cleanup(restart_dma);
  dma_channel_unclaim(synth_dma);
  dma_channel_unclaim(restart_dma);
  synth_dma = 999999; // Set to some unrealistic value to signal that it is not valid
  restart_dma = 999999;
}


synth::~synth() {
  pio_sm_unclaim(pio, sm);
  unclaim_dma();
}


// Let the PIO regain control of the out pins.
void synth::restore_out_pins()
{
  pio_gpio_init(pio, m_first_rf_pin);
  pio_gpio_init(pio, m_first_rf_pin+1);
}

