// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_PWM_AML_PWM_AML_PWM_H_
#define ZIRCON_SYSTEM_DEV_PWM_AML_PWM_AML_PWM_H_

#include <lib/mmio/mmio.h>

#include <array>

#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/protocol/pwm.h>

namespace pwm {

namespace {

// MMIO indices (based on aml-gpio.c gpio_mmios)
enum MmioIdx {
  MMIO_AB = 0,
  MMIO_CD = 1,
  MMIO_EF = 2,
  MMIO_AO_AB = 3,
  MMIO_AO_CD = 4,
  MMIO_COUNT,
};

}  // namespace

class AmlPwmDevice;
using AmlPwmDeviceType = ddk::Device<AmlPwmDevice, ddk::UnbindableNew>;

class AmlPwmDevice : public AmlPwmDeviceType,
                     public ddk::PwmImplProtocol<AmlPwmDevice, ddk::base_protocol> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkUnbindNew(ddk::UnbindTxn txn) {
    ShutDown();
    txn.Reply();
  }
  void DdkRelease() { delete this; }

  // Ddk Mixins.
  zx_status_t PwmImplSetConfig(uint32_t idx, const pwm_config_t* config);
  zx_status_t PwmImplEnable(uint32_t idx);
  zx_status_t PwmImplDisable(uint32_t idx);

 protected:
  // For unit testing
  explicit AmlPwmDevice(ddk::MmioBuffer mmio_ab, ddk::MmioBuffer mmio_cd, ddk::MmioBuffer mmio_ef,
                        ddk::MmioBuffer mmio_ao_ab, ddk::MmioBuffer mmio_ao_cd)
      : AmlPwmDeviceType(nullptr),
        mmios_{std::move(mmio_ab), std::move(mmio_cd), std::move(mmio_ef), std::move(mmio_ao_ab),
               std::move(mmio_ao_cd)} {}

 private:
  explicit AmlPwmDevice(zx_device_t* parent) : AmlPwmDeviceType(parent) {}

  zx_status_t Init(zx_device_t* parent);
  void ShutDown();

  std::array<std::optional<ddk::MmioBuffer>, MMIO_COUNT> mmios_;
};

}  // namespace pwm

#endif  // ZIRCON_SYSTEM_DEV_PWM_AML_PWM_AML_PWM_H_
