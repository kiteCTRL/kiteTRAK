#ifndef EXTRAS_KT_SHARED_H
#define EXTRAS_KT_SHARED_H

#include <stddef.h>
#include <stdint.h>

namespace ktshared {
    constexpr int BUF_LEN = 42;
    constexpr int FRAME_LEN = BUF_LEN + 2;
    constexpr uint8_t SOF_MARKER = 0xFA;
    constexpr uint8_t EOF_MARKER = 0xFB;
    constexpr uint32_t SPI_BAUD_RATE = 1000000;

    constexpr uint8_t encodeSigned(int value) {
        return static_cast<uint8_t>(value + 128);
    }

    constexpr int decodeSigned(uint8_t value) {
        return static_cast<int>(value) - 128;
    }

    struct KtData {
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
    };

    struct KtFrame {
        uint8_t sof;
        KtData data;
        uint8_t eof;
    };

    static_assert(sizeof(KtData) == BUF_LEN, "Unexpected kiteTRAK payload size");
    static_assert(sizeof(KtFrame) == FRAME_LEN, "Unexpected kiteTRAK frame size");
}

#endif // EXTRAS_KT_SHARED_H