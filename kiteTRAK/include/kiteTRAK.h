#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ktShared.h>

#include "FreeSansBoldOblique12pt7b.h"
#include "FreeSansBold9pt7b.h"
#include "FreeSerifBoldItalic18pt7b.h"
#include "FreeSerifBoldItalic12pt7b.h"
#include "Picopixel.h"
#include "bitmaps.h"
#include "flame.h"
#include "rain.h"

#define ORG 127             // Origin point for stick calibration

#define LOGO_HEIGHT 16      // Bootup logo height
#define LOGO_WIDTH 16       // Bootup logo width

#define SCREEN_WIDTH 128    // OLED display width, in pixels
#define SCREEN_HEIGHT 64    // OLED display height, in pixels

#define OLED_RESET -1       // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3D // Address; 0x3D for 128x64, 0x3C for 128x32

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

// PhobGCC Configuration data received from master
struct PhobConfig {
    int calibration_step;      // Current calibration step (-1 = not calibrating, 0-47+ = step number)
    uint8_t aRemap;            // A button remap (bitmask)
    uint8_t bRemap;            // B button remap (bitmask)
    uint8_t dRemap;            // D-pad remap (bitmask)
    uint8_t lRemap;            // L button remap (bitmask)
    uint8_t rRemap;            // R button remap (bitmask)
    uint8_t xRemap;            // X button remap (bitmask)
    uint8_t yRemap;            // Y button remap (bitmask)
    uint8_t zRemap;            // Z button remap (bitmask)
    uint8_t lConfig;           // L trigger mode (0-6)
    uint8_t rConfig;           // R trigger mode (0-6)
    uint8_t lTriggerOffset;    // L trigger offset (49-227)
    uint8_t rTriggerOffset;    // R trigger offset (49-227)
    uint8_t rumble;            // Rumble strength (0-11)
    uint8_t autoInit;          // Auto-initialize setting (0-1)
    int8_t xSnapback;          // A-stick X snapback (-10 to 10)
    int8_t ySnapback;          // A-stick Y snapback (-10 to 10)
    int8_t cxSmoothing;        // C-stick X snapback (?)
    int8_t cySmoothing;        // C-stick Y snapback (?)
    uint8_t axSmoothing;       // A-stick X smoothing (0-9)
    uint8_t aySmoothing;       // A-stick Y smoothing (0-9)
    uint8_t axWaveshaping;     // A-stick X waveshaping (0-15)
    uint8_t ayWaveshaping;     // A-stick Y waveshaping (0-15)
    uint8_t cxWaveshaping;     // C-stick X waveshaping (0-15)
    uint8_t cyWaveshaping;     // C-stick Y waveshaping (0-15)
    int8_t astickCardinalSnapping;  // A-stick cardinal snapping (-1 to 6)
    int8_t cstickCardinalSnapping;  // C-stick cardinal snapping (-1 to 6)
    uint8_t astickAnalogScaler;     // A-stick analog scaler (82-125)
    uint8_t cstickAnalogScaler;     // C-stick analog scaler (82-125)
    uint8_t tournamentToggle;       // Tournament toggle mode (0-5)
    uint8_t whichStick;        // Which stick being calibrated (0=ASTICK, 1=CSTICK)
    bool safe_mode;            // Safe mode enabled/disabled
};

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire1, OLED_RESET);

enum DisplayMode {
    MODE_INPUT,
    MODE_IPM,
    MODE_CLOCK,
    MODE_ANIMATIONS,
    MODE_CALIBRATION,

    NUM_MODES
};

