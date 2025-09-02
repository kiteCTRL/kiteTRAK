#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "FreeSansBoldOblique12pt7b.h"
#include "Petme8x8.h"
#include "bitmaps.h"

#define BUF_LEN    11       // Length of the buffer
#define SOF_MARKER 0xFA     // Start of Frame marker
#define EOF_MARKER 0xFB     // End of Frame marker

#define ORG 127             // Origin point for stick calibration

#define LOGO_HEIGHT 16      // Bootup logo height
#define LOGO_WIDTH 16       // Bootup logo width

#define SCREEN_WIDTH 128    // OLED display width, in pixels
#define SCREEN_HEIGHT 64    // OLED display height, in pixels

#define OLED_RESET -1       // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3D // Address; 0x3D for 128x64, 0x3C for 128x32

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire1, OLED_RESET);

struct Buttons {
    uint8_t A;
    uint8_t B;
    uint8_t X;
    uint8_t Y;
    uint8_t S;

    uint8_t Dl;
    uint8_t Dr;
    uint8_t Dd;
    uint8_t Du;
    uint8_t Z;
    uint8_t R;
    uint8_t L;

    uint8_t Ax;
    uint8_t Ay;
    uint8_t Cx;
    uint8_t Cy;

    uint8_t La;
    uint8_t Ra;

    float Ax_melee; // Melee-calibrated Ax
    float Ay_melee; // Melee-calibrated Ay
    float Cx_melee; // Melee-calibrated Cx
    float Cy_melee; // Melee-calibrated Cy
};

// SPI Communication Functions
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

// Input Functions
//=============================================================//

// Stick Snapping to Melee Coord
void melee_coord_clamp(const int xIn, const int yIn, float &xOut, float &yOut) {
	const float magnitude = sqrt((float) xIn*xIn + yIn*yIn);
	const float scale = fmin(1.0f, 80.0f/magnitude);
	xOut = truncf(xIn*scale)/80.0f;
	yOut = truncf(yIn*scale)/80.0f;
}

// Assign Buttons to Struct
void assign_buttons(Buttons &btn, const uint8_t* aligned_buf) {
    btn.A = get_nth_bit(aligned_buf[1], 0);
    btn.B = get_nth_bit(aligned_buf[1], 1);
    btn.X = get_nth_bit(aligned_buf[1], 2);
    btn.Y = get_nth_bit(aligned_buf[1], 3);
    btn.S = get_nth_bit(aligned_buf[1], 4);

    btn.Dl = get_nth_bit(aligned_buf[2], 0);
    btn.Dr = get_nth_bit(aligned_buf[2], 1);
    btn.Dd = get_nth_bit(aligned_buf[2], 2);
    btn.Du = get_nth_bit(aligned_buf[2], 3);
    btn.Z = get_nth_bit(aligned_buf[2], 4);
    btn.R = get_nth_bit(aligned_buf[2], 5);
    btn.L = get_nth_bit(aligned_buf[2], 6);

    btn.Ax = aligned_buf[3];
    btn.Ay = aligned_buf[4];
    btn.Cx = aligned_buf[5];
    btn.Cy = aligned_buf[6];

    btn.La = aligned_buf[7];
    btn.Ra = aligned_buf[8];

    melee_coord_clamp(btn.Ax - ORG, btn.Ay - ORG, btn.Ax_melee, btn.Ay_melee);
    melee_coord_clamp(btn.Cx - ORG, btn.Cy - ORG, btn.Cx_melee, btn.Cy_melee);
}


// Display Modes
//=============================================================//

// Boot Animation
void boot_animation() {
    display.setTextColor(1);
    display.setTextWrap(false);
    display.setFont(&FreeSansBoldOblique12pt7b);

    // 1- All moving and bouncing to right
    for (int i = 0; i < 63; i++) {
        int y_shift = ((i/6) % 2 == 0) ? 3 : 0;

        display.setCursor(136-(i*2), 39 + y_shift);
        display.print("kiteTRAK");
        display.drawBitmap(236-(i*2), 0 + y_shift, drifloon_left_bmp, 15, 22, 1);
        display.drawBitmap(134-(i*2), 1 + y_shift, drifloon_left_bmp, 15, 22, 1);

        display.display();
        busy_wait_ms(15);
        display.clearDisplay();
    }

    // 2 - All bouncing in place -> Drop -> Turnaround
    for (int i = 0; i < 42; i++) {
        int y_shift = ((i/6) % 2 == 0) ? 3 : 0;

        if (i < 18) {
          display.setCursor(12, 39 + y_shift);  
        } else {
          display.setCursor(12, 42);  
        }
        display.print("kiteTRAK");
        if (i < 24) {
            display.drawBitmap(10, 1 + y_shift, drifloon_left_bmp, 15, 22, 1);
        } else {
            display.drawBitmap(10, 1 + y_shift, drifloon_right_bmp, 15, 22, 1);
        }
        display.drawBitmap(112, 0 + y_shift, drifloon_left_bmp, 15, 22, 1);

        display.display();
        busy_wait_ms(15);
        display.clearDisplay();
    }

    // 3 - Drifloon moving outwards to sides
    for (int i = 0; i < (53); i++) {
        int y_shift = 0;
        if ((i/6) % 2 == 0) {
            y_shift += 3;
        }

        display.setCursor(12, 42);
        display.print("kiteTRAK");
        display.drawBitmap(112+(i*2), 0 + y_shift, drifloon_right_bmp, 15, 22, 1);
        display.drawBitmap(10-(i*2), 1 + y_shift, drifloon_left_bmp, 15, 22, 1);

        display.display();
        busy_wait_ms(15);
        display.clearDisplay();
    }
}

