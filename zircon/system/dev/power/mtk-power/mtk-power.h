// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/powerimpl.h>
#include <lib/mmio/mmio.h>
#include <threads.h>

namespace power {

class MtkPower;
using MtkPowerType = ddk::Device<MtkPower, ddk::Unbindable>;

class MtkPower : public MtkPowerType,
                 public ddk::PowerImplProtocol<MtkPower, ddk::base_protocol> {
public:
    explicit MtkPower(zx_device_t* parent,
                      const ddk::PDev& pdev,
                      ddk::MmioBuffer mmio)
        : MtkPowerType(parent), pdev_(pdev), pmic_mmio_(std::move(mmio)) {}

    ~MtkPower() = default;
    static zx_status_t Create(void* ctx, zx_device_t* parent);

    // Device protocol implementation
    void DdkRelease();
    void DdkUnbind();

    zx_status_t PowerImplGetPowerDomainStatus(uint32_t index, power_domain_status_t* out_status);
    zx_status_t PowerImplEnablePowerDomain(uint32_t index);
    zx_status_t PowerImplDisablePowerDomain(uint32_t index);
    zx_status_t PowerImplGetSupportedVoltageRange(uint32_t index, uint32_t* min_voltage,
                                                  uint32_t* max_voltage);
    zx_status_t PowerImplRequestVoltage(uint32_t index, uint32_t voltage, uint32_t* actual_voltage);
    zx_status_t PowerImplWritePmicCtrlReg(uint32_t index, uint32_t addr, uint32_t value);
    zx_status_t PowerImplReadPmicCtrlReg(uint32_t index, uint32_t addr, uint32_t* value);

private:
    ddk::PDev pdev_;
    ddk::MmioBuffer pmic_mmio_;

    zx_status_t Bind();
    zx_status_t Init();
    zx_status_t Test(uint32_t index);
    void WaitForIdle();
    void WaitForValidClr();
    zx_status_t ReadPMICReg(uint32_t reg_addr, uint32_t* reg_value);
    zx_status_t WritePMICReg(uint32_t reg_addr, uint32_t value);
};

} //namespace power