// SPI Communication Functions
uint8_t get_nth_bit(uint8_t num, int n) {
    return (num >> n) & 1;
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
void assign_buttons(const KtData& data, Buttons &btn) {
    btn.A = get_nth_bit(data.buttons[0], 0);
    btn.B = get_nth_bit(data.buttons[0], 1);
    btn.X = get_nth_bit(data.buttons[0], 2);
    btn.Y = get_nth_bit(data.buttons[0], 3);
    btn.S = get_nth_bit(data.buttons[0], 4);

    btn.Dl = get_nth_bit(data.buttons[1], 0);
    btn.Dr = get_nth_bit(data.buttons[1], 1);
    btn.Dd = get_nth_bit(data.buttons[1], 2);
    btn.Du = get_nth_bit(data.buttons[1], 3);
    btn.Z = get_nth_bit(data.buttons[1], 4);
    btn.R = get_nth_bit(data.buttons[1], 5);
    btn.L = get_nth_bit(data.buttons[1], 6);

    btn.Ax = data.buttons[2];
    btn.Ay = data.buttons[3];
    btn.Cx = data.buttons[4];
    btn.Cy = data.buttons[5];

    btn.La = data.buttons[6];
    btn.Ra = data.buttons[7];

    melee_coord_clamp(btn.Ax - ORG, btn.Ay - ORG, btn.Ax_melee, btn.Ay_melee);
    melee_coord_clamp(btn.Cx - ORG, btn.Cy - ORG, btn.Cx_melee, btn.Cy_melee);
}

// Parse PhobGCC Configuration from buffer
void parse_phob_config(const KtData& data, PhobConfig &config) {
    config.calibration_step = (int)data.calibrationStepEncoded - 128;
    config.aRemap = data.aRemap;
    config.bRemap = data.bRemap;
    config.dRemap = data.dRemap;
    config.lRemap = data.lRemap;
    config.rRemap = data.rRemap;
    config.xRemap = data.xRemap;
    config.yRemap = data.yRemap;
    config.zRemap = data.zRemap;
    config.lConfig = data.lConfig;
    config.rConfig = data.rConfig;
    config.lTriggerOffset = data.lTriggerOffset;
    config.rTriggerOffset = data.rTriggerOffset;
    config.rumble = data.rumble;
    config.autoInit = data.autoInit;
    config.xSnapback = (int8_t)(data.xSnapbackEncoded - 128);
    config.ySnapback = (int8_t)(data.ySnapbackEncoded - 128);
    config.axSmoothing = data.axSmoothing;
    config.aySmoothing = data.aySmoothing;
    config.cxSmoothing = data.cxSmoothing;
    config.cySmoothing = data.cySmoothing;
    config.axWaveshaping = data.axWaveshaping;
    config.ayWaveshaping = data.ayWaveshaping;
    config.cxWaveshaping = data.cxWaveshaping;
    config.cyWaveshaping = data.cyWaveshaping;
    config.astickCardinalSnapping = (int8_t)(data.astickCardinalSnappingEncoded - 128);
    config.cstickCardinalSnapping = (int8_t)(data.cstickCardinalSnappingEncoded - 128);
    config.astickAnalogScaler = data.astickAnalogScaler;
    config.cstickAnalogScaler = data.cstickAnalogScaler;
    config.tournamentToggle = data.tournamentToggle;
    config.whichStick = data.whichStick;
    config.safe_mode = data.safeMode;
}

// Display Helper Functions
//=============================================================//
void draw_dpad(int x=0, int y=0) {
    // dpad-y
    display.drawRect(28+x, 17+y, 7, 25, 1);
    // dpad-x
    display.drawRect(19+x, 26+y, 25, 7, 1);
    // dpad-mask-u
    display.drawLine(29+x, 26+y, 33+x, 26+y, 0);
    // dpad-mask-r
    display.drawLine(34+x, 27+y, 34+x, 31+y, 0);
    // dpad-mask-d
    display.drawLine(33+x, 32+y, 29+x, 32+y, 0);
    // dpad-mask-l
    display.drawLine(28+x, 31+y, 28+x, 27+y, 0);
}

void draw_sm_disabled() {
    display.setFont();
    display.fillRoundRect(31, 22, 67, 20, 1, 0);
    display.setTextColor(1);
    display.setTextWrap(false);
    display.setCursor(46, 31);
    display.print("Disabled");
    display.drawBitmap(32, 23, sm_border_bmp, 65, 18, 1);
    display.drawBitmap(36, 27, unlock_bmp, 7, 8, 1);
    display.drawBitmap(46, 25, sm_txt_bmp, 36, 5, 1);
}

void draw_sm_enabled() {
    display.setFont();
    display.fillRoundRect(31, 22, 67, 20, 1, 0);
    display.setTextColor(1);
    display.setTextWrap(false);
    display.setCursor(46, 31);
    display.print("Enabled");
    display.drawBitmap(32, 23, sm_border_bmp, 65, 18, 1);
    display.drawBitmap(36, 27, lock_bmp, 7, 8, 1);
    display.drawBitmap(46, 25, sm_txt_bmp, 36, 5, 1);
}

void draw_boot_screen() {
    display.drawBitmap(32, 23, boot_screen_bmp, 65, 18, 1);
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
    for (int i = 0; i < 46; i++) {
        int y_shift = ((i/6) % 2 == 0) ? 3 : 0;

        if (i < 6) {
          display.setCursor(12, 39 + y_shift);  
        } else {
          display.setCursor(12, 42);  
        }
        display.print("kiteTRAK");
        if (i < 18) {
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
        int y_shift = ((i/6) % 2 == 0) ? 3 : 0;

        display.setCursor(12, 42);
        display.print("kiteTRAK");
        display.drawBitmap(112+(i*2), 0 + y_shift, drifloon_right_bmp, 15, 22, 1);
        display.drawBitmap(10-(i*2), 1 + y_shift, drifloon_left_bmp, 15, 22, 1);

        display.display();
        busy_wait_ms(15);
        display.clearDisplay();
    }
}

// Switch Mode Menu
void display_menu(int disp_mode, bool Dl, bool Dr, bool Z) {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setFont(&FreeSansBold9pt7b);
    display.setCursor(0, 0);
    display.drawBitmap(16, 1, menu_border_bmp, 96, 61, 1);

    // Draw arrows
    if (Dl && Z) {
        display.fillTriangle(10, 22, 10, 40, 1, 31, SSD1306_WHITE);
    } else {
        display.drawTriangle(10, 22, 10, 40, 1, 31, SSD1306_WHITE);
    }

    if (Dr && Z) {
        display.fillTriangle(118, 22, 118, 40, 127, 31, SSD1306_WHITE);
    } else {
        display.drawTriangle(118, 22, 118, 40, 127, 31, SSD1306_WHITE);
    }
    
    // Determine menu text
    const char* menu_text;
    switch (disp_mode) {
        case MODE_INPUT:
            menu_text = "Buttons";
            // Analog stick
            display.drawCircle(40, 21, 14, 1);
            display.drawCircle(40, 21, 10, 1);
            display.drawCircle(40, 21, 6, 1);

            display.drawCircle(84, 23, 10, 1); // a button
            display.drawCircle(72, 37, 5, 1); // b button
            display.drawBitmap(89, 5, xbutton_bmp, 15, 13, 1); // x button
            display.drawBitmap(65, 5, ybutton_bmp, 15, 13, 1); // y button
            break;
        case MODE_IPM:
            menu_text = "IPM";
            display.drawBitmap(38, -4, ipm_bmp, 50, 50, 1);
            break;
        case MODE_ANIMATIONS:
            menu_text = "Animat.";
            display.drawBitmap(57, 14, film_bmp, 15, 16, 1);
            break;
        case MODE_CLOCK:
            menu_text = "Timer";
            display.drawBitmap(57, 14, clock_bmp, 15, 16, 1);
            break;
        case MODE_CALIBRATION:
            menu_text = "Cal. Guide";
            display.drawBitmap(56, 14, guide_icon_bmp, 17, 16, 1);
            break;
    }

    // Measure text width and height
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(menu_text, 0, 0, &x1, &y1, &w, &h);

    // Center text on the screen
    int text_x = (SCREEN_WIDTH - w) / 2;
    int text_y = (SCREEN_HEIGHT + h) / 2;

    display.setCursor(text_x, text_y + 18); // Adjust to bottom of border
    display.print(menu_text);
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

            const uint16_t Cx_orig = 100;
            const uint16_t Cy_orig = 42;
            display.fillCircle(Cx_orig + btn.Cx_melee*20, Cy_orig - btn.Cy_melee*20, 3, SSD1306_WHITE);
        }
    }
}

// Button State & Debug
void display_buttons_dash(const Buttons &btn) {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setFont();
    display.setCursor(0, 0);

    display.println("Digital Inputs:");
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
    display.println();

    display.println("Analog Inputs:");
    display.printf("Ax:% 7.4f", btn.Ax_melee);
    display.printf(" Ay:% 7.4f\n", btn.Ay_melee);
    display.printf("Cx:% 7.4f", btn.Cx_melee);
    display.printf(" Cy:% 7.4f\n", btn.Cy_melee);
    display.printf("La:%03d", btn.La);
    display.printf(" Ra:%03d", btn.Ra);
}