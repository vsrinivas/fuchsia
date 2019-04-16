// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/pdev.h>
#include <fbl/unique_ptr.h>
#include <soc/mt8167/mt8167-power-regs.h>
#include <soc/mt8167/mt8167-power.h>

#include "mtk-power.h"

namespace power {

struct MtkPowerDomainInfo {
    const uint32_t enable_reg;
    const uint8_t enable_bit;
};

constexpr MtkPowerDomainInfo kMtkPowerDomains[] = {
    [kBuckVProc] = {.enable_reg = PMIC_VPROC_CON7, .enable_bit = 1},
    [kBuckVCore] = {.enable_reg = PMIC_VPROC_CON7, .enable_bit = 1},
    [kBuckVSys] = {.enable_reg = PMIC_VPROC_CON7, .enable_bit = 1},
    [kALdoVAud28] = {.enable_reg = PMIC_ANALDO_CON23, .enable_bit = 14},
    [kALdoVAud22] = {.enable_reg = PMIC_ANALDO_CON2, .enable_bit = 14},
    [kALdoVAdc18] = {.enable_reg = PMIC_ANALDO_CON25, .enable_bit = 14},
    [kALdoVXo22] = {.enable_reg = PMIC_ANALDO_CON1, .enable_bit = 10},
    [kALdoVCamA] = {.enable_reg = PMIC_ANALDO_CON4, .enable_bit = 15},
    [kVSysLdoVm] = {.enable_reg = PMIC_DIGLDO_CON47, .enable_bit = 14},
    [kVSysLdoVcn18] = {.enable_reg = PMIC_DIGLDO_CON11, .enable_bit = 14},
    [kVSysLdoVio18] = {.enable_reg = PMIC_DIGLDO_CON49, .enable_bit = 14},
    [kVSysLdoVCamIo] = {.enable_reg = PMIC_DIGLDO_CON53, .enable_bit = 14},
    [kVSysLdoVCamD] = {.enable_reg = PMIC_DIGLDO_CON51, .enable_bit = 14},
    [kVDLdoVcn35] = {.enable_reg = PMIC_ANALDO_CON21, .enable_bit = 12},
    [kVDLdoVio28] = {.enable_reg = PMIC_DIGLDO_CON0, .enable_bit = 14},
    [kVDLdoVemc33] = {.enable_reg = PMIC_DIGLDO_CON6, .enable_bit = 14},
    [kVDLdoVmc] = {.enable_reg = PMIC_DIGLDO_CON3, .enable_bit = 12},
    [kVDLdoVmch] = {.enable_reg = PMIC_DIGLDO_CON5, .enable_bit = 14},
    [kVDLdoVUsb33] = {.enable_reg = PMIC_DIGLDO_CON2, .enable_bit = 14},
    [kVDLdoVGp1] = {.enable_reg = PMIC_DIGLDO_CON7, .enable_bit = 15},
    [kVDLdoVM25] = {.enable_reg = PMIC_DIGLDO_CON55, .enable_bit = 14},
    [kVDLdoVGp2] = {.enable_reg = PMIC_DIGLDO_CON8, .enable_bit = 15},
    [kVDLdoVCamAf] = {.enable_reg = PMIC_DIGLDO_CON31, .enable_bit = 15},
};

void MtkPower::WaitForIdle() {
    while (PmicWacs2RData::Get().ReadFrom(&pmic_mmio_).wacs2_fsm()
           != PmicWacs2RData::kFsmStateIdle) {
    }
}

void MtkPower::WaitForValidClr() {
    while (PmicWacs2RData::Get().ReadFrom(&pmic_mmio_).wacs2_fsm()
           != PmicWacs2RData::kFsmStateWfVldClr) {
    }
}

zx_status_t MtkPower::ReadPMICReg(uint32_t reg_addr, uint32_t* reg_value) {
    WaitForIdle();
    PmicWacs2Cmd::Get()
        .FromValue(0)
        .set_wacs2_write(0)
        .set_wacs2_addr(reg_addr >> 1)
        .WriteTo(&pmic_mmio_);
    // Wait for data to be available.
    WaitForValidClr();

    *reg_value = PmicWacs2RData::Get().ReadFrom(&pmic_mmio_).wacs2_rdata();

    //Data is read. clear the valid flag.
    PmicWacs2VldClr::Get().ReadFrom(&pmic_mmio_).set_wacs2_vldclr(1).WriteTo(&pmic_mmio_);
    return ZX_OK;
}

zx_status_t MtkPower::WritePMICReg(uint32_t reg_addr, uint32_t value) {
    WaitForIdle();
    PmicWacs2Cmd::Get()
        .FromValue(0)
        .set_wacs2_write(1)
        .set_wacs2_addr(reg_addr >> 1)
        .set_wacs2_data(value)
        .WriteTo(&pmic_mmio_);
    return ZX_OK;
}

zx_status_t MtkPower::PowerImplDisablePowerDomain(uint32_t index) {
    if (index >= fbl::count_of(kMtkPowerDomains)) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    const MtkPowerDomainInfo* domain = &kMtkPowerDomains[index];
    uint32_t cur_val;
    uint32_t enable_mask = 1 << domain->enable_bit;
    zx_status_t status = ReadPMICReg(domain->enable_reg, &cur_val);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s Reading PMIC reg failed: %d\n", __FUNCTION__, status);
        return status;
    }
    if (!(cur_val & enable_mask)) {
        return ZX_ERR_BAD_STATE;
    }
    cur_val &= ~(enable_mask);
    status = WritePMICReg(domain->enable_reg, cur_val);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s Writing PMIC reg failed: %d\n", __FUNCTION__, status);
    }
    return status;
}

zx_status_t MtkPower::PowerImplEnablePowerDomain(uint32_t index) {
    if (index >= fbl::count_of(kMtkPowerDomains)) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    const MtkPowerDomainInfo* domain = &kMtkPowerDomains[index];
    uint32_t enable_mask = 1 << domain->enable_bit;
    uint32_t cur_val;
    zx_status_t status = ReadPMICReg(domain->enable_reg, &cur_val);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s Reading PMIC reg failed: %d\n", __FUNCTION__, status);
        return status;
    }
    if (cur_val & enable_mask) {
        return ZX_ERR_BAD_STATE;
    }
    status = WritePMICReg(domain->enable_reg, (1 << domain->enable_bit));
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s Writing PMIC reg failed: %d\n", __FUNCTION__, status);
    }
    return status;
}

zx_status_t MtkPower::PowerImplGetPowerDomainStatus(uint32_t index,
                                                    power_domain_status_t* out_status) {
    if (index >= fbl::count_of(kMtkPowerDomains)) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    const MtkPowerDomainInfo* domain = &kMtkPowerDomains[index];
    uint32_t enable_mask = 1 << domain->enable_bit;
    uint32_t cur_val;
    zx_status_t status = ReadPMICReg(domain->enable_reg, &cur_val);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s Reading PMIC reg failed: %d\n", __FUNCTION__, status);
        return status;
    }
    *out_status = POWER_DOMAIN_STATUS_DISABLED;
    if (cur_val & enable_mask) {
        *out_status = POWER_DOMAIN_STATUS_ENABLED;
    }
    return ZX_OK;
}

zx_status_t MtkPower::PowerImplGetSupportedVoltageRange(uint32_t index, uint32_t* min_voltage,
                                                        uint32_t* max_voltage) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MtkPower::PowerImplRequestVoltage(uint32_t index, uint32_t voltage,
                                              uint32_t* out_voltage) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MtkPower::PowerImplWritePmicCtrlReg(uint32_t index, uint32_t reg_addr,
                                                uint32_t value) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MtkPower::PowerImplReadPmicCtrlReg(uint32_t index, uint32_t addr, uint32_t* value) {
    return ZX_ERR_NOT_SUPPORTED;
}

void MtkPower::DdkRelease() {
    delete this;
}

void MtkPower::DdkUnbind() {
    DdkRemove();
}

zx_status_t MtkPower::Init() {
    //TODO(ravoorir): Check if bootloader did not init the PMIC and
    // do the needful.
    return ZX_OK;
}

zx_status_t MtkPower::Bind() {
    pbus_protocol_t pbus;
    zx_status_t status = device_get_protocol(parent(), ZX_PROTOCOL_PBUS, &pbus);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s failed to get ZX_PROTOCOL_PBUS, %d\n", __FUNCTION__, status);
        return status;
    }

    power_impl_protocol_t power_proto = {
        .ops = &power_impl_protocol_ops_,
        .ctx = this,
    };

    status = pbus_register_protocol(&pbus, ZX_PROTOCOL_POWER_IMPL,
                                    &power_proto,
                                    sizeof(power_proto));
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s pbus_register_protocol failed: %d\n", __FUNCTION__, status);
        return status;
    }
    status = DdkAdd("mtk-power");
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s DdkAdd failed: %d\n", __FUNCTION__, status);
    }

    return status;
}

zx_status_t MtkPower::Create(void* ctx, zx_device_t* parent) {
    zx_status_t status = ZX_OK;

    ddk::PDev pdev(parent);
    if (!pdev.is_valid()) {
        zxlogf(ERROR, "%s Could not get pdev: %d\n", __FUNCTION__, status);
        return ZX_ERR_NO_RESOURCES;
    }

    std::optional<ddk::MmioBuffer> mmio;
    status = pdev.MapMmio(0, &mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s Failed to get mmio: %d\n", __FUNCTION__, status);
        return status;
    }
    auto dev = std::make_unique<MtkPower>(parent, pdev, *std::move(mmio));

    if ((status = dev->Init()) != ZX_OK) {
        return status;
    }

    if ((status = dev->Bind()) != ZX_OK) {
        return status;
    }

    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = dev.release();
    return ZX_OK;
}

static zx_driver_ops_t mtk_power_driver_ops = []() {
    zx_driver_ops_t driver_ops;
    driver_ops.version = DRIVER_OPS_VERSION;
    driver_ops.bind = MtkPower::Create;
    return driver_ops;
}();

} //namespace power

ZIRCON_DRIVER_BEGIN(mtk_power, power::mtk_power_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_MEDIATEK),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_MEDIATEK_POWER),
ZIRCON_DRIVER_END(mtk_power)
