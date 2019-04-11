// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/gpio.h>
#include <ddktl/metadata/audio.h>
#include <fbl/algorithm.h>
#include <hwreg/bitfields.h>
#include <soc/mt8167/mt8167-clk.h>
#include <soc/mt8167/mt8167-gpio.h>
#include <soc/mt8167/mt8167-hw.h>

#include "mt8167.h"

namespace board_mt8167 {

class WACS2_CMD : public hwreg::RegisterBase<WACS2_CMD, uint32_t> {
public:
    static auto Get() { return hwreg::RegisterAddr<WACS2_CMD>(0x00A0); }

    DEF_BIT(31, WACS2_WRITE);
    DEF_FIELD(30, 16, WACS2_ADR);
    DEF_FIELD(15, 0, WACS2_WDATA);
};

class WACS2_RDATA : public hwreg::RegisterBase<WACS2_RDATA, uint32_t> {
public:
    static constexpr uint32_t kStateIdle = 0;

    static auto Get() { return hwreg::RegisterAddr<WACS2_RDATA>(0x00A4); }

    DEF_FIELD(18, 16, status);
};

zx_status_t Mt8167::AudioInit() {
    if (board_info_.pid != PDEV_PID_MEDIATEK_8167S_REF &&
        board_info_.pid != PDEV_PID_CLEO) {
        // We only support the boards listed above.
        return ZX_ERR_NOT_SUPPORTED;
    }
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
        {.clk = board_mt8167::kClkRgAud1},
        {.clk = board_mt8167::kClkRgAud2},
    };

    static constexpr pbus_bti_t btis_out[] = {
        {
            .iommu_index = 0,
            .bti_id = BTI_AUDIO_OUT,
        },
    };

    constexpr pbus_gpio_t gpios_in[] = {
        {
            .gpio = MT8167_GPIO24_EINT24, // ~ADC_RESET.
        },
    };
    static constexpr pbus_bti_t btis_in[] = {
        {
            .iommu_index = 0,
            .bti_id = BTI_AUDIO_IN,
        },
    };
    static constexpr pbus_i2c_channel_t i2cs_in[] = {
        {
            .bus_id = 1,
            .address = 0x1B,
        },
    };

    metadata::Codec out_codec = metadata::Codec::Tas5782; // Default to PDEV_PID_MEDIATEK_8167S_REF.
    if (board_info_.pid == PDEV_PID_CLEO) {
        out_codec = metadata::Codec::Tas5805;
    }
    pbus_metadata_t out_metadata[] = {
        {
            .type = DEVICE_METADATA_PRIVATE,
            .data_buffer = &out_codec,
            .data_size = sizeof(out_codec),
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
    dev_out.bti_list = btis_out;
    dev_out.bti_count = countof(btis_out);
    dev_out.metadata_list = out_metadata;
    dev_out.metadata_count = countof(out_metadata);

    pbus_gpio_t mt8167s_ref_gpios_out[] = {
        {
            .gpio = MT8167_GPIO107_MSDC1_DAT1, // ~AMP_RESET.
        },
        {
            .gpio = MT8167_GPIO108_MSDC1_DAT2, // ~AMP_MUTE.
        },
    };
    // No reset/mute on Cleo.
    if (board_info_.pid == PDEV_PID_MEDIATEK_8167S_REF) {
        dev_out.gpio_list = mt8167s_ref_gpios_out;
        dev_out.gpio_count = countof(mt8167s_ref_gpios_out);
    } else {
        dev_out.gpio_list = nullptr;
        dev_out.gpio_count = 0;
    }

    pbus_i2c_channel_t mt8167s_ref_i2cs_out[] = {
        {
            .bus_id = 2,
            .address = 0x48,
        },
    };
    pbus_i2c_channel_t cleo_i2cs_out[] = {
        {
            .bus_id = 2,
            .address = 0x2C,
        },
    };
    if (board_info_.pid == PDEV_PID_MEDIATEK_8167S_REF) {
        dev_out.i2c_channel_list = mt8167s_ref_i2cs_out;
        dev_out.i2c_channel_count = countof(mt8167s_ref_i2cs_out);
    } else {
        dev_out.i2c_channel_list = cleo_i2cs_out;
        dev_out.i2c_channel_count = countof(cleo_i2cs_out);
    }

    pbus_dev_t dev_in = {};
    dev_in.name = "mt8167-audio-in";
    dev_in.vid = PDEV_VID_MEDIATEK;
    dev_in.pid = PDEV_PID_MEDIATEK_8167S_REF;
    dev_in.did = PDEV_DID_MEDIATEK_AUDIO_IN;
    dev_in.mmio_list = mmios;
    dev_in.mmio_count = countof(mmios);
    dev_in.clk_list = clks;
    dev_in.clk_count = countof(clks);
    dev_in.gpio_list = gpios_in;
    dev_in.gpio_count = countof(gpios_in);
    dev_in.bti_list = btis_in;
    dev_in.bti_count = countof(btis_in);
    dev_in.i2c_channel_list = i2cs_in;
    dev_in.i2c_channel_count = countof(i2cs_in);

    // Output pin assignments.
    // Datasheet has 2 numberings for I2S engines: I2S[0-3] (used in GPIOs) and I2S[1-4] (other
    // registers). 8CH corresponds to I2S2 in the 1-4 range (MtAudioOutDevice::I2S2).
    gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO25_EINT25, MT8167_GPIO25_I2S2_MCK_FN);
    gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO55_I2S_DATA_IN,
                               MT8167_GPIO55_I2S_8CH_DO1_FN);
    gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO56_I2S_LRCK, MT8167_GPIO56_I2S_8CH_LRCK_FN);
    gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO25_I2S_BCK, MT8167_GPIO57_I2S_8CH_BCK_FN);

    // No reset/mute on Cleo.
    if (board_info_.pid == PDEV_PID_MEDIATEK_8167S_REF) {
        // ~AMP_RESET.
        gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO107_MSDC1_DAT1, MT8167_GPIO_GPIO_FN);
        gpio_impl_config_out(&gpio_impl_, MT8167_GPIO107_MSDC1_DAT1, 1); // Set to "not reset".

        // ~AMP_MUTE.
        gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO108_MSDC1_DAT2, MT8167_GPIO_GPIO_FN);
        gpio_impl_config_out(&gpio_impl_, MT8167_GPIO108_MSDC1_DAT2, 1); // Set to "not mute".
    }

    // Input pin assignments.
    gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO100_CMDAT0, MT8167_GPIO100_TDM_RX_MCK_FN);
    gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO101_CMDAT1, MT8167_GPIO101_TDM_RX_BCK_FN);
    if (board_info_.pid == PDEV_PID_MEDIATEK_8167S_REF) {
        gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO16_EINT16, MT8167_GPIO16_TDM_RX_LRCK_FN);
    } else {
        gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO102_CMMCLK,
                                   MT8167_GPIO102_TDM_RX_LRCK_FN);
    }
    gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO103_CMPCLK, MT8167_GPIO103_TDM_RX_DI_FN);

    // ~ADC_RESET.
    gpio_impl_set_alt_function(&gpio_impl_, MT8167_GPIO24_EINT24, MT8167_GPIO_GPIO_FN);
    gpio_impl_config_out(&gpio_impl_, MT8167_GPIO24_EINT24, 1); // Set to "not reset".

    zx::unowned_resource root_resource(get_root_resource());
    std::optional<ddk::MmioBuffer> pmic_mmio;
    auto status = ddk::MmioBuffer::Create(MT8167_PMIC_WRAP_BASE, MT8167_PMIC_WRAP_SIZE,
                                          *root_resource, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                          &pmic_mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: PMIC MmioBuffer::Create failed %d\n", __FUNCTION__, status);
        return status;
    }

    // Wait for the PMIC to be IDLE.
    while (WACS2_RDATA::Get().ReadFrom(&(*pmic_mmio)).status() != WACS2_RDATA::kStateIdle) {
    }

    // Set the VCN 1.8 Volts by sending a command to the PMIC via the SOC's PMIC WRAP interface.
    constexpr uint32_t kDigLdoCon11 = 0x0512;
    constexpr uint16_t kVcn18Enable = 0x4001;
    auto pmic = WACS2_CMD::Get().ReadFrom(&(*pmic_mmio));
    // From the documentation "Wrapper access: Address[15:1]" hence the >> 1.
    pmic.set_WACS2_WRITE(1).set_WACS2_ADR(kDigLdoCon11 >> 1).set_WACS2_WDATA(kVcn18Enable);
    pmic.WriteTo(&(*pmic_mmio));

    status = pbus_.DeviceAdd(&dev_out);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pbus_.DeviceAdd failed %d\n", __FUNCTION__, status);
        return status;
    }
    status = pbus_.DeviceAdd(&dev_in);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pbus_.DeviceAdd failed %d\n", __FUNCTION__, status);
        return status;
    }
    return ZX_OK;
}

} // namespace board_mt8167
