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

	#define BUF_LEN 42
	#define FRAME_LEN (BUF_LEN + 2)
	#define SOF_MARKER 0xFA
	#define EOF_MARKER 0xFB

	#include "hardware/dma.h"
	#include "hardware/irq.h"
	#include "hardware/spi.h"

	uint8_t out_buf[FRAME_LEN];
	uint8_t dma_buf[FRAME_LEN];

	// DMA state for non-blocking SPI transfers
	static int kt_dma_chan = -1;
	static volatile bool kt_dma_busy = false;

	// DMA IRQ handler forward declaration
	static void kt_dma_irq_handler(void);

	// Debug variables for SPI transfer timing
	int buffer_time = 0;
	int start_time = 0;
	int end_time = 0;
	int avg_time = 0;
	int max_time = 0;
	unsigned long total_buffer_time = 0;
	unsigned long num_measurements = 0;
	
	// Variables to track SPI_transfer function timing
	uint64_t spi_transfer_start_us = 0;
	uint64_t spi_transfer_end_us = 0;
	uint32_t spi_transfer_max_us = 0;

	// DMA IRQ handler: clears DMA interrupt and mark channel not busy
	static void kt_dma_irq_handler(void) {
		if (kt_dma_chan >= 0) {
			dma_hw->ints0 = 1u << kt_dma_chan; // clear IRQ for our channel
		}
		kt_dma_busy = false;
	}

	void rumble_pin_setup() {
		gpio_init(_pinRumble);
		gpio_init(_pinBrake);
		gpio_set_dir(_pinRumble, GPIO_OUT);
		gpio_set_dir(_pinBrake, GPIO_OUT);
		gpio_put(_pinRumble, 1);
		gpio_put(_pinBrake, 0);
	}

	void SPI_setup() {
		spi_init(spi1, 1000*1000); //1MHz Baudrate
		gpio_set_function(_pinSpare0, GPIO_FUNC_SPI); //GPIO12 - Rx
		gpio_set_function(_pinSpare1, GPIO_FUNC_SPI); //GPIO13 - Cs
		gpio_set_function(_pinSpare2, GPIO_FUNC_SPI); //GPIO14 - SCK
		gpio_set_function(_pinLED, GPIO_FUNC_SPI);	  //GPIO15 - Tx

		// Initialize DMA channel for SPI TX (non-blocking)
		if (kt_dma_chan < 0) {
			kt_dma_chan = dma_claim_unused_channel(true);
			dma_channel_config c = dma_channel_get_default_config(kt_dma_chan);
			channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
			channel_config_set_read_increment(&c, true);
			channel_config_set_dreq(&c, DREQ_SPI1_TX);

			// Do not configure source/destination here; configure on each transfer
			dma_channel_set_irq0_enabled(kt_dma_chan, true);
			irq_set_exclusive_handler(DMA_IRQ_0, kt_dma_irq_handler);
			irq_set_enabled(DMA_IRQ_0, true);
		}
	}

	void SPI_transfer(uint8_t buttons_array[10], int calibration_step, bool safe_mode, IntOrFloat config[], uint8_t button_B, const ControlConfig &controls, int whichStick) {
		/*
		if (config[KT_SETTING_ENABLE].intValue != KT_ENABLED) {
       		return;
    	} 
		*/
		
		/*
		// Reset max time if B button is pressed
		if (button_B) {
			spi_transfer_max_us = 0;
		}
		spi_transfer_start_us = time_us_64();
		*/

		out_buf[0] = SOF_MARKER; // Start of Frame marker for buffer

		// Assign button states to out_buf (payload bytes 1-8)
		for(size_t i = 0; i < 8; ++i) {
			out_buf[i + 1] = buttons_array[i];
		}

		//out_buf[7] = (uint8_t)(spi_transfer_max_us);
		out_buf[9] = (uint8_t)(calibration_step + 128); // Calibration step (offset for signed values)

		// Pack ControlConfig data for calibration assistance (payload bytes 9-41)
		out_buf[10] = (uint8_t)controls.aRemap;
		out_buf[11] = (uint8_t)controls.bRemap;
		out_buf[12] = (uint8_t)controls.dRemap;
		out_buf[13] = (uint8_t)controls.lRemap;
		out_buf[14] = (uint8_t)controls.rRemap;
		out_buf[15] = (uint8_t)controls.xRemap;
		out_buf[16] = (uint8_t)controls.yRemap;
		out_buf[17] = (uint8_t)controls.zRemap;
		out_buf[18] = (uint8_t)controls.lConfig;
		out_buf[19] = (uint8_t)controls.rConfig;
		out_buf[20] = (uint8_t)controls.lTriggerOffset;
		out_buf[21] = (uint8_t)controls.rTriggerOffset;
		out_buf[22] = (uint8_t)controls.rumble;
		out_buf[23] = (uint8_t)controls.autoInit;
		out_buf[24] = (uint8_t)(controls.xSnapback + 128);      // A-stick X snapback
		out_buf[25] = (uint8_t)(controls.ySnapback + 128);      // A-stick Y snapback
		out_buf[26] = (uint8_t)(controls.cxSmoothing + 128);    // C-stick X snapback (stored in smoothing)
		out_buf[27] = (uint8_t)(controls.cySmoothing + 128);    // C-stick Y snapback (stored in smoothing)
		out_buf[28] = (uint8_t)controls.axSmoothing;
		out_buf[29] = (uint8_t)controls.aySmoothing;
		out_buf[30] = (uint8_t)controls.cxSmoothing;            // Duplicate for compatibility
		out_buf[31] = (uint8_t)controls.cySmoothing;            // Duplicate for compatibility
		out_buf[32] = (uint8_t)controls.axWaveshaping;
		out_buf[33] = (uint8_t)controls.ayWaveshaping;
		out_buf[34] = (uint8_t)controls.cxWaveshaping;
		out_buf[35] = (uint8_t)controls.cyWaveshaping;
		out_buf[36] = (uint8_t)(controls.astickCardinalSnapping + 128);
		out_buf[37] = (uint8_t)(controls.cstickCardinalSnapping + 128);
		out_buf[38] = (uint8_t)controls.astickAnalogScaler;
		out_buf[39] = (uint8_t)controls.cstickAnalogScaler;
		out_buf[40] = (uint8_t)controls.tournamentToggle;
		out_buf[41] = (uint8_t)whichStick; // Which stick is being calibrated (0=ASTICK, 1=CSTICK)
		out_buf[42] = (uint8_t)(safe_mode ? 1 : 0); // Safe mode status
		out_buf[FRAME_LEN - 1] = EOF_MARKER; // End of Frame marker

		if(spi_is_writable(spi1)) {
			// If DMA available and not busy, kick off a non-blocking DMA transfer
			if (kt_dma_chan >= 0 && !kt_dma_busy) {
				kt_dma_busy = true;
				// copy into dma buffer so caller may reuse out_buf immediately
				for (size_t _i = 0; _i < FRAME_LEN; ++_i) dma_buf[_i] = out_buf[_i];
				dma_channel_config c = dma_channel_get_default_config(kt_dma_chan);
				channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
				channel_config_set_read_increment(&c, true);
				channel_config_set_dreq(&c, DREQ_SPI1_TX);

				// Configure the DMA transfer: source = dma_buf, dest = SPI DR
				dma_channel_configure(kt_dma_chan, &c,
									&spi_get_hw(spi1)->dr, // dest
									dma_buf,               // src
									FRAME_LEN,             // transfer count
									true);                 // start immediately
				//dma_channel_wait_for_finish_blocking(kt_dma_chan); // Wait for transfer to complete (could be made non-blocking if desired)
			}
		}

		/*
		// End timing and update max time
		spi_transfer_end_us = time_us_64();
		uint32_t elapsed_us = (uint32_t)(spi_transfer_end_us - spi_transfer_start_us);
		if (elapsed_us > spi_transfer_max_us) {
			spi_transfer_max_us = elapsed_us;
		}
		*/
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