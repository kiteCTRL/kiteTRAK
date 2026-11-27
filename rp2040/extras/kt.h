#ifndef EXTRAS_KT_H
#define EXTRAS_KT_H

#include <stddef.h>

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

	struct KtFrame {
		uint8_t sof;
		uint8_t buttons[8];
		uint8_t calibrationStepEncoded;
		uint8_t aRemap;
		uint8_t bRemap;
		uint8_t dRemap;
		uint8_t lRemap;
		uint8_t rRemap;
		uint8_t xRemap;
		uint8_t yRemap;
		uint8_t zRemap;
		uint8_t lConfig;
		uint8_t rConfig;
		uint8_t lTriggerOffset;
		uint8_t rTriggerOffset;
		uint8_t rumble;
		uint8_t autoInit;
		uint8_t xSnapbackEncoded;
		uint8_t ySnapbackEncoded;
		uint8_t cxSmoothingEncoded;
		uint8_t cySmoothingEncoded;
		uint8_t axSmoothing;
		uint8_t aySmoothing;
		uint8_t cxSmoothing;
		uint8_t cySmoothing;
		uint8_t axWaveshaping;
		uint8_t ayWaveshaping;
		uint8_t cxWaveshaping;
		uint8_t cyWaveshaping;
		uint8_t astickCardinalSnappingEncoded;
		uint8_t cstickCardinalSnappingEncoded;
		uint8_t astickAnalogScaler;
		uint8_t cstickAnalogScaler;
		uint8_t tournamentToggle;
		uint8_t whichStick;
		uint8_t safeMode;
		uint8_t eof;
	};

	static_assert(sizeof(KtFrame) == FRAME_LEN, "Unexpected kiteTRAK frame size");
	static_assert(offsetof(KtFrame, safeMode) == 42, "Unexpected safe-mode offset");

	#include "hardware/dma.h"
	#include "hardware/irq.h"
	#include "hardware/spi.h"

	KtFrame out_buf;
	KtFrame dma_buf;

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

	// returns true when data transfer was successfully initiated
	bool SPI_transfer(const Buttons &buttons, const ControlConfig &controls, int calibration_step, WhichStick whichStick) {
		/*
		if (controls.extras[extrasKtConfigSlot].config[KT_SETTING_ENABLE].intValue != KT_ENABLED) {
       		return;
    	} 
		*/
		
		/*
		// Reset max time if B button is pressed
		if (buttons.B) {
			spi_transfer_max_us = 0;
		}
		spi_transfer_start_us = time_us_64();
		*/

		out_buf.sof = SOF_MARKER; // Start of Frame marker for buffer

		// Assign button states to out_buf (payload bytes 1-8)
		for(size_t i = 0; i < 8; ++i) {
			out_buf.buttons[i] = buttons.arr[i];
		}

		//out_buf.buttons[6] = (uint8_t)(spi_transfer_max_us);
		out_buf.calibrationStepEncoded = (uint8_t)(calibration_step + 128);

		// Pack ControlConfig data for calibration assistance (payload bytes 9-41)
		out_buf.aRemap = (uint8_t)controls.aRemap;
		out_buf.bRemap = (uint8_t)controls.bRemap;
		out_buf.dRemap = (uint8_t)controls.dRemap;
		out_buf.lRemap = (uint8_t)controls.lRemap;
		out_buf.rRemap = (uint8_t)controls.rRemap;
		out_buf.xRemap = (uint8_t)controls.xRemap;
		out_buf.yRemap = (uint8_t)controls.yRemap;
		out_buf.zRemap = (uint8_t)controls.zRemap;
		out_buf.lConfig = (uint8_t)controls.lConfig;
		out_buf.rConfig = (uint8_t)controls.rConfig;
		out_buf.lTriggerOffset = (uint8_t)controls.lTriggerOffset;
		out_buf.rTriggerOffset = (uint8_t)controls.rTriggerOffset;
		out_buf.rumble = (uint8_t)controls.rumble;
		out_buf.autoInit = (uint8_t)controls.autoInit;
		out_buf.xSnapbackEncoded = (uint8_t)(controls.xSnapback + 128);
		out_buf.ySnapbackEncoded = (uint8_t)(controls.ySnapback + 128);
		out_buf.cxSmoothingEncoded = (uint8_t)(controls.cxSmoothing + 128);
		out_buf.cySmoothingEncoded = (uint8_t)(controls.cySmoothing + 128);
		out_buf.axSmoothing = (uint8_t)controls.axSmoothing;
		out_buf.aySmoothing = (uint8_t)controls.aySmoothing;
		out_buf.cxSmoothing = (uint8_t)controls.cxSmoothing;
		out_buf.cySmoothing = (uint8_t)controls.cySmoothing;
		out_buf.axWaveshaping = (uint8_t)controls.axWaveshaping;
		out_buf.ayWaveshaping = (uint8_t)controls.ayWaveshaping;
		out_buf.cxWaveshaping = (uint8_t)controls.cxWaveshaping;
		out_buf.cyWaveshaping = (uint8_t)controls.cyWaveshaping;
		out_buf.astickCardinalSnappingEncoded = (uint8_t)(controls.astickCardinalSnapping + 128);
		out_buf.cstickCardinalSnappingEncoded = (uint8_t)(controls.cstickCardinalSnapping + 128);
		out_buf.astickAnalogScaler = (uint8_t)controls.astickAnalogScaler;
		out_buf.cstickAnalogScaler = (uint8_t)controls.cstickAnalogScaler;
		out_buf.tournamentToggle = (uint8_t)controls.tournamentToggle;
		out_buf.whichStick = (uint8_t)whichStick;
		out_buf.safeMode = (uint8_t)controls.safeMode;
		out_buf.eof = EOF_MARKER; // End of Frame marker

		if(spi_is_writable(spi1)) {
			// If DMA available and not busy, kick off a non-blocking DMA transfer
			if (kt_dma_chan >= 0 && !kt_dma_busy) {
				kt_dma_busy = true;
				// copy into dma buffer so caller may reuse out_buf immediately
				dma_buf = out_buf;
				dma_channel_config c = dma_channel_get_default_config(kt_dma_chan);
				channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
				channel_config_set_read_increment(&c, true);
				channel_config_set_dreq(&c, DREQ_SPI1_TX);

				// Configure the DMA transfer: source = dma_buf, dest = SPI DR
				dma_channel_configure(kt_dma_chan, &c,
									&spi_get_hw(spi1)->dr, // dest
									&dma_buf,              // src
									FRAME_LEN,             // transfer count
									true);                 // start immediately
				//dma_channel_wait_for_finish_blocking(kt_dma_chan); // Wait for transfer to complete (could be made non-blocking if desired)

				return true;
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

		return false; // no transfer initiated
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