#include <stdio.h>
#include <cmath>
#include <EEPROM.h>
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "pico/binary_info.h"
#include "pico/stdlib.h"

#include "kiteTRAK.h"

uint8_t in_buf[BUF_LEN];
uint8_t dma_buf[FRAME_LEN];

// DMA state for non-blocking SPI transfers
static int kt_dma_chan = -1;
static volatile bool kt_dma_busy = false;
static volatile bool kt_dma_data_ready = false;

// DMA IRQ handler forward declaration
static void kt_dma_irq_handler(void);

// DMA IRQ handler: clears DMA interrupt, copies data to input buffer, and marks data ready
static void kt_dma_irq_handler(void) {
  if (kt_dma_chan >= 0) {
    dma_hw->ints0 = 1u << kt_dma_chan; // clear IRQ for our channel
    // Copy received data from DMA buffer to input buffer
    size_t sof_idx = FRAME_LEN;
    for (size_t i = 0; i < FRAME_LEN; ++i) {
      if (dma_buf[i] == SOF_MARKER) {
        sof_idx = i;
        break;
      }
    }

    if (sof_idx < FRAME_LEN &&
        dma_buf[(sof_idx + FRAME_LEN - 1) % FRAME_LEN] == EOF_MARKER) {
      for (size_t i = 0; i < BUF_LEN; ++i) {
        in_buf[i] = dma_buf[(sof_idx + 1 + i) % FRAME_LEN];
      }
    }
    kt_dma_data_ready = true;
  }
  kt_dma_busy = false;
}

Buttons btn;
Buttons oldButton;
PhobConfig phob_config; // PhobGCC configuration data
uint8_t btn_arr1[8];
uint8_t btn_arr2[8];

bool down_pressed = false;
bool button_pressed = false;
int disp_mode = MODE_INPUT; // Default display mode
bool disp_menu = false;

// Default views
int input_view_index = 0;
int ipm_view_index = 0;
int anim_view_index = 0;
int cal_view_index = 0;

// Calibration help menu
bool cal_help_menu = false;
unsigned long last_flash_ms = 0;

struct DefaultScreenSettings {
  uint32_t magic;
  int disp_mode;
  int input_view_index;
  int ipm_view_index;
  int anim_view_index;
  int cal_view_index;
};

const uint32_t DEFAULT_SCREEN_MAGIC = 0x4B54524B;
const unsigned long DEFAULT_SCREEN_HOLD_MS = 3000;
unsigned long default_screen_hold_start = 0;
bool default_screen_hold_active = false;
bool default_screen_saved = false;
unsigned long default_screen_show_until = 0;

// Idle display timeout (2 minutes = 120000 ms)
const unsigned long DISPLAY_TIMEOUT_MS = 120000;
unsigned long last_input_ms = 0;
bool display_is_off = false;

// Safe-mode overlay handling
unsigned long sm_show_until = 0; // timestamp (ms) until which to show overlay
bool sm_state_to_show = false;    // which graphic to show when overlay active
bool sm_initialized = false;     // avoid triggering overlay on first sample
bool prev_safe_mode = false;     // last seen safe_mode state

int input_count = 0;
// Rolling IPM (inputs per minute) buffer settings (0.25s buckets)
const int WINDOW_SECONDS = 6; // total rolling window in seconds
const int BUCKET_MS = 250; // each bucket duration in milliseconds (0.25s)
const int ROLLING_BUCKETS = (WINDOW_SECONDS * 1000) / BUCKET_MS; // 24 buckets for 6s (0.25s buckets)
int bucket_counts[ROLLING_BUCKETS] = {0}; // raw event counts per bucket
int ipm_buffer[ROLLING_BUCKETS] = {0}; // rolling sum (total IPM) snapshot at each bucket boundary for plotting
int ipm_index = 0; // circular buffer index
int events_this_bucket = 0; // events counted in the current 0.25s bucket
unsigned long last_bucket_ms = 0; // last bucket timestamp in ms
int rolling_sum = 0; // sum of counts in buffer
int inputs_per_minute = 0; // computed IPM (scaled from WINDOW_SECONDS)
int max_inputs_per_minute = 0; // track peak IPM
const int IPM_SCALE = 60 / WINDOW_SECONDS; // scale factor to convert window to per-minute (60/6 = 10)
const int MAX_IPM = 1000; // graph vertical axis max in inputs-per-minute
const int IPM_FLAME = 700; // IPM level to trigger flame animation

// Splash animation state
const int MAX_SPLASHES = 30;
struct Splash {
  int x, y;
  unsigned long start_time;
  bool active;
};
Splash splashes[MAX_SPLASHES];

// Core 0 SPI Setup and Loop
//===============================================================//

void setup() {
  spi_init(spi0, 1000*1000); //1MHz Baudrate
  spi_set_slave(spi0, true);

  gpio_set_function(4, GPIO_FUNC_SPI); // MISO (Rx)
  gpio_set_function(6, GPIO_FUNC_SPI); // SCK
  gpio_set_function(3, GPIO_FUNC_SPI); // MOSI (Tx)
  gpio_set_function(5, GPIO_FUNC_SPI); // CS

  // Initialize DMA channel for SPI RX (only once)
  if (kt_dma_chan < 0) {
    kt_dma_chan = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(kt_dma_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, false);  // SPI DR is fixed address
    channel_config_set_write_increment(&c, true);  // Write incrementally to buffer
    channel_config_set_dreq(&c, DREQ_SPI0_RX);    // Use SPI0 RX DMA request
    
    dma_channel_set_config(kt_dma_chan, &c, false);
    dma_channel_set_irq0_enabled(kt_dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, kt_dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);
  }

  for(size_t i = 0; i < BUF_LEN; ++i) {
    in_buf[i] = 0;
  }
  for(size_t i = 0; i < FRAME_LEN; ++i) {
    dma_buf[i] = 0;
  }
}

