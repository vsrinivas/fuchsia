// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_POWER_DRIVERS_AML_MESON_POWER_AML_POWER_H_
#define SRC_DEVICES_POWER_DRIVERS_AML_MESON_POWER_AML_POWER_H_

#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <fuchsia/hardware/powerimpl/cpp/banjo.h>
#include <fuchsia/hardware/pwm/cpp/banjo.h>
#include <fuchsia/hardware/vreg/cpp/banjo.h>
#include <lib/device-protocol/pdev.h>
#include <lib/zx/status.h>
#include <threads.h>

#include <array>
#include <optional>
#include <vector>

#include <ddktl/device.h>
#include <soc/aml-a311d/a311d-power.h>
#include <soc/aml-common/aml-power.h>
#include <soc/aml-s905d2/s905d2-power.h>
#include <soc/aml-s905d3/s905d3-power.h>
#include <soc/aml-t931/t931-power.h>

namespace power {

class AmlPower;
using AmlPowerType = ddk::Device<AmlPower, ddk::Unbindable>;

class AmlPower : public AmlPowerType, public ddk::PowerImplProtocol<AmlPower, ddk::base_protocol> {
 private:
  static constexpr int kInvalidIndex = -1;

 public:
  // Constructor for Astro.
  AmlPower(zx_device_t* parent, ddk::PwmProtocolClient big_cluster_pwm,
           const std::vector<aml_voltage_table_t> voltage_table, voltage_pwm_period_ns_t pwm_period)
      : AmlPowerType(parent),
        big_cluster_pwm_(big_cluster_pwm),
        current_big_cluster_voltage_index_(kInvalidIndex),
        current_little_cluster_voltage_index_(kInvalidIndex),
        voltage_table_(voltage_table),
        pwm_period_(pwm_period),
        num_domains_(1) {}

  // Constructor for Sherlock.
  AmlPower(zx_device_t* parent, ddk::PwmProtocolClient big_cluster_pwm,
           ddk::PwmProtocolClient little_cluster_pwm,
           const std::vector<aml_voltage_table_t> voltage_table, voltage_pwm_period_ns_t pwm_period)
      : AmlPowerType(parent),
        big_cluster_pwm_(big_cluster_pwm),
        little_cluster_pwm_(little_cluster_pwm),
        current_big_cluster_voltage_index_(kInvalidIndex),
        current_little_cluster_voltage_index_(kInvalidIndex),
        voltage_table_(voltage_table),
        pwm_period_(pwm_period),
        num_domains_(2) {}

  AmlPower(zx_device_t* parent, ddk::VregProtocolClient big_cluster_vreg,
           ddk::PwmProtocolClient little_cluster_pwm,
           const std::vector<aml_voltage_table_t> voltage_table, voltage_pwm_period_ns_t pwm_period)
      : AmlPowerType(parent),
        big_cluster_vreg_(big_cluster_vreg),
        little_cluster_pwm_(little_cluster_pwm),
        current_big_cluster_voltage_index_(kInvalidIndex),
        current_little_cluster_voltage_index_(kInvalidIndex),
        voltage_table_(voltage_table),
        pwm_period_(pwm_period),
        num_domains_(2) {}

  // Constructor for Vim3.
  AmlPower(zx_device_t* parent, ddk::VregProtocolClient big_cluster_vreg,
           ddk::VregProtocolClient little_cluster_vreg)
      : AmlPowerType(parent),
        big_cluster_vreg_(big_cluster_vreg),
        little_cluster_vreg_(little_cluster_vreg),
        current_big_cluster_voltage_index_(kInvalidIndex),
        current_little_cluster_voltage_index_(kInvalidIndex),
        voltage_table_({}),  // not used
        pwm_period_(0),      // not used
        num_domains_(2) {}

  AmlPower(const AmlPower&) = delete;
  AmlPower(AmlPower&&) = delete;
  AmlPower& operator=(const AmlPower&) = delete;
  AmlPower& operator=(AmlPower&&) = delete;

  virtual ~AmlPower() = default;
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Device protocol implementation
  void DdkRelease();
  void DdkUnbind(ddk::UnbindTxn txn);

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
  static_assert(kBigClusterDomain == static_cast<uint32_t>(A311dPowerDomains::kArmCoreBig));
  static_assert(kLittleClusterDomain == static_cast<uint32_t>(A311dPowerDomains::kArmCoreLittle));

 private:
  struct ClusterArgs {
    ddk::PwmProtocolClient pwm;
    ddk::VregProtocolClient vreg;
    int* current_voltage_index = nullptr;
  };
  zx::status<ClusterArgs> GetClusterArgs(uint32_t cluster_index);

  zx_status_t GetTargetIndex(const ddk::PwmProtocolClient& pwm, uint32_t u_volts,
                             uint32_t* target_index);
  zx_status_t GetTargetIndex(const ddk::VregProtocolClient& vreg, uint32_t u_volts,
                             uint32_t* target_index);
  zx_status_t Update(const ddk::PwmProtocolClient& pwm, uint32_t idx);
  zx_status_t Update(const ddk::VregProtocolClient& vreg, uint32_t idx);

  template <class ProtocolClient>
  zx_status_t RequestVoltage(const ProtocolClient& pwm, uint32_t u_volts,
                             int* current_voltage_index);

  ddk::PwmProtocolClient big_cluster_pwm_;
  ddk::VregProtocolClient big_cluster_vreg_;
  ddk::PwmProtocolClient little_cluster_pwm_;
  ddk::VregProtocolClient little_cluster_vreg_;

  int current_big_cluster_voltage_index_;
  int current_little_cluster_voltage_index_;

  const std::vector<aml_voltage_table_t> voltage_table_;
  const voltage_pwm_period_ns_t pwm_period_;

  const uint32_t num_domains_;
};

}  // namespace power

#endif  // SRC_DEVICES_POWER_DRIVERS_AML_MESON_POWER_AML_POWER_H_
