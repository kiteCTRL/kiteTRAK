#ifndef EXTRAS_KT_H
#define EXTRAS_KT_H

namespace kt {
	/* Namespace for kiteTRAK functionality.
	 * Functions called below used in main.cpp and readHardware.h for handling SPI communication to peripheral device.
	 * Source code for kiteTRAK can be found here: https://github.com/kiteCTRL/kiteTRAK-for-PhobGCC.
	 */

	ExtrasSlot extrasKtConfigSlot = EXTRAS_UP;

	enum KtSettings{
		KT_SETTING_ENABLE,
		KT_SETTING_UNUSED1,
		KT_SETTING_UNUSED2,
		KT_SETTING_UNUSED3
	};

	enum KtSettingEnable{
		KT_DISABLED,
		KT_ENABLED
	};

	#define BUF_LEN 11
	#define SOF_MARKER 0xFA
	#define EOF_MARKER 0xFB

	uint8_t out_buf[BUF_LEN];

	void SPI_transfer(uint8_t buttons_array[10], int calibration_step, IntOrFloat config[]) {
		/*
		if (config[KT_SETTING_ENABLE].intValue != KT_ENABLED) {
       		return;
    	}
		*/
		out_buf[0] = SOF_MARKER; // Start of Frame marker for buffer
		out_buf[BUF_LEN - 1] = EOF_MARKER; // End of Frame marker for buffer
		out_buf[9] = (uint8_t)calibration_step; // Stick calibration step

		// Assign button states to out_buf (bytes 1-8 of out_buf)
		for(size_t i = 0; i < 8; ++i) {
			out_buf[i+1] = buttons_array[i];
		}

		if(spi_is_writable(spi1)) {
			spi_write_blocking(spi1, out_buf, BUF_LEN);
		}

	}

	void SPI_setup() {
		spi_init(spi1, 1000*1000); //1MHz Baudrate
		gpio_set_function(_pinSpare0, GPIO_FUNC_SPI); //GPIO12 - Rx
		gpio_set_function(_pinSpare1, GPIO_FUNC_SPI); //GPIO13 - Cs
		gpio_set_function(_pinSpare2, GPIO_FUNC_SPI); //GPIO14 - SCK
		gpio_set_function(_pinLED, GPIO_FUNC_SPI);	  //GPIO15 - Tx
	}
	
	bool toggle(IntOrFloat config[]) {
		int& enabled = config[KT_SETTING_ENABLE].intValue;
		if (enabled != KT_DISABLED){
			enabled = KT_DISABLED;
		} else {
			enabled = KT_ENABLED;
		}
		setExtrasSettingInt(extrasKtConfigSlot, KT_SETTING_ENABLE, enabled);
		return (enabled == KT_ENABLED);
	}
}

#endif //EXTRAS_KT_H