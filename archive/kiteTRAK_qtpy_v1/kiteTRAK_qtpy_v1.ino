#include <stdio.h>
#include "hardware/spi.h"
#include "pico/binary_info.h"
#include "pico/stdlib.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3D ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire1, OLED_RESET);

#define LOGO_HEIGHT 16
#define LOGO_WIDTH 16

#define BUF_LEN 12
#define SOF_MARKER 0xFA
#define EOF_MARKER 0xFB

#define ORG 127

uint8_t in_buf[BUF_LEN];
uint8_t out_buf[BUF_LEN];

uint8_t btn_arr1[8];
uint8_t btn_arr2[8];

//btn definition
uint8_t btn_A;
uint8_t btn_B;
uint8_t btn_X;
uint8_t btn_Y;
uint8_t btn_S;

uint8_t btn_Dl;
uint8_t btn_Dr;
uint8_t btn_Dd;
uint8_t btn_Du;

uint8_t btn_Z;
uint8_t btn_R;
uint8_t btn_L;

uint8_t btn_Ax;
uint8_t btn_Ay;
uint8_t btn_Cx;
uint8_t btn_Cy;

uint8_t btn_La;
uint8_t btn_Ra;

bool comm_est = false;

int disp_mode = 0;

//===============================================================//

uint8_t get_nth_bit(uint8_t num, int n) {
  return (num >> n) & 1;
}

uint8_t* align_to_sof_eof(const uint8_t* arr, size_t len, uint8_t sof_marker = 0xFA, uint8_t eof_marker = 0xFB) {
    static uint8_t aligned[BUF_LEN];
    size_t sof_idx = len; // Invalid by default

    // Find the SOF marker
    for (size_t i = 0; i < len; ++i) {
        if (arr[i] == sof_marker) {
            sof_idx = i;
            break;
        }
    }
    if (sof_idx == len) return nullptr; // SOF not found

    // Rotate array so SOF is at index 0
    for (size_t i = 0; i < len; ++i) {
        aligned[i] = arr[(i + sof_idx) % len];
    }
    // Check if EOF marker is now at the last index
    if (aligned[len - 1] != eof_marker) return nullptr;

    return aligned;
}

void meleeCoordClamp(const int xIn, const int yIn, float &xOut, float &yOut) {
	const float magnitude = sqrt((float) xIn*xIn + yIn*yIn);
	const float scale = fmin(1.0f, 80.0f/magnitude);
	xOut = truncf(xIn*scale)/80.0f;
	yOut = truncf(yIn*scale)/80.0f;
}

//===============================================================//

void setup() { //SPI setup (core0)

  spi_deinit(spi0); // Deinitialize SPI

  busy_wait_ms(500);

  spi_init(spi0, 3000*1000); //3MHz Baudrate
  spi_set_slave(spi0, true);

  gpio_set_function(4, GPIO_FUNC_SPI); //MISO (Rx)
  gpio_set_function(6, GPIO_FUNC_SPI); //SCK
  gpio_set_function(3, GPIO_FUNC_SPI); //MOSI (Tx)
  gpio_set_function(5, GPIO_FUNC_SPI); //CS

  for(size_t i = 0; i < BUF_LEN; ++i) {
    in_buf[i] = 0;
    out_buf[i] = i;
  }
}

void loop() { //SPI read loop (core0)

  if(spi_is_readable(spi0)) {
    spi_write_read_blocking(spi0, out_buf, in_buf, BUF_LEN);
  }
  
}

//===============================================================//

void setup1() { //Display setup (core1)
  
  // display init
  busy_wait_ms(100);

  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    //for(;;); // Don't proceed, loop forever
  }

  display.display();
  busy_wait_ms(1000);
  display.clearDisplay();
}

void loop1() { //Display loop (core1)

  // Align to SOF/EOF markers and only process if valid
  uint8_t* aligned_buf = align_to_sof_eof(in_buf, BUF_LEN, SOF_MARKER, EOF_MARKER);
  if (!aligned_buf) {
    // Invalid frame, skip processing this loop
    return;
  }

  for(size_t i = 0; i < 7; ++i) {
    btn_arr1[i] = get_nth_bit(aligned_buf[1], i);
    btn_arr2[i] = get_nth_bit(aligned_buf[2], i);
  }

  //binary btn array 1
  btn_A = btn_arr1[0];
  btn_B = btn_arr1[1];
  btn_X = btn_arr1[2];
  btn_Y = btn_arr1[3];
  btn_S = btn_arr1[4];

  //binary btn array 2
  btn_Dl = btn_arr2[0];
  btn_Dr = btn_arr2[1];
  btn_Dd = btn_arr2[2];
  btn_Du = btn_arr2[3];
  btn_Z = btn_arr2[4];
  btn_R = btn_arr2[5];
  btn_L = btn_arr2[6];

  btn_Ax = aligned_buf[3];
  btn_Ay = aligned_buf[4];
  btn_Cx = aligned_buf[5];
  btn_Cy = aligned_buf[6];

  btn_La = aligned_buf[7];
  btn_Ra = aligned_buf[8];

  //melee stick values
  int AxCoord = btn_Ax - ORG;
  int AyCoord = btn_Ay - ORG;
  float AxMelee;
  float AyMelee;
  meleeCoordClamp(AxCoord, AyCoord, AxMelee, AyMelee);

  int CxCoord = btn_Cx - ORG;
  int CyCoord = btn_Cy - ORG;
  float CxMelee;
  float CyMelee;
  meleeCoordClamp(CxCoord, CyCoord, CxMelee, CyMelee);

  //mode switching (dpad)
  if(btn_Dl) {
    disp_mode = 0;
  } else if(btn_Dr) {
    disp_mode = 1;
  } else if(btn_Dd) {
    disp_mode = 2;
  }

  if(disp_mode == 0) {

    //reset display printing
    display.setTextSize(1);             // Normal 1:1 pixel scale
    display.setTextColor(SSD1306_WHITE);       // Draw white text     
    display.setCursor(0, 0);

    //row 1
    display.print("A");
    display.print(btn_A);

    display.print("|B");
    display.print(btn_B);

    display.print("|X");
    display.print(btn_X);

    display.print("|Y");
    display.print(btn_Y);

    display.print("|S");
    display.println(btn_S);

    //row 2
    display.print("l");
    display.print(btn_Dl);

    display.print("|r");
    display.print(btn_Dr);

    display.print("|d");
    display.print(btn_Dd);

    display.print("|u");
    display.print(btn_Du);

    display.print("|Z");
    display.print(btn_Z); 

    display.print("|R");
    display.print(btn_R); 

    display.print("|L");
    display.println(btn_L); 

    //row 3
    display.print("Ax");
    display.printf("%.4f", AxMelee);

    display.print("|Ay");
    display.printf("%.4f\n", AyMelee);

    //row 4
    display.print("Cx");
    display.printf("%.4f", CxMelee);

    display.print("|Cy");
    display.printf("%.4f\n", CyMelee);

    //row 5
    display.print("La");
    display.print(btn_La);

    display.print("|Ra");
    display.println(btn_Ra);

    //row 7
    display.println();
    display.print("comm time(us): ");
    display.println(aligned_buf[10]);

    //row 8
    display.print("calibration step: ");
    display.print(aligned_buf[9]);

    display.display();
    display.clearDisplay();

  }

  if(disp_mode == 1) {

    display.setTextSize(1);             
    display.setTextColor(SSD1306_WHITE);  

    //a stickbox draw
    display.drawLine(27, 1, 13, 7, SSD1306_WHITE);
    display.drawLine(13, 7, 7, 21, SSD1306_WHITE);
    display.drawLine(7, 21, 13, 35, SSD1306_WHITE);
    display.drawLine(13, 35, 27, 41, SSD1306_WHITE);
    display.drawLine(27, 41, 41, 35, SSD1306_WHITE);
    display.drawLine(41, 35, 47, 21, SSD1306_WHITE);
    display.drawLine(47, 21, 41, 7, SSD1306_WHITE);
    display.drawLine(41, 7, 27, 1, SSD1306_WHITE);

    //c stickbox draw
    display.drawLine(100, 22, 86, 28, SSD1306_WHITE);
    display.drawLine(86, 28, 80, 42, SSD1306_WHITE);
    display.drawLine(80, 42, 86, 56, SSD1306_WHITE);
    display.drawLine(86, 56, 100, 62, SSD1306_WHITE);
    display.drawLine(100, 62, 114, 56, SSD1306_WHITE);
    display.drawLine(114, 56, 120, 42, SSD1306_WHITE);
    display.drawLine(120, 42, 114, 28, SSD1306_WHITE);
    display.drawLine(114, 28, 100, 22, SSD1306_WHITE);
    
    //a stick value text and circle
    display.setCursor(0, 47);
    display.printf("Ax:%.4f", AxMelee);
    display.setCursor(0, 56);
    display.printf("Ay:%.4f", AyMelee);

    const uint16_t Ax_orig = 27;
    const uint16_t Ay_orig = 21;
    display.fillCircle(Ax_orig + AxMelee*20, Ay_orig - AyMelee*20, 3, SSD1306_WHITE);
    
    //c stick value text and circle
    display.setCursor(68, 0);
    display.printf("Cx:%.4f", CxMelee);
    display.setCursor(68, 9);
    display.printf("Cy:%.4f", CyMelee);

    const uint16_t Cx_orig = 100;
    const uint16_t Cy_orig = 42;
    display.fillCircle(Cx_orig + CxMelee*20, Cy_orig - CyMelee*20, 3, SSD1306_WHITE);    
    
    display.display();
    display.clearDisplay();
  }
}
