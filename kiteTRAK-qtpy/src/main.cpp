#include <stdio.h>
#include "hardware/spi.h"
#include "pico/binary_info.h"
#include "pico/stdlib.h"

#include "kiteTRAK.h"

Buttons btn;
uint8_t btn_arr1[8];
uint8_t btn_arr2[8];
bool button_pressed = false;

uint8_t in_buf[BUF_LEN];

int disp_mode = MODE_BUTTONS_DEBUG; // Default display mode
bool disp_menu = false;
int menu_duration = 2000; // Show menu for 2 seconds
int menu_show_until = 0;

// Debug variables for SPI transfer timing
int buffer_time = 0;
int start_time = 0;
int end_time = 0;
int avg_time = 0;
int max_time = 0;
unsigned long total_buffer_time = 0;
unsigned long num_measurements = 0;

// Core 0 SPI Setup and Loop
//===============================================================//

void setup() {
  spi_init(spi0, 1000*1000); //1MHz Baudrate
  spi_set_slave(spi0, true);

  gpio_set_function(4, GPIO_FUNC_SPI); // MISO (Rx)
  gpio_set_function(6, GPIO_FUNC_SPI); // SCK
  gpio_set_function(3, GPIO_FUNC_SPI); // MOSI (Tx)
  gpio_set_function(5, GPIO_FUNC_SPI); // CS

  for(size_t i = 0; i < BUF_LEN; ++i) {
    in_buf[i] = 0;
  }
}

void loop() {
  if (spi_is_readable(spi0)) {
    start_time = time_us_32();
    spi_read_blocking(spi0, 0, in_buf, BUF_LEN);
    end_time = time_us_32();
    buffer_time = end_time - start_time;
    total_buffer_time += buffer_time;
    num_measurements++;
  }

  avg_time = total_buffer_time / num_measurements;

  if (buffer_time > max_time) {
    max_time = buffer_time;
  }

  // Reset timing stats
  if (btn.B) {
    max_time = 0;
    total_buffer_time = 0;
    num_measurements = 0;
    avg_time = 0;
  }
}

// Core 1 Display Setup and Loop
//===============================================================//

void setup1() {
  busy_wait_ms(150);
  display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
  display.clearDisplay();

  boot_animation();
}

void loop1() {
  // Align to SOF/EOF markers and only process if valid
  uint8_t* aligned_buf = align_to_sof_eof(in_buf, BUF_LEN, SOF_MARKER, EOF_MARKER);
  
  // Invalid frame, skip processing this loop
  if (!aligned_buf) {
    return;
  }

  // Assign button states to btn
  assign_buttons(btn, aligned_buf);


  // Display Modes
  //=============================================================//

  // Display Stickbox Visuals
  if (disp_mode == MODE_STICKBOX && !disp_menu) {
    display.clearDisplay();
    display_stick(btn, "a");
    display_stick(btn, "c");
    
    display.display();
  }

  // Display Button State &
  if (disp_mode == MODE_BUTTONS_DEBUG && !disp_menu) {
    display.clearDisplay();

    display_buttons_debug(btn, aligned_buf);

    display.println();
    display.print("max comm (us): ");
    display.println(max_time);
    display.print("avg comm (us): ");
    display.print(avg_time);

    display.display();
  }

  // Display Mode Switching Menu
  if (btn.Dl && !button_pressed) {
    disp_mode--;
    disp_menu = true;
    menu_show_until = to_ms_since_boot(get_absolute_time()) + menu_duration;

    if (disp_mode < 0) {
      disp_mode = DisplayMode(NUM_MODES - 1);
    }

  } else if (btn.Dr && !button_pressed) {
    disp_mode++;
    disp_menu = true;
    menu_show_until = to_ms_since_boot(get_absolute_time()) + menu_duration;

    if (disp_mode >= NUM_MODES) {
      disp_mode = DisplayMode(0);
    }

  }
  button_pressed = btn.Dl || btn.Dr;

  if (disp_menu && to_ms_since_boot(get_absolute_time()) < menu_show_until) {
    display.clearDisplay();
    display_menu(disp_mode, btn.Dl, btn.Dr);
    display.display();
  } else {
    disp_menu = false;
  }

}