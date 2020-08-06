// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_POWER_DRIVERS_AML_MESON_POWER_AML_POWER_H_
#define SRC_DEVICES_POWER_DRIVERS_AML_MESON_POWER_AML_POWER_H_

#include <lib/device-protocol/pdev.h>
#include <threads.h>

#include <array>
#include <optional>
#include <vector>

#include <ddktl/device.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/powerimpl.h>
#include <ddktl/protocol/pwm.h>
#include <soc/aml-common/aml-power.h>
#include <soc/aml-s905d2/s905d2-power.h>
#include <soc/aml-s905d3/s905d3-power.h>
#include <soc/aml-t931/t931-power.h>

namespace power {

class AmlPower;
using AmlPowerType = ddk::Device<AmlPower, ddk::UnbindableNew>;

class AmlPower : public AmlPowerType, public ddk::PowerImplProtocol<AmlPower, ddk::base_protocol> {
 private:
  static constexpr int kInvalidIndex = -1;

 public:
  explicit AmlPower(zx_device_t* parent, std::optional<ddk::PwmProtocolClient> big_cluster_pwm,
                    std::optional<ddk::PwmProtocolClient> little_cluster_pwm,
                    const std::vector<aml_voltage_table_t> voltage_table,
                    voltage_pwm_period_ns_t pwm_period)
      : AmlPowerType(parent),
        big_cluster_pwm_(big_cluster_pwm),
        little_cluster_pwm_(little_cluster_pwm),
        current_big_cluster_voltage_index_(kInvalidIndex),
        current_little_cluster_voltage_index_(kInvalidIndex),
        voltage_table_(voltage_table),
        pwm_period_(pwm_period),
        num_domains_(little_cluster_pwm ? 2 : 1) {}

  AmlPower(const AmlPower&) = delete;
  AmlPower(AmlPower&&) = delete;
  AmlPower& operator=(const AmlPower&) = delete;
  AmlPower& operator=(AmlPower&&) = delete;

  virtual ~AmlPower() = default;
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Device protocol implementation
  void DdkRelease();
  void DdkUnbindNew(ddk::UnbindTxn txn);

  zx_status_t PowerImplGetPowerDomainStatus(uint32_t index, power_domain_status_t* out_status);
  zx_status_t PowerImplEnablePowerDomain(uint32_t index);
  zx_status_t PowerImplDisablePowerDomain(uint32_t index);
  zx_status_t PowerImplGetSupportedVoltageRange(uint32_t index, uint32_t* min_voltage,
                                                uint32_t* max_voltage);
  zx_status_t PowerImplRequestVoltage(uint32_t index, uint32_t voltage, uint32_t* actual_voltage);
  zx_status_t PowerImplGetCurrentVoltage(uint32_t index, uint32_t* current_voltage);
  zx_status_t PowerImplWritePmicCtrlReg(uint32_t index, uint32_t addr, uint32_t value);
  zx_status_t PowerImplReadPmicCtrlReg(uint32_t index, uint32_t addr, uint32_t* value);

  static constexpr uint32_t kBigClusterDomain = 0;
  static constexpr uint32_t kLittleClusterDomain = 1;

  static_assert(kBigClusterDomain == static_cast<uint32_t>(S905d2PowerDomains::kArmCore));
  static_assert(kBigClusterDomain == static_cast<uint32_t>(S905d3PowerDomains::kArmCore));
  static_assert(kBigClusterDomain == static_cast<uint32_t>(T931PowerDomains::kArmCoreBig));
  static_assert(kLittleClusterDomain == static_cast<uint32_t>(T931PowerDomains::kArmCoreLittle));

 private:
  zx_status_t RequestVoltage(const ddk::PwmProtocolClient& pwm, uint32_t u_volts,
                             int* current_voltage_idx);

  std::optional<ddk::PwmProtocolClient> big_cluster_pwm_;
  std::optional<ddk::PwmProtocolClient> little_cluster_pwm_;

  int current_big_cluster_voltage_index_;
  int current_little_cluster_voltage_index_;

  const std::vector<aml_voltage_table_t> voltage_table_;
  const voltage_pwm_period_ns_t pwm_period_;

  const uint32_t num_domains_;
};

}  // namespace power

#endif  // SRC_DEVICES_POWER_DRIVERS_AML_MESON_POWER_AML_POWER_H_