// Stick Visuals
void display_stick(const Buttons &btn, const String &stick = "a", bool mini_sticks = false) {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setFont();
    display.setCursor(0, 0);

    if (mini_sticks == false) {
        if (stick == "a") {
            //a stickbox draw
            display.drawLine(27, 1, 13, 7, SSD1306_WHITE);
            display.drawLine(13, 7, 7, 21, SSD1306_WHITE);
            display.drawLine(7, 21, 13, 35, SSD1306_WHITE);
            display.drawLine(13, 35, 27, 41, SSD1306_WHITE);
            display.drawLine(27, 41, 41, 35, SSD1306_WHITE);
            display.drawLine(41, 35, 47, 21, SSD1306_WHITE);
            display.drawLine(47, 21, 41, 7, SSD1306_WHITE);
            display.drawLine(41, 7, 27, 1, SSD1306_WHITE);

            //a stick value text and circle
            display.setCursor(0, 47);
            display.printf("Ax:%.4f", btn.Ax_melee);
            display.setCursor(0, 56);
            display.printf("Ay:%.4f", btn.Ay_melee);
            const uint16_t Ax_orig = 27;
            const uint16_t Ay_orig = 21;
            display.fillCircle(Ax_orig + btn.Ax_melee*20, Ay_orig - btn.Ay_melee*20, 3, SSD1306_WHITE);
        }
    
        if (stick == "c") {
            //c stickbox draw
            display.drawLine(100, 22, 86, 28, SSD1306_WHITE);
            display.drawLine(86, 28, 80, 42, SSD1306_WHITE);
            display.drawLine(80, 42, 86, 56, SSD1306_WHITE);
            display.drawLine(86, 56, 100, 62, SSD1306_WHITE);
            display.drawLine(100, 62, 114, 56, SSD1306_WHITE);
            display.drawLine(114, 56, 120, 42, SSD1306_WHITE);
            display.drawLine(120, 42, 114, 28, SSD1306_WHITE);
            display.drawLine(114, 28, 100, 22, SSD1306_WHITE);

            //c stick value text and circle
            display.setCursor(68, 0);
            display.printf("Cx:%.4f", btn.Cx_melee);
            display.setCursor(68, 9);
            display.printf("Cy:%.4f", btn.Cy_melee);
            const uint16_t Cx_orig = 100;
            const uint16_t Cy_orig = 42;
            display.fillCircle(Cx_orig + btn.Cx_melee*20, Cy_orig - btn.Cy_melee*20, 3, SSD1306_WHITE);
        }
    }
}

// Button State & Debug
void display_buttons_debug(const Buttons &btn, uint8_t* aligned_buf = nullptr) {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setFont();
    display.setCursor(0, 0);

    display.print("A"); display.print(btn.A);
    display.print(" B"); display.print(btn.B);
    display.print(" X"); display.print(btn.X);
    display.print(" Y"); display.print(btn.Y);
    display.print(" Z"); display.print(btn.Z);
    display.print(" S"); display.println(btn.S);

    display.print("l"); display.print(btn.Dl);
    display.print(" r"); display.print(btn.Dr);
    display.print(" u"); display.print(btn.Du);
    display.print(" d"); display.print(btn.Dd);
    display.print(" L"); display.print(btn.L);
    display.print(" R"); display.println(btn.R);

    display.printf("Ax:%.4f", btn.Ax_melee);
    display.printf(" Ay:%.4f\n", btn.Ay_melee);
    display.printf("Cx:%.4f", btn.Cx_melee);
    display.printf(" Cy:%.4f\n", btn.Cy_melee);
    display.printf("La:%03d", btn.La);
    display.printf(" Ra:%03d\n", btn.Ra);
}