#ifndef EXTRAS_KT_H
#define EXTRAS_KT_H

#include <stddef.h>
#include "ktShared.h"

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

	#include "hardware/dma.h"
	#include "hardware/irq.h"
	#include "hardware/spi.h"

	ktshared::KtFrame out_buf;
	ktshared::KtFrame dma_buf;

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
		spi_init(spi1, ktshared::SPI_BAUD_RATE);
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

		out_buf.sof = ktshared::SOF_MARKER; // Start of Frame marker for buffer

		// Assign button states to out_buf (payload bytes 1-8)
		for(size_t i = 0; i < 8; ++i) {
			out_buf.data.buttons[i] = buttons.arr[i];
		}

		//out_buf.data.buttons[6] = (uint8_t)(spi_transfer_max_us);
		out_buf.data.calibrationStepEncoded = ktshared::encodeSigned(calibration_step);

		// Pack ControlConfig data for calibration assistance (payload bytes 9-41)
		out_buf.data.aRemap = (uint8_t)controls.aRemap;
		out_buf.data.bRemap = (uint8_t)controls.bRemap;
		out_buf.data.dRemap = (uint8_t)controls.dRemap;
		out_buf.data.lRemap = (uint8_t)controls.lRemap;
		out_buf.data.rRemap = (uint8_t)controls.rRemap;
		out_buf.data.xRemap = (uint8_t)controls.xRemap;
		out_buf.data.yRemap = (uint8_t)controls.yRemap;
		out_buf.data.zRemap = (uint8_t)controls.zRemap;
		out_buf.data.lConfig = (uint8_t)controls.lConfig;
		out_buf.data.rConfig = (uint8_t)controls.rConfig;
		out_buf.data.lTriggerOffset = (uint8_t)controls.lTriggerOffset;
		out_buf.data.rTriggerOffset = (uint8_t)controls.rTriggerOffset;
		out_buf.data.rumble = (uint8_t)controls.rumble;
		out_buf.data.autoInit = (uint8_t)controls.autoInit;
		out_buf.data.xSnapbackEncoded = ktshared::encodeSigned(controls.xSnapback);
		out_buf.data.ySnapbackEncoded = ktshared::encodeSigned(controls.ySnapback);
		out_buf.data.cxSmoothingEncoded = ktshared::encodeSigned(controls.cxSmoothing);
		out_buf.data.cySmoothingEncoded = ktshared::encodeSigned(controls.cySmoothing);
		out_buf.data.axSmoothing = (uint8_t)controls.axSmoothing;
		out_buf.data.aySmoothing = (uint8_t)controls.aySmoothing;
		out_buf.data.cxSmoothing = (uint8_t)controls.cxSmoothing;
		out_buf.data.cySmoothing = (uint8_t)controls.cySmoothing;
		out_buf.data.axWaveshaping = (uint8_t)controls.axWaveshaping;
		out_buf.data.ayWaveshaping = (uint8_t)controls.ayWaveshaping;
		out_buf.data.cxWaveshaping = (uint8_t)controls.cxWaveshaping;
		out_buf.data.cyWaveshaping = (uint8_t)controls.cyWaveshaping;
		out_buf.data.astickCardinalSnappingEncoded = ktshared::encodeSigned(controls.astickCardinalSnapping);
		out_buf.data.cstickCardinalSnappingEncoded = ktshared::encodeSigned(controls.cstickCardinalSnapping);
		out_buf.data.astickAnalogScaler = (uint8_t)controls.astickAnalogScaler;
		out_buf.data.cstickAnalogScaler = (uint8_t)controls.cstickAnalogScaler;
		out_buf.data.tournamentToggle = (uint8_t)controls.tournamentToggle;
		out_buf.data.whichStick = (uint8_t)whichStick;
		out_buf.data.safeMode = (uint8_t)controls.safeMode;
		out_buf.eof = ktshared::EOF_MARKER; // End of Frame marker

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
									ktshared::FRAME_LEN,    // transfer count
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
	
	bool SPI_transfer(const Buttons &buttons, const ControlConfig &controls) {
		return SPI_transfer(
			buttons, 
			controls, 
			ktshared::decodeSigned(out_buf.data.calibrationStepEncoded),
			static_cast<WhichStick>(out_buf.data.whichStick));
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