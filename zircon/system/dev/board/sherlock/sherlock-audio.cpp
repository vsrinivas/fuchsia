// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sherlock.h"

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/metadata.h>
#include <ddktl/metadata/audio.h>
#include <soc/aml-s905d2/s905d2-hiu.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

namespace sherlock {

zx_status_t Sherlock::AudioInit() {

    static constexpr pbus_gpio_t audio_gpios[] = {
        {
            // AUDIO_SOC_FAULT_L
            .gpio = T931_GPIOZ(8),
        },
        {
            // SOC_AUDIO_EN
            .gpio = T931_GPIOH(7),
        },
    };

    static constexpr pbus_mmio_t audio_mmios[] = {
        {
            .base = T931_EE_AUDIO_BASE,
            .length = T931_EE_AUDIO_LENGTH
        },
        {
            .base = T931_GPIO_BASE,
            .length = T931_GPIO_LENGTH,
        },
        {
            .base = T931_GPIO_A0_BASE,
            .length = T931_GPIO_AO_LENGTH,
        },
    };

    static constexpr pbus_bti_t tdm_btis[] = {
        {
            .iommu_index = 0,
            .bti_id = BTI_AUDIO_OUT,
        },
    };

    static constexpr pbus_i2c_channel_t p2_codecs_i2cs[] = {
        {
            .bus_id = SHERLOCK_I2C_A0_0,
            .address = 0x6c, // Tweeters.
        },
        {
            .bus_id = SHERLOCK_I2C_A0_0,
            .address = 0x6f, // Woofer.
        },
    };

    static constexpr pbus_i2c_channel_t evt_codecs_i2cs[] = {
        {
            .bus_id = SHERLOCK_I2C_A0_0,
            .address = 0x6c, // Tweeter left.
        },
        {
            .bus_id = SHERLOCK_I2C_A0_0,
            .address = 0x6d, // Tweeter right.
        },
        {
            .bus_id = SHERLOCK_I2C_A0_0,
            .address = 0x6f, // Woofer.
        },
    };

    pdev_board_info_t board_info = {};
    zx_status_t status = pbus_.GetBoardInfo(&board_info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: GetBoardInfo failed %d\n", __FILE__, status);
        return status;
    }

    // We treat EVT and higher the same (having 3 TAS5720s).
    metadata::Codec out_codec = metadata::Codec::Tas5720x3;
    if (board_info.board_revision < BOARD_REV_P2) {
        return ZX_ERR_NOT_SUPPORTED; // For audio we don't support boards revision lower than P2.
    } else if (board_info.board_revision < BOARD_REV_EVT1) {
        out_codec = metadata::Codec::Tas5760_Tas5720; // We treat all P2 variants the same.
    }

    pbus_metadata_t out_metadata[] = {
        {
            .type = DEVICE_METADATA_PRIVATE,
            .data_buffer = &out_codec,
            .data_size = sizeof(out_codec),
        },
    };

    pbus_dev_t tdm_dev = {};
    tdm_dev.name = "SherlockAudio";
    tdm_dev.vid = PDEV_VID_AMLOGIC;
    tdm_dev.pid = PDEV_PID_AMLOGIC_T931;
    tdm_dev.did = PDEV_DID_AMLOGIC_TDM;
    tdm_dev.gpio_list = audio_gpios;
    tdm_dev.gpio_count = countof(audio_gpios);
    tdm_dev.mmio_list = audio_mmios;
    tdm_dev.mmio_count = countof(audio_mmios);
    tdm_dev.bti_list = tdm_btis;
    tdm_dev.bti_count = countof(tdm_btis);
    tdm_dev.metadata_list = out_metadata;
    tdm_dev.metadata_count = countof(out_metadata);

    if (board_info.board_revision < BOARD_REV_EVT1) {
        tdm_dev.i2c_channel_list = p2_codecs_i2cs;
        tdm_dev.i2c_channel_count = countof(p2_codecs_i2cs);
    } else {
        tdm_dev.i2c_channel_list = evt_codecs_i2cs;
        tdm_dev.i2c_channel_count = countof(evt_codecs_i2cs);
    }

    static constexpr pbus_mmio_t pdm_mmios[] = {
        {
            .base = T931_EE_PDM_BASE,
            .length = T931_EE_PDM_LENGTH
        },
        {
            .base = T931_EE_AUDIO_BASE,
            .length = T931_EE_AUDIO_LENGTH
        },
    };

    static constexpr pbus_bti_t pdm_btis[] = {
        {
            .iommu_index = 0,
            .bti_id = BTI_AUDIO_IN,
        },
    };

    pbus_dev_t pdm_dev;
    pdm_dev.name = "SherlockAudioIn";
    pdm_dev.vid = PDEV_VID_AMLOGIC;
    pdm_dev.pid = PDEV_PID_AMLOGIC_T931;
    pdm_dev.did = PDEV_DID_SHERLOCK_PDM;
    pdm_dev.mmio_list = pdm_mmios;
    pdm_dev.mmio_count = countof(pdm_mmios);
    pdm_dev.bti_list = pdm_btis;
    pdm_dev.bti_count = countof(pdm_btis);


    aml_hiu_dev_t hiu;
    status = s905d2_hiu_init(&hiu);
    if (status != ZX_OK) {
        zxlogf(ERROR, "hiu_init: failed: %d\n", status);
        return status;
    }

    aml_pll_dev_t hifi_pll;
    s905d2_pll_init(&hiu, &hifi_pll, HIFI_PLL);
    status = s905d2_pll_set_rate(&hifi_pll, T931_HIFI_PLL_RATE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Invalid rate selected for hifipll\n");
        return status;
    }

    s905d2_pll_ena(&hifi_pll);

    // TDM pin assignments.
    gpio_impl_.SetAltFunction(T931_GPIOZ(7), T931_GPIOZ_7_TDMC_SCLK_FN);
    gpio_impl_.SetAltFunction(T931_GPIOZ(6), T931_GPIOZ_6_TDMC_FS_FN);
    gpio_impl_.SetAltFunction(T931_GPIOZ(2), T931_GPIOZ_2_TDMC_D0_FN);
    gpio_impl_.SetAltFunction(T931_GPIOZ(3), T931_GPIOZ_3_TDMC_D1_FN);
    gpio_impl_.SetAltFunction(T931_GPIOAO(9), T931_GPIOAO_9_MCLK_FN);

    // PDM pin assignments.
    gpio_impl_.SetAltFunction(T931_GPIOA(7), T931_GPIOA_7_PDM_DCLK_FN);
    gpio_impl_.SetAltFunction(T931_GPIOA(8), T931_GPIOA_8_PDM_DIN0_FN);
    gpio_impl_.SetAltFunction(T931_GPIOA(9), T931_GPIOA_9_PDM_DIN1_FN);

    gpio_impl_.ConfigOut(T931_GPIOH(7), 1); // SOC_AUDIO_EN.

    status = pbus_.DeviceAdd(&tdm_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s pbus_.DeviceAdd failed %d\n", __FUNCTION__, status);
        return status;
    }
    status = pbus_.DeviceAdd(&pdm_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s pbus_.DeviceAdd failed %d\n", __FUNCTION__, status);
        return status;
    }
    return ZX_OK;
}

} // namespace sherlock