void loop() {
  if (spi_is_readable(spi0)) {
    // If DMA available and not busy, kick off a non-blocking DMA transfer
    if (kt_dma_chan >= 0 && !kt_dma_busy) {
      kt_dma_busy = true;
      kt_dma_data_ready = false;
      
      // Configure and start DMA transfer: read from SPI0 DR, write to dma_buf
      dma_channel_config c = dma_channel_get_default_config(kt_dma_chan);
      channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
      channel_config_set_read_increment(&c, false);   // SPI DR is fixed address
      channel_config_set_write_increment(&c, true);   // Write incrementally to buffer
      channel_config_set_dreq(&c, DREQ_SPI0_RX);      // Use SPI0 RX DMA request
      
      dma_channel_configure(kt_dma_chan, &c,
                            dma_buf,                // destination: DMA buffer
                            &spi_get_hw(spi0)->dr,  // source: SPI data register
                            FRAME_LEN,             // transfer count
                            true);                  // start immediately
    }
  }
}

// Core 1 Display Setup and Loop
//===============================================================//

void setup1() {
  busy_wait_ms(150);
  display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
  display.clearDisplay();

  EEPROM.begin(sizeof(DefaultScreenSettings));
  DefaultScreenSettings default_screen;
  EEPROM.get(0, default_screen);
  if (default_screen.magic == DEFAULT_SCREEN_MAGIC &&
      default_screen.disp_mode >= 0 && default_screen.disp_mode < NUM_MODES &&
      default_screen.input_view_index >= 0 && default_screen.input_view_index < 3 &&
      default_screen.ipm_view_index >= 0 && default_screen.ipm_view_index < 2 &&
      default_screen.anim_view_index >= 0 && default_screen.anim_view_index < 3 &&
      default_screen.cal_view_index >= 0 && default_screen.cal_view_index < 7) {
    disp_mode = default_screen.disp_mode;
    input_view_index = default_screen.input_view_index;
    ipm_view_index = default_screen.ipm_view_index;
    anim_view_index = default_screen.anim_view_index;
    cal_view_index = default_screen.cal_view_index;
  }

  // Initialize splash array
  for (int i = 0; i < MAX_SPLASHES; i++) {
    splashes[i].active = false;
  }

  // Initialize last input time
  last_input_ms = to_ms_since_boot(get_absolute_time());

  boot_animation(); // comment out to skip boot animation 
}

void loop1() {
  // Assign button states to btn
  assign_buttons(btn, in_buf);

  // Parse PhobGCC configuration data
  parse_phob_config(phob_config, in_buf);

  // Check for any input activity to track idle timeout
  unsigned long now_ms = to_ms_since_boot(get_absolute_time());
  
  // Check for controller being idle
  if (btn.A == 1 || btn.B == 1 || btn.X == 1 || 
      btn.Y == 1 || btn.Z == 1 || btn.S == 1 ||
      btn.L == 1 || btn.R == 1 || btn.Dl == 1 ||
      btn.Dr == 1 || btn.Du == 1 || btn.Dd == 1) {
    last_input_ms = now_ms;
    display_is_off = false;
  }
  
  // Check if display should be turned off due to inactivity
  if (!display_is_off && (now_ms - last_input_ms) > DISPLAY_TIMEOUT_MS) {
    display_is_off = true;
  }
  
  // If display is off, show black screen and return early
  if (display_is_off) {
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    oldButton = btn; // Still update oldButton to detect when input resumes
    return;
  } else {
    display.ssd1306_command(SSD1306_DISPLAYON);
  }

  // Save the current screen as the boot default after holding Z + Start for 3 seconds.
  if (btn.Z && btn.S) {
    if (!default_screen_hold_active) {
      default_screen_hold_active = true;
      default_screen_hold_start = now_ms;
    } else if (!default_screen_saved &&
               now_ms - default_screen_hold_start >= DEFAULT_SCREEN_HOLD_MS) {
      DefaultScreenSettings default_screen = {
        DEFAULT_SCREEN_MAGIC,
        disp_mode,
        input_view_index,
        ipm_view_index,
        anim_view_index,
        cal_view_index
      };
      EEPROM.put(0, default_screen);
      EEPROM.commit();
      default_screen_saved = true;
      default_screen_show_until = now_ms + 2000;
    }
  } else {
    default_screen_hold_active = false;
    default_screen_saved = false;
  }

  // Detect safe_mode changes and trigger 2s overlay when changed
  if (!sm_initialized) {
    prev_safe_mode = phob_config.safe_mode;
    sm_initialized = true;
  } else if (phob_config.safe_mode != prev_safe_mode) {
    // record which graphic to show and show for 2 seconds
    sm_state_to_show = phob_config.safe_mode; // true => enabled, false => disabled
    sm_show_until = now_ms + 2000;
    prev_safe_mode = phob_config.safe_mode;
  }

  // Check if safe-mode overlay should be shown
  bool show_sm_overlay = (now_ms < sm_show_until && !disp_menu);
  bool show_default_screen_overlay = (now_ms < default_screen_show_until && !disp_menu);

  // If overlay is active, skip normal rendering and show overlay
  if (show_sm_overlay || show_default_screen_overlay) {
    display.clearDisplay();
    if (show_sm_overlay && sm_state_to_show) {
      draw_sm_enabled();
    } else if (show_sm_overlay) {
      draw_sm_disabled();
    } else {
      draw_boot_screen();
    }
    display.display();
    return; // Skip normal display mode rendering
  }

  // Display Modes
  //=============================================================//

  // Display Input (sticks / buttons) and allow cycling with D-pad down
  if (disp_mode == MODE_INPUT && !disp_menu) {
    display.clearDisplay();

    if (input_view_index == 0) {
      // Sticks view
      display_stick(btn, "a");
      display.setCursor(0, 46);
      display.printf("Ax:% 7.4f", btn.Ax_melee);
      display.setCursor(0, 55);
      display.printf("Ay:% 7.4f", btn.Ay_melee);

      display_stick(btn, "c");
      display.setCursor(68, 0);
      display.printf("Cx:% 7.4f", btn.Cx_melee);
      display.setCursor(68, 9);
      display.printf("Cy:% 7.4f", btn.Cy_melee);
    } else if (input_view_index == 1) {
      // Buttons view
      draw_dpad(2, 0);
      display.drawCircle(93, 28, 10, 1);                  // a
      display.drawCircle(81, 42, 5, 1);                   // b
      display.drawBitmap(98, 10, xbutton_bmp, 15, 13, 1); // x
      display.drawBitmap(74, 10, ybutton_bmp, 15, 13, 1); // y
      display.drawRoundRect(93, 1, 20, 5, 2, 1);          // z
      display.drawCircle(59, 51, 3, 1);                   // start
      // L trigger: outline box (filled upward based on btn.La value)
      display.drawRect(2, 1, 7, 54, 1);
      int la_fill_height = (btn.La * 54) / 255; // scale 0-255 to 0-54 pixels
      if (la_fill_height > 0) {
        display.fillRect(2, 55 - la_fill_height, 7, la_fill_height, 1); // fill upward from bottom
      }

      // R trigger: outline box (filled upward based on btn.Ra value)
      display.drawRect(119, 1, 7, 54, 1);
      int ra_fill_height = (btn.Ra * 54) / 255; // scale 0-255 to 0-54 pixels
      if (ra_fill_height > 0) {
        display.fillRect(119, 55 - ra_fill_height, 7, ra_fill_height, 1); // fill upward from bottom
      }

      // Trigger value readouts beneath
      display.setTextSize(1);
      display.setFont();
      display.setCursor(0, 57);
      display.printf("%03d", btn.La);
      display.setCursor(111, 57);
      display.printf("%03d", btn.Ra);

      if (btn.A) {
        display.fillCircle(93, 28, 10, 1);
      }
      if (btn.B) {
        display.fillCircle(81, 42, 5, 1);
      }
      if (btn.X) {
        display.drawBitmap(98, 10, xbutton_pressed_bmp, 15, 13, 1);
      }
      if (btn.Y) {
        display.drawBitmap(74, 10, ybutton_pressed_bmp, 15, 13, 1);
      }
      if (btn.Z) {
        display.fillRoundRect(93, 1, 20, 5, 2, 1);
      }
      if (btn.S) {
        display.fillCircle(59, 51, 3, 1);
      }
      if (btn.Dl) {
        display.fillRect(22, 27, 9, 5, 1);
      }
      if (btn.Dr) {
        display.fillRect(36, 27, 9, 5, 1);
      }
      if (btn.Du) {
        display.fillRect(31, 18, 5, 9, 1);
      }
      if (btn.Dd) {
        display.fillRect(31, 32, 5, 9, 1);
      }
      if (btn.L) {
        display.fillRect(2, 1, 7, 54, 1);
      }
      if (btn.R) {
        display.fillRect(119, 1, 7, 54, 1);
      }

    } else if (input_view_index == 2) {
      // Button/Sticks dashboard view
      display_buttons_dash(btn, in_buf);
    } 
    
    display.display();

    // Cycle between input sub-views with D-pad down/up (debounced)
    if (btn.Dd && btn.Z && !down_pressed) {
      input_view_index = (input_view_index + 1) % 3; // 0=sticks,1=buttons,2=dashboard
      down_pressed = true;
    } else if (btn.Du && btn.Z && !down_pressed) {
      input_view_index = (input_view_index - 1 + 3) % 3; // cycle backwards
      down_pressed = true;
    } else if (!btn.Dd && !btn.Du) {
      down_pressed = false;
    }
  }

  // Display Inputs Per Minute (IPM) Visuals
  if (disp_mode == MODE_IPM && !disp_menu) {

    if (btn.A && !oldButton.A) events_this_bucket++;
    if (btn.B && !oldButton.B) events_this_bucket++;
    if (btn.X && !oldButton.X) events_this_bucket++;
    if (btn.Y && !oldButton.Y) events_this_bucket++;
    if (btn.L && !oldButton.L) events_this_bucket++;
    if (btn.R && !oldButton.R) events_this_bucket++;
    if (btn.Z && !oldButton.Z) events_this_bucket++;

    if ((btn.Ax-ORG >= 23 && oldButton.Ax-ORG < 23) || (abs(btn.Ax-ORG) < 23 && abs(oldButton.Ax-ORG) >= 23) || (btn.Ax-ORG <= -23 && oldButton.Ax-ORG > -23)) events_this_bucket++;
    if ((btn.Ay-ORG >= 23 && oldButton.Ay-ORG < 23) || (abs(btn.Ay-ORG) < 23 && abs(oldButton.Ay-ORG) >= 23) || (btn.Ay-ORG <= -23 && oldButton.Ay-ORG > -23)) events_this_bucket++;
    if ((btn.Cx-ORG >= 23 && oldButton.Cx-ORG < 23) || (abs(btn.Cx-ORG) < 23 && abs(oldButton.Cx-ORG) >= 23) || (btn.Cx-ORG <= -23 && oldButton.Cx-ORG > -23)) events_this_bucket++;
    if ((btn.Cy-ORG >= 23 && oldButton.Cy-ORG < 23) || (abs(btn.Cy-ORG) < 23 && abs(oldButton.Cy-ORG) >= 23) || (btn.Cy-ORG <= -23 && oldButton.Cy-ORG > -23)) events_this_bucket++;

    // update oldButton snapshot
    oldButton = btn;

    // Update rolling buffer at bucket boundaries (every BUCKET_MS)
    unsigned long now_ms = to_ms_since_boot(get_absolute_time());
    unsigned long now_bucket = now_ms / BUCKET_MS;
    unsigned long last_bucket = last_bucket_ms / BUCKET_MS;
    if (now_bucket != last_bucket) {
      // Subtract outgoing bucket count from rolling sum
      rolling_sum -= bucket_counts[ipm_index];
      // Store new bucket event count
      bucket_counts[ipm_index] = events_this_bucket;
      // Add incoming bucket count to rolling sum
      rolling_sum += bucket_counts[ipm_index];
      // Store rolling sum (in IPM units) for plotting
      ipm_buffer[ipm_index] = rolling_sum;
      ipm_index = (ipm_index + 1) % ROLLING_BUCKETS;

      // compute IPM (which is already the rolling_sum)
      inputs_per_minute = rolling_sum * IPM_SCALE;
      
      // update max IPM if current exceeds it
      if (inputs_per_minute > max_inputs_per_minute) {
        max_inputs_per_minute = inputs_per_minute;
      }

      // reset counter for new bucket and update timestamp
      events_this_bucket = 0;
      last_bucket_ms = now_ms;
    }
      // Toggle minimal view with D-pad down/up (debounced)
      if (btn.Dd && btn.Z && !down_pressed) {
        ipm_view_index = (ipm_view_index + 1) % 2; // 0=full,1=minimal
        down_pressed = true;
      } else if (btn.Du && btn.Z && !down_pressed) {
        ipm_view_index = (ipm_view_index - 1 + 2) % 2; // cycle backwards
        down_pressed = true;
      } else if (!btn.Dd && !btn.Du) {
        down_pressed = false;
      }

      if (ipm_view_index == 0) {
        // Minimal: show only IPM label and numeric value
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setFont();
        display.setCursor(73, 4);
        display.print("Max:");
        display.setCursor(102, 4);
        display.print(max_inputs_per_minute);

        display.setFont(&FreeSansBoldOblique12pt7b);
        
        // Measure text width to center horizontally
        int16_t x1, y1;
        uint16_t w, h;
        char ipm_str[16];
        snprintf(ipm_str, sizeof(ipm_str), "%d", inputs_per_minute);
        display.getTextBounds(ipm_str, 0, 0, &x1, &y1, &w, &h);
        
        // Center x and position y
        int text_x = (SCREEN_WIDTH - w) / 2;
        display.setCursor(text_x, 40);
        display.print(ipm_str);

        if (inputs_per_minute > IPM_FLAME) {
          static int flame_frame = 0;
          display.drawBitmap(0, 12, epd_bitmap_allArray_flame[flame_frame], 128, 64, SSD1306_WHITE);
          flame_frame = (flame_frame + 1) % 157;
        }

        display.display();
      } else {
        display.clearDisplay();

        // Header: IPM value (full view)
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setFont();
        display.setCursor(2, 4);
        display.print("IPM:");
        display.setCursor(32, 4);
        display.print(inputs_per_minute);
        display.setCursor(73, 4);
        display.print("Max:");
        display.setCursor(102, 4);
        display.print(max_inputs_per_minute);

        if (inputs_per_minute > IPM_FLAME) {
          static int flame_frame = 0;
          display.drawBitmap(0, 12, epd_bitmap_allArray_flame[flame_frame], 128, 64, SSD1306_WHITE);
          flame_frame = (flame_frame + 1) % 157;
        }

        // Small moving bar graph of per-bucket counts
        const int graph_x = 0;
        const int graph_y = 12;
        const int graph_w = 128;
        const int graph_h = 52;
        const int buckets = ROLLING_BUCKETS; // should be 24
        int bar_w = graph_w / buckets; // integer width per bar

        // Fixed scaling: graph axis is directly in IPM (0 to MAX_IPM)
        float scale = (float)graph_h / (float)MAX_IPM; // pixels per IPM

        // Draw border for graph area at screen edges
        display.drawRect(0, 12, 128, 52, SSD1306_WHITE);

        // Draw connected line plot: oldest at left (ipm_index is next write slot)
        int prev_x = 0;
        int prev_y = 0;
        bool has_prev = false;
        for (int k = 0; k < buckets; ++k) {
          int buf_idx = (ipm_index + k) % buckets;
          int v = ipm_buffer[buf_idx]; // v is in raw event counts
          // Convert to IPM for scaling
          int v_ipm = v * IPM_SCALE;
          // center x of the bucket
          int x = graph_x + k * bar_w + (bar_w / 2);
          // y coordinate: invert so larger values are higher on screen
          int y = graph_y + graph_h - (int)(v_ipm * scale + 0.5f);

          // clamp y within graph bounds
          if (y < graph_y) y = graph_y;
          if (y > graph_y + graph_h) y = graph_y + graph_h;

          if (has_prev) {
            display.drawLine(prev_x, prev_y, x, y, SSD1306_WHITE);
          }
          // draw a pixel/point at the sample
          display.drawPixel(x, y, SSD1306_WHITE);
          prev_x = x;
          prev_y = y;
          has_prev = true;
        }

        display.display();
      }
  }

  // Display Animations (rain / fire) and allow cycling with D-pad down
  if (disp_mode == MODE_ANIMATIONS && !disp_menu) {
    display.clearDisplay();

    static int rain_frame = 0;
    static int flame_frame = 0;
    static unsigned long last_frame_ms = 0;
    unsigned long now_ms = to_ms_since_boot(get_absolute_time());

    // Update frame every ~33ms for ~30 fps
    if (now_ms - last_frame_ms >= 33) {
      if (anim_view_index == 0) {
        rain_frame = (rain_frame + 1) % 47;
      } else {
        flame_frame = (flame_frame + 1) % 157;
      }
      last_frame_ms = now_ms;
    }

    if (anim_view_index == 0) {
      display.drawBitmap(0, 0, epd_bitmap_allArray_rain[rain_frame], 128, 64, SSD1306_WHITE);
    } else if (anim_view_index == 1) {
      display.drawBitmap(0, 0, epd_bitmap_allArray_flame[flame_frame], 128, 64, SSD1306_WHITE);
    } else if (anim_view_index == 2) {
      // Button ripple effect (check if any button was just pressed)
      if ((btn.A && !oldButton.A) || (btn.B && !oldButton.B) || 
          (btn.X && !oldButton.X) || (btn.Y && !oldButton.Y) || 
          (btn.Z && !oldButton.Z) || (btn.S && !oldButton.S) ||
          (btn.Du && !oldButton.Du) || (btn.Dd && !oldButton.Dd) ||
          (btn.Dl && !oldButton.Dl) || (btn.Dr && !oldButton.Dr)) {
        // Find first inactive splash and create new one
        for (int i = 0; i < MAX_SPLASHES; i++) {
          if (!splashes[i].active) {
            splashes[i].x = rand() % (SCREEN_WIDTH - 10) + 10;
            splashes[i].y = rand() % (SCREEN_HEIGHT - 10) + 10;
            splashes[i].start_time = to_ms_since_boot(get_absolute_time());
            splashes[i].active = true;
            break;
          }
        }
      }
      
      // Draw all active splash animations
      const int SPLASH_DURATION = 2000; // total duration in ms
      const int RING_COUNT = 3;
      const int RING_DELAY = SPLASH_DURATION / RING_COUNT; // delay between rings
      const int MAX_RADIUS = 15; // maximum radius of rings
      const int FIZZLE_DURATION = 300; // duration for fizzle-out effect in ms
      
      for (int s = 0; s < MAX_SPLASHES; s++) {
        if (splashes[s].active) {
          unsigned long elapsed = to_ms_since_boot(get_absolute_time()) - splashes[s].start_time;
          
          // Draw each ring with overlap (back-to-back)
          for (int ring = 0; ring < RING_COUNT; ring++) {
            int ring_start = ring * (2 * RING_DELAY / 3);
            int ring_expand_end = ring_start + RING_DELAY;
            int fizzle_start = ring_expand_end - FIZZLE_DURATION; // fizzle starts before expansion ends
            
            if (elapsed >= ring_start && elapsed < ring_expand_end) {
              // This ring is expanding
              int age = elapsed - ring_start;
              int radius = (age * MAX_RADIUS) / RING_DELAY;
              
              if (elapsed < fizzle_start) {
                // Normal expansion - full circle
                display.drawCircle(splashes[s].x, splashes[s].y, radius, SSD1306_WHITE);
              } else {
                // Fizzle phase during final expansion - draw with increasing gaps
                int fizzle_age = elapsed - fizzle_start;
                int gap_size = (fizzle_age * 8) / FIZZLE_DURATION; // gaps grow from 0 to 8 pixels
                
                // Draw circle as dashes/segments that disappear
                for (int angle = 0; angle < 360; angle += (4 + gap_size)) {
                  int angle_rad = angle;
                  int next_angle = angle + (4 - gap_size / 2); // shrinking segment
                  if (next_angle > angle) {
                    // Draw a small arc segment
                    for (int a = angle_rad; a < next_angle && a < 360; a += 2) {
                      float rad = (a * 3.14159 / 180.0);
                      int px = splashes[s].x + (radius * cos(rad));
                      int py = splashes[s].y + (radius * sin(rad));
                      display.drawPixel(px, py, SSD1306_WHITE);
                    }
                  }
                }
              }
            }
          }
          
          // Stop animation after total duration
          if (elapsed >= SPLASH_DURATION) {
            splashes[s].active = false;
          }
        }
      }
      
      // Update oldButton for next frame
      oldButton = btn;
    }

    display.display();

    // Cycle between animations with D-pad down/up (debounced)
    if (btn.Dd && btn.Z && !down_pressed) {
      anim_view_index = (anim_view_index + 1) % 3; // 0=rain,1=fire,2=ripple
      down_pressed = true;
    } else if (btn.Du && btn.Z && !down_pressed) {
      anim_view_index = (anim_view_index - 1 + 3) % 3; // cycle backwards
      down_pressed = true;
    } else if (!btn.Dd && !btn.Du) {
      down_pressed = false;
    }
  }

  // Display Clock (elapsed time since power-on)
  if (disp_mode == MODE_CLOCK && !disp_menu) {
    display.clearDisplay();

    // Get elapsed time in milliseconds and convert to seconds
    unsigned long elapsed_ms = to_ms_since_boot(get_absolute_time());
    unsigned long total_secs = elapsed_ms / 1000;
    
    // Calculate HH:MM:SS
    int hours = (total_secs / 3600) % 24;
    int minutes = (total_secs / 60) % 60;
    int seconds = total_secs % 60;

    // Format as HH:MM:SS
    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02d: %02d:", hours, minutes);

    display.setTextColor(SSD1306_WHITE);
    display.setFont(&FreeSerifBoldItalic18pt7b);
    display.setCursor(0, 41);
    display.print(time_str);
    display.setFont(&FreeSerifBoldItalic12pt7b);
    display.setCursor(104, 41);
    display.printf("%02d", seconds);

    display.display();
  }

  // Display PhobGCC Settings / QR Code for Calibration
  if (disp_mode == MODE_CALIBRATION && !disp_menu) {
    // Show help while C-stick down is held, except during active stick
    // calibration, where D-pad down is reserved for help.
    const bool active_stick_calibration =
      cal_view_index == 1 && phob_config.calibration_step >= 0;
    const bool help_input = active_stick_calibration
                              ? btn.Dd
                              : cal_view_index != 0 && btn.Cy_melee < -0.9f;
    cal_help_menu = help_input && !btn.Z && !phob_config.safe_mode;
    
    // Toggle between views with D-pad down/up + Z
    if (btn.Dd && btn.Z && !down_pressed) {
      cal_view_index = (cal_view_index + 1) % 7; // increment view index (7 views: 0-6)
      down_pressed = true;
    } else if (btn.Du && btn.Z && !down_pressed) {
      cal_view_index = (cal_view_index - 1 + 7) % 7; // decrement view index
      down_pressed = true;
    } else if (!btn.Dd && !btn.Du) {
      down_pressed = false;
    }

    display.clearDisplay();

    if (cal_view_index == 0) {
      // Display calibration guide QR code
      display.drawBitmap(62, 0, guide_qr_bmp, 60, 60, SSD1306_WHITE);
      display.setFont(&Picopixel);
      display.setCursor(0, 6);
      display.println("Scan for PhobGCC");
      display.println("calibration");
      display.println("guide");
    } else if (phob_config.safe_mode) {
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.drawBitmap(0, 0, alert_bmp, 9, 8, 1);
      display.setFont();
      display.setCursor(11, 1);
      display.println("To configure or ");
      display.println("calibrate PhobGCC,");
      display.println("disable Safe-Mode by");
      display.println("holding A+X+Y+Start");
      display.println("for 3 seconds.");
    } else if (cal_view_index == 1 && !phob_config.safe_mode) {
      // Display stick calibration
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setFont();
      display.setCursor(0, 0);
      
      if (phob_config.calibration_step < 0) {
        display.println("Stick Calibration:");
        display.setFont();

        display.println();
        display.setFont(&Picopixel);
        display.println("Hold the inputs below for 1 second");
        display.println("to enter stick calibration mode:");
        display.setFont();
        display.println();
        display.println("For A-Stick: A+X+Y+L");
        display.println("For C-Stick: A+X+Y+R");
    } else if (phob_config.calibration_step >= 0 && phob_config.whichStick == 0) {
        display_stick(btn, "a");
        display_stick(btn, "c");
        display.setCursor(0, 46);
        display.printf("Ax:% 7.4f", btn.Ax_melee);
        display.setCursor(0, 55);
        display.printf("Ay:% 7.4f", btn.Ay_melee);
        if (phob_config.calibration_step >= 0) {
          display.setFont(&Picopixel);
          display.setCursor(62, 15);
          display.println("Move A-Stick here:");
          
          // Flash help text at key calibration steps
          bool show_help_text = true;
          if (phob_config.calibration_step == 0 || phob_config.calibration_step == 16 || phob_config.calibration_step == 32) {
            unsigned long now_ms = to_ms_since_boot(get_absolute_time());
            // Toggle every 500ms
            show_help_text = ((now_ms / 500) % 2) == 0;
          }
          
          if (show_help_text) {
            display.setCursor(62, 6);
            display.print("Dpad down for help.");
          }
        } 
      } else if (phob_config.calibration_step >= 0 && phob_config.whichStick == 1) {
        display_stick(btn, "a");
        display_stick(btn, "c");
        display.setCursor(68, 0);
        display.printf("Cx:% 7.4f", btn.Cx_melee);
        display.setCursor(68, 9);
        display.printf("Cy:% 7.4f", btn.Cy_melee);
        if (phob_config.calibration_step >= 0) {
          display.setFont(&Picopixel);
          display.setCursor(0, 51);
          display.println("Move C-Stick here:");
          
          // Flash help text at key calibration steps
          bool show_help_text = true;
          if (phob_config.calibration_step == 0 || phob_config.calibration_step == 16 || phob_config.calibration_step == 32) {
            unsigned long now_ms = to_ms_since_boot(get_absolute_time());
            // Toggle every 500ms
            show_help_text = ((now_ms / 500) % 2) == 0;
          }
          
          if (show_help_text) {
            display.setCursor(0, 60);
            display.print("Dpad down for help.");
          }
        } 
      }
    } else if (cal_view_index == 2) {
      // Display A-Stick settings
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setFont();
      display.setCursor(0, 0);
      display.println("A-Stick Settings:");
      display.println();
      display.printf("Snapback : %2d,%2d \n", phob_config.xSnapback, phob_config.ySnapback);
      display.printf("Waveshape: %2d,%2d \n", phob_config.axWaveshaping, phob_config.ayWaveshaping);
      display.printf("Smoothing: %2d,%2d \n", phob_config.axSmoothing, phob_config.aySmoothing);
      display.println();
      display.printf("Scaling: %d \n", phob_config.astickAnalogScaler);
      display.printf("Car. Snapping: %d", phob_config.astickCardinalSnapping);
      display.setFont(&Picopixel);
      display.setCursor(73, 13);
      display.print("X");
      display.drawLine(72, 14, 76, 14, 1);
      display.setCursor(91, 13);
      display.print("Y");
      display.drawLine(90, 14, 94, 14, 1);
    } else if (cal_view_index == 3) {
      // Display C-Stick settings
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setFont();
      display.setCursor(0, 0);
      display.println("C-Stick Settings:");
      display.println();
      display.printf("Snapback : %2d,%2d \n", phob_config.cxSmoothing, phob_config.cySmoothing);
      display.printf("Waveshape: %2d,%2d \n", phob_config.cxWaveshaping, phob_config.cyWaveshaping);
      display.println();
      display.printf("Scaling: %d \n", phob_config.cstickAnalogScaler);
      display.printf("Car. Snapping: %d", phob_config.cstickCardinalSnapping);
      display.setFont(&Picopixel);
      display.setCursor(73, 13);
      display.print("X");
      display.drawLine(72, 14, 76, 14, 1);
      display.setCursor(91, 13);
      display.print("Y");
      display.drawLine(90, 14, 94, 14, 1);
    } else if (cal_view_index == 4) {
      // Display Trigger settings
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setFont();
      display.setCursor(0, 0);
      display.println("Trigger Settings:");
      display.println();
      display.printf("L Mode  : %d \n", phob_config.lConfig);
      display.printf("L Offset: %d \n", phob_config.lTriggerOffset);
      display.println();
      display.printf("R Mode  : %d \n", phob_config.rConfig);
      display.printf("R Offset: %d", phob_config.rTriggerOffset);
    } else if (cal_view_index == 5) {
      // Button remaps
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setFont();
      display.setCursor(0, 0);
      display.println("Button Remaps:");
      display.println();

      // Helper lambda to get button name from bitmask
      auto get_button_name = [](uint8_t mask) -> const char* {
        if (mask & (1 << 0)) return "A";
        if (mask & (1 << 1)) return "B";
        if (mask & (1 << 2)) return "Du";
        if (mask & (1 << 3)) return "L";
        if (mask & (1 << 4)) return "R";
        if (mask & (1 << 5)) return "X";
        if (mask & (1 << 6)) return "Y";
        if (mask & (1 << 7)) return "Z";
        return "-";
      };
      
      // Display all button remaps across two compact columns.
      //display.setFont(&Picopixel);
      display.setCursor(0, 16);
      display.printf("A<->%s", get_button_name(phob_config.aRemap));
      display.setCursor(54, 16);
      display.printf("B<->%s", get_button_name(phob_config.bRemap));
      display.setCursor(0, 26);
      display.printf("Du<->%s", get_button_name(phob_config.dRemap));
      display.setCursor(54, 26);
      display.printf("L<->%s", get_button_name(phob_config.lRemap));
      display.setCursor(0, 36);
      display.printf("R<->%s", get_button_name(phob_config.rRemap));
      display.setCursor(54, 36);
      display.printf("X<->%s", get_button_name(phob_config.xRemap));
      display.setCursor(0, 46);
      display.printf("Y<->%s", get_button_name(phob_config.yRemap));
      display.setCursor(54, 46);
      display.printf("Z<->%s", get_button_name(phob_config.zRemap));
    } else if (cal_view_index == 6) {
      // Other settings
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setFont();
      display.setCursor(0, 0);
      display.println("Other Settings:");
      display.println();
      display.printf("Auto Initialize: %s \n", phob_config.autoInit ? "On" : "Off");
      display.printf("Tournament Mode: %d \n", phob_config.tournamentToggle);
      display.printf("Rumble Strength: %d", phob_config.rumble);
    }
    
    // Display help menu overlay if active
    if (cal_help_menu) {
      display.fillRect(0, 0, 128, 64, SSD1306_BLACK);
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setFont();
      
      // Display help text based on current view and state
      if (cal_view_index == 1) {
        // Stick calibration help
        display.setCursor(0, 0);

        if (phob_config.calibration_step < 0) {
          display.setFont();
          display.println("Stick Calibration:");
          display.setFont(&Picopixel);
          display.println();
          display.println("This allows you to calibrate");
          display.println("the A-Stick or C-Stick by assigning");
          display.println("and adjusting  your controller");
          display.println("notch positions.");
        } else

        if (phob_config.calibration_step >= 0 && phob_config.calibration_step < 16) {
          display.setFont();
          display.println("Notch Calibration:");
          display.setFont(&Picopixel);
          display.println();
          display.println("Move the stick being calibrated into");
          display.println("the center position or notch shown");
          display.println("on opposite stick, then press");
          display.println("A/L/R to confirm position.");
        } else if (phob_config.calibration_step >= 16 && phob_config.calibration_step < 32) {
          display.setFont();
          display.println("Custom Notches:");
          display.setFont(&Picopixel);
          display.println();
          display.println("Continue following the same");
          display.println("steps to calibrate custom notches.");
          display.println();
          display.println("If you do not have custom notches,");
          display.println("leave the stick centered and");
          display.println("press A/L/R to skip.");
        } else if (phob_config.calibration_step >= 32) {
          display.setFont();
          display.println("Notch Adjustment:");
          display.setFont(&Picopixel);
          display.println();
          display.println("Move stick to shown notch position");
          display.println("and adjust CW and CCW by pressing");
          display.println("X and Y respectively and A/L/R");
          display.println("to confirm.");
          display.setCursor(0, 54);
          display.println("NE: ( 0.70,  0.70), NW: (-0.70,  0.70)");
          display.println("SE: (-0.70, -0.70), SW: ( 0.70, -0.70)");
        }
        
      } else if (cal_view_index == 2) {
        // A-Stick settings help
        display.setCursor(0, 0);
        display.println("A-Stick Commands:");
        display.println();
        display.println("Snpback: A+X/Y+Du/Dd");
        display.println("Wvshape: L+X/Y+Du/Dd");
        display.println("Smooth : R+X/Y+Du/Dd");
        display.println();
        display.println("Scaling : L+A+Du/Dd");
        display.println("Snapping: R+A+Du/Dd");
      } else if (cal_view_index == 3) {
        // C-Stick settings help
        display.setCursor(0, 0);
        display.println("C-Stick Commands:");
        display.println();
        display.println("Snpback: AZ+X/Y+Du/Dd");
        display.println("Wvshape: LZ+X/Y+Du/Dd");
        display.println();
        display.println("Scaling : L+A+Z+Du/Dd");
        display.println("Snapping: R+A+Z+Du/Dd");
      } else if (cal_view_index == 4) {
        // Trigger settings help
        display.setCursor(0, 0);
        display.println("Trigger Commands:");
        display.println();
        display.println("L Mode  : AB+L");
        display.println("L Offset: B+L+Du/Dd");
        display.println();
        display.println("R Mode  : AB+R");
        display.println("R Offset: B+R+Du/Dd");
      } else if (cal_view_index == 5) {
        // Button remaps help
        display.setCursor(0, 0);
        display.println("Btn Remap Commands:");
        display.println();
        display.println("Start Remap: B+X+Y");
        display.setFont(&Picopixel);
        display.println("Then press A,B,Du,L,R,X,Y,Z");
        display.println("in the order you want them remapped.");
        display.println();
        display.println("Example: Remap X<->Z");
        display.println("Press A,B,Du,L,R,Z,Y,X");
      } else if (cal_view_index == 6) {
        // Other settings help
        display.setCursor(0, 0);
        display.println("Other Commands:");
        display.println();
        display.println("Auto Init : A+X+Y+Z");
        display.println("Tourn Mode: Z+Strt+Du");
        display.println("Rumble Str: AB+Du/Dd");
      }
    }
    
    display.display();
  }

  // Display Mode Switching Menu
  if (btn.Dl && btn.Z && !button_pressed) {
    disp_mode--;

    if (disp_mode < 0) {
      disp_mode = DisplayMode(NUM_MODES - 1);
    }

  } else if (btn.Dr && btn.Z && !button_pressed) {
    disp_mode++;

    if (disp_mode >= NUM_MODES) {
      disp_mode = DisplayMode(0);
    }

  }
  button_pressed = btn.Dl || btn.Dr;

  // Show menu when Z + Dpad left/right are pressed, hide when Z is released or Z + up/down
  if (btn.Z && (btn.Dl || btn.Dr)) {
    disp_menu = true;
  } else if (!btn.Z || (btn.Z && (btn.Du || btn.Dd))) {
    if (disp_menu && btn.Z && (btn.Du || btn.Dd)) {
      // Consume the D-pad press used to dismiss the menu.
      down_pressed = true;
    }
    disp_menu = false;
  }

  if (disp_menu) {
    display.clearDisplay();
    display_menu(disp_mode, btn.Dl, btn.Dr, btn.Z);
    if (phob_config.safe_mode) {
      display.drawBitmap(121, 0, lock_bmp, 7, 8, 1);
    } else {
      display.drawBitmap(121, 0, unlock_bmp, 7, 8, 1);
    }
    display.display();
  }

}