// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-tsensor.h"
#include "aml-tsensor-regs.h"
#include <ddk/debug.h>
#include <fbl/auto_call.h>
#include <fbl/unique_ptr.h>
#include <hw/reg.h>
#include <threads.h>
#include <unistd.h>

namespace thermal {

// clang-format off
// MMIO indexes.
static constexpr uint32_t kPllMmio      = 0;
static constexpr uint32_t kAoMmio       = 1;
static constexpr uint32_t kHiuMmio      = 2;

// Thermal calibration magic numbers from uboot.
static constexpr int32_t kCalA_         = 324;
static constexpr int32_t kCalB_         = 424;
static constexpr int32_t kCalC_         = 3159;
static constexpr int32_t kCalD_         = 9411;
static constexpr uint32_t kRebootTemp   = 130000;
// clang-format on

zx_status_t AmlTSensor::InitPdev(zx_device_t* parent) {
    zx_status_t status = device_get_protocol(parent,
                                             ZX_PROTOCOL_PLATFORM_DEV,
                                             &pdev_);
    if (status != ZX_OK) {
        return status;
    }

    // Map amlogic temperature sensopr peripheral control registers.
    status = pdev_map_mmio_buffer(&pdev_, kPllMmio, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                  &pll_mmio_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-tsensor: could not map periph mmio: %d\n", status);
        return status;
    }

    status = pdev_map_mmio_buffer(&pdev_, kAoMmio, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                  &ao_mmio_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-tsensor: could not map periph mmio: %d\n", status);
        return status;
    }

    status = pdev_map_mmio_buffer(&pdev_, kHiuMmio, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                  &hiu_mmio_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-tsensor: could not map periph mmio: %d\n", status);
        return status;
    }

    // Map tsensor interrupt.
    status = pdev_map_interrupt(&pdev_, 0, tsensor_irq_.reset_and_get_address());
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-tsensor: could not map tsensor interrupt\n");
        return status;
    }

    pll_regs_ = fbl::make_unique<hwreg::RegisterIo>(reinterpret_cast<volatile void*>(
        io_buffer_virt(&pll_mmio_)));

    ao_regs_ = fbl::make_unique<hwreg::RegisterIo>(reinterpret_cast<volatile void*>(
        io_buffer_virt(&ao_mmio_)));

    hiu_regs_ = fbl::make_unique<hwreg::RegisterIo>(reinterpret_cast<volatile void*>(
        io_buffer_virt(&hiu_mmio_)));
    return ZX_OK;
}

// Tsensor treats temperature as a mapped temperature code.
// The temperature is converted differently depending on the calibration type.
uint32_t AmlTSensor::TempToCode(uint32_t temp, bool trend) {
    int64_t sensor_code;
    uint32_t reg_code;
    uint32_t uefuse = trim_info_ & 0xffff;

    // Referred u-boot code for below magic calculations.
    // T = 727.8*(u_real+u_efuse/(1<<16)) - 274.7
    // u_readl = (5.05*YOUT)/((1<<16)+ 4.05*YOUT)
    // u_readl = (T + 274.7) / 727.8 - u_efuse / (1 << 16)
    // Yout =  (u_readl / (5.05 - 4.05u_readl)) *(1 << 16)
    if (uefuse & 0x8000) {
        sensor_code = ((1 << 16) * (temp * 10 + kCalC_) / kCalD_ +
                       (1 << 16) * (uefuse & 0x7fff) / (1 << 16));
    } else {
        sensor_code = ((1 << 16) * (temp * 10 + kCalC_) / kCalD_ -
                       (1 << 16) * (uefuse & 0x7fff) / (1 << 16));
    }

    sensor_code = (sensor_code * 100 / (kCalB_ - kCalA_ * sensor_code / (1 << 16)));
    if (trend) {
        reg_code = static_cast<uint32_t>((sensor_code >> 0x4) & AML_TS_TEMP_MASK) + AML_TEMP_CAL;
    } else {
        reg_code = ((sensor_code >> 0x4) & AML_TS_TEMP_MASK);
    }
    return reg_code;
}

// Calculate a temperature value from a temperature code.
// The unit of the temperature is degree Celsius.
uint32_t AmlTSensor::CodeToTemp(uint32_t temp_code) {
    uint32_t sensor_temp = temp_code;
    uint32_t uefuse = trim_info_ & 0xffff;

    // Referred u-boot code for below magic calculations.
    // T = 727.8*(u_real+u_efuse/(1<<16)) - 274.7
    // u_readl = (5.05*YOUT)/((1<<16)+ 4.05*YOUT)
    sensor_temp = ((sensor_temp * kCalB_) / 100 * (1 << 16) /
                   (1 * (1 << 16) + kCalA_ * sensor_temp / 100));
    if (uefuse & 0x8000) {
        sensor_temp = (1000 * ((sensor_temp - (uefuse & (0x7fff))) * kCalD_ / (1 << 16) - kCalC_) / 10);
    } else {
        sensor_temp = 1000 * ((sensor_temp + uefuse) * kCalD_ / (1 << 16) - kCalC_) / 10;
    }
    return sensor_temp;
}

uint32_t AmlTSensor::ReadTemperature() {
    int count = 0;
    volatile unsigned int tvalue = 0;
    unsigned int value_all = 0;

    // Datasheet is incorrect.
    // Referred to u-boot code.
    // Yay magic numbers.
    for (int j = 0; j < AML_TS_VALUE_CONT; j++) {
        tvalue = pll_regs_->Read<uint32_t>(AML_TS_STAT0);
        tvalue = tvalue & 0xffff;
        if ((tvalue >= 0x18a9) && (tvalue <= 0x32a6)) {
            count++;
            value_all += (tvalue & 0xffff);
        }
    }
    if (count == 0) {
        return 0;
    } else {
        return CodeToTemp(value_all / count) / MCELSIUS;
    }
}

void AmlTSensor::SetRebootTemperature(uint32_t temp) {
    uint32_t reboot_val = TempToCode(kRebootTemp / MCELSIUS, true);
    uint32_t reboot_config = pll_regs_->Read<uint32_t>(AML_TS_CFG_REG2);

    reboot_config |= reboot_val << 4;
    reboot_config |= AML_TS_HITEMP_EN | AML_TS_REBOOT_ALL_EN | AML_TS_REBOOT_TIME;
    pll_regs_->Write(AML_TS_CFG_REG2, reboot_config);
}

zx_status_t AmlTSensor::InitSensor(zx_device_t* parent) {
    zx_status_t status = InitPdev(parent);
    if (status != ZX_OK) {
        return status;
    }

    // Get the trim info.
    trim_info_ = ao_regs_->Read<uint32_t>(AML_TRIM_INFO);

    // Set the clk.
    hiu_regs_->Write(AML_HHI_TS_CLK_CNTL, AML_HHI_TS_CLK_ENABLE);

    // Not setting IRQ's here.
    uint32_t sensor_ctl = pll_regs_->Read<uint32_t>(AML_TS_CFG_REG1);
    sensor_ctl |= (AML_TS_FILTER_EN | AML_TS_VCM_EN | AML_TS_VBG_EN | AML_TS_CH_SEL |
                   AML_TS_IPTAT_EN | AML_TS_DEM_EN);
    pll_regs_->Write(AML_TS_CFG_REG1, sensor_ctl);
    return ZX_OK;
}

void AmlTSensor::ShutDown() {
    tsensor_irq_.destroy();
    io_buffer_release(&pll_mmio_);
    io_buffer_release(&ao_mmio_);
    io_buffer_release(&hiu_mmio_);
}

} // namespace thermal
