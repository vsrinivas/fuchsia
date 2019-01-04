// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/gpio.h>
#include <soc/mt8167/mt8167-clk.h>
#include <soc/mt8167/mt8167-gpio.h>
#include <soc/mt8167/mt8167-hw.h>

#include "mt8167.h"

namespace board_mt8167 {

zx_status_t Mt8167::AudioInit() {
    constexpr pbus_mmio_t mmios[] = {
        {
            .base = MT8167_AUDIO_BASE,
            .length = MT8167_AUDIO_SIZE,
        },
        // MMIO for clocks.
        // TODO(andresoportus): Move this to a clock driver.
        {
            .base = MT8167_XO_BASE,
            .length = MT8167_XO_SIZE,
        },
        {
            .base = MT8167_PLL_BASE,
            .length = MT8167_PLL_SIZE,
        },
    };
    static constexpr pbus_clk_t clks[] = {
        {
            .clk = board_mt8167::kClkAud1
        },
    };

    constexpr pbus_gpio_t gpios_out[] = {
        {
            .gpio = MT8167_GPIO107_MSDC1_DAT1, // ~AMP_RESET.
        },
        {
            .gpio = MT8167_GPIO108_MSDC1_DAT2, // ~AMP_MUTE.
        },
    };
    static constexpr pbus_bti_t btis_out[] = {
        {
            .iommu_index = 0,
            .bti_id = BTI_AUDIO_OUT,
        },
    };
    static constexpr pbus_i2c_channel_t i2cs_out[] = {
        {
            .bus_id = 2,
            .address = 0x48,
        },
    };

    pbus_dev_t dev_out = {};
    dev_out.name = "mt8167-audio-out";
    dev_out.vid = PDEV_VID_MEDIATEK;
    dev_out.pid = PDEV_PID_MEDIATEK_8167S_REF;
    dev_out.did = PDEV_DID_MEDIATEK_AUDIO_OUT;
    dev_out.mmio_list = mmios;
    dev_out.mmio_count = countof(mmios);
    dev_out.clk_list = clks;
    dev_out.clk_count = countof(clks);
    dev_out.gpio_list = gpios_out;
    dev_out.gpio_count = countof(gpios_out);
    dev_out.bti_list = btis_out;
    dev_out.bti_count = countof(btis_out);
    dev_out.i2c_channel_list = i2cs_out;
    dev_out.i2c_channel_count = countof(i2cs_out);

    // Output pin assignments.
    // Datasheet has 2 numberings for I2S engines: I2S[0-3] (used in GPIOs) and I2S[1-4] (other
    // registers). 8CH corresponds to I2S2 in the 1-4 range (MtAudioOutDevice::I2S2).
    gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO25_EINT25, MT8167_GPIO25_I2S2_MCK_FN);
    gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO55_I2S_DATA_IN,
                               MT8167_GPIO55_I2S_8CH_DO1_FN);
    gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO56_I2S_LRCK, MT8167_GPIO56_I2S_8CH_LRCK_FN);
    gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO25_I2S_BCK, MT8167_GPIO57_I2S_8CH_BCK_FN);

    // ~AMP_RESET.
    gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO107_MSDC1_DAT1, MT8167_GPIO_GPIO_FN);
    gpio_impl_config_out(&gpio_impl_, MT8167_GPIO107_MSDC1_DAT1, 1); // Set to "not reset".

    // ~AMP_MUTE.
    gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO108_MSDC1_DAT2, MT8167_GPIO_GPIO_FN);
    gpio_impl_config_out(&gpio_impl_, MT8167_GPIO108_MSDC1_DAT2, 1); // Set to "not mute".

    zx_status_t status = pbus_.DeviceAdd(&dev_out);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pbus_.DeviceAdd failed %d\n", __FUNCTION__, status);
        return status;
    }
    return ZX_OK;
}

} // namespace board_mt8167
