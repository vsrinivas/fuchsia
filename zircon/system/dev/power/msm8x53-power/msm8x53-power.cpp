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
#include <soc/msm8x53/msm8x53-power-regs.h>
#include <soc/msm8x53/msm8x53-power.h>

#include "msm8x53-power.h"

namespace power {

constexpr Msm8x53PowerDomainInfo kMsm8x53PowerDomains[] = {
    [kVRegS1] = {.type = RPM_REGULATOR},
    [kVRegS2] = {.type = RPM_REGULATOR},
    [kVRegS3] = {.type = RPM_REGULATOR},
    [kVRegS4] = {.type = RPM_REGULATOR},
    [kVRegS5] = {.type = RPM_REGULATOR},
    [kVRegS6] = {.type = RPM_REGULATOR},
    [kVRegS7] = {.type = RPM_REGULATOR},
    [kVRegLdoA1] = {.type = RPM_REGULATOR},
    [kVRegLdoA2] = {.type = RPM_REGULATOR},
    [kVRegLdoA3] = {.type = RPM_REGULATOR},
    [kVRegLdoA5] = {.type = RPM_REGULATOR},
    [kVRegLdoA6] = {.type = RPM_REGULATOR},
    [kVRegLdoA7] = {.type = RPM_REGULATOR},
    [kVRegLdoA8] = {.type = RPM_REGULATOR},
    [kVRegLdoA9] = {.type = RPM_REGULATOR},
    [kVRegLdoA10] = {.type = RPM_REGULATOR},
    [kVRegLdoA11] = {.type = RPM_REGULATOR},
    [kVRegLdoA12] = {.type = RPM_REGULATOR},
    [kVRegLdoA13] = {.type = RPM_REGULATOR},
    [kVRegLdoA16] = {.type = RPM_REGULATOR},
    [kVRegLdoA17] = {.type = RPM_REGULATOR},
    [kVRegLdoA19] = {.type = RPM_REGULATOR},
    [kVRegLdoA22] = {.type = RPM_REGULATOR},
    [kVRegLdoA23] = {.type = RPM_REGULATOR},
    [kPmicCtrlReg] = {.type = PMIC_CTRL_REGISTER},
};

zx_status_t Msm8x53Power::ReadPMICReg(uint32_t reg_addr, uint32_t* reg_value) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53Power::WritePMICReg(uint32_t reg_addr, uint32_t value) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53Power::RpmRegulatorEnable(const Msm8x53PowerDomainInfo* domain) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53Power::RpmRegulatorDisable(const Msm8x53PowerDomainInfo* domain) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53Power::SpmRegulatorEnable(const Msm8x53PowerDomainInfo* domain) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53Power::SpmRegulatorDisable(const Msm8x53PowerDomainInfo* domain) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53Power::PowerImplWritePmicCtrlReg(uint32_t index, uint32_t addr, uint32_t value) {
    if (index != kPmicCtrlReg) {
        return ZX_ERR_INVALID_ARGS;
    }
    return WritePMICReg(addr, value);
}

zx_status_t Msm8x53Power::PowerImplReadPmicCtrlReg(uint32_t index, uint32_t addr, uint32_t* value) {
    if (index != kPmicCtrlReg) {
        return ZX_ERR_INVALID_ARGS;
    }
    return ReadPMICReg(addr, value);
}

zx_status_t Msm8x53Power::PowerImplDisablePowerDomain(uint32_t index) {
    if (index >= fbl::count_of(kMsm8x53PowerDomains)) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    const Msm8x53PowerDomainInfo* domain = &kMsm8x53PowerDomains[index];
    if (domain->type == RPM_REGULATOR) {
        return RpmRegulatorDisable(domain);
    } else if (domain->type == SPM_REGULATOR) {
        return SpmRegulatorDisable(domain);
    }
    return ZX_ERR_INVALID_ARGS;
}

zx_status_t Msm8x53Power::PowerImplEnablePowerDomain(uint32_t index) {
    if (index >= fbl::count_of(kMsm8x53PowerDomains)) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    const Msm8x53PowerDomainInfo* domain = &kMsm8x53PowerDomains[index];
    if (domain->type == RPM_REGULATOR) {
        return RpmRegulatorEnable(domain);
    } else if (domain->type == SPM_REGULATOR) {
        return SpmRegulatorEnable(domain);
    }
    return ZX_ERR_INVALID_ARGS;
}

zx_status_t Msm8x53Power::PowerImplGetPowerDomainStatus(uint32_t index,
                                                        power_domain_status_t* out_status) {
    if (index >= fbl::count_of(kMsm8x53PowerDomains)) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    return ZX_ERR_NOT_SUPPORTED;
}

void Msm8x53Power::DdkRelease() {
    delete this;
}

void Msm8x53Power::DdkUnbind() {
    DdkRemove();
}

zx_status_t Msm8x53Power::Init() {
    //TODO(ravoorir): Check if bootloader did not init the PMIC and
    // do the needful.
    return ZX_OK;
}

zx_status_t Msm8x53Power::Bind() {
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
    status = DdkAdd("msm8x53-power");
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s DdkAdd failed: %d\n", __FUNCTION__, status);
    }

    return status;
}

zx_status_t Msm8x53Power::Create(void* ctx, zx_device_t* parent) {
    zx_status_t status = ZX_OK;

    ddk::PDev pdev(parent);
    if (!pdev.is_valid()) {
        zxlogf(ERROR, "%s Could not get pdev: %d\n", __FUNCTION__, status);
        return ZX_ERR_NO_RESOURCES;
    }

    std::optional<ddk::MmioBuffer> core_mmio;
    status = pdev.MapMmio(kPmicArbCoreMmioIndex, &core_mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s Failed to get core mmio: %d\n", __FUNCTION__, status);
        return status;
    }

    std::optional<ddk::MmioBuffer> chnls_mmio;
    status = pdev.MapMmio(kPmicArbChnlsMmioIndex, &chnls_mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s Failed to get core mmio: %d\n", __FUNCTION__, status);
        return status;
    }

    std::optional<ddk::MmioBuffer> obsvr_mmio;
    status = pdev.MapMmio(kPmicArbObsrvrMmioIndex, &obsvr_mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s Failed to get core mmio: %d\n", __FUNCTION__, status);
        return status;
    }

    std::optional<ddk::MmioBuffer> intr_mmio;
    status = pdev.MapMmio(kPmicArbIntrMmioIndex, &intr_mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s Failed to get core mmio: %d\n", __FUNCTION__, status);
        return status;
    }

    std::optional<ddk::MmioBuffer> cfg_mmio;
    status = pdev.MapMmio(kPmicArbCnfgMmioIndex, &cfg_mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s Failed to get core mmio: %d\n", __FUNCTION__, status);
        return status;
    }

    auto dev = std::make_unique<Msm8x53Power>(parent, pdev, *std::move(core_mmio),
                                              *std::move(chnls_mmio),
                                              *std::move(obsvr_mmio),
                                              *std::move(intr_mmio),
                                              *std::move(cfg_mmio));

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
    driver_ops.bind = Msm8x53Power::Create;
    return driver_ops;
}();

} //namespace power

ZIRCON_DRIVER_BEGIN(mtk_power, power::mtk_power_driver_ops, "zircon", "0.1", 3)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_QUALCOMM),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_QUALCOMM_POWER),
    ZIRCON_DRIVER_END(mtk_power)
