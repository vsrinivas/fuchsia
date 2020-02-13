// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_THERMAL_DRIVERS_AML_THERMAL_S905D2G_AML_VOLTAGE_H_
#define SRC_DEVICES_THERMAL_DRIVERS_AML_THERMAL_S905D2G_AML_VOLTAGE_H_

#include <lib/mmio/mmio.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>

#include <ddk/platform-defs.h>
#include <ddk/protocol/scpi.h>
#include <ddktl/protocol/composite.h>
#include <ddktl/protocol/pwm.h>
#include <soc/aml-common/aml-pwm-regs.h>
#include <soc/aml-common/aml-thermal.h>

namespace thermal {
// This class represents a voltage regulator
// on the Amlogic board which provides interface
// to set and get current voltage for the CPU.
class AmlVoltageRegulator {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AmlVoltageRegulator);
  AmlVoltageRegulator() = default;
  zx_status_t Create(zx_device_t* parent,
                     const fuchsia_hardware_thermal_ThermalDeviceInfo& thermal_config,
                     const aml_thermal_info_t* thermal_info);
  // For testing
  zx_status_t Init(const pwm_protocol_t* big_cluster_pwm, const pwm_protocol_t* little_cluster_pwm,
                   const fuchsia_hardware_thermal_ThermalDeviceInfo& thermal_config,
                   const aml_thermal_info_t* thermal_info);
  zx_status_t Init(const fuchsia_hardware_thermal_ThermalDeviceInfo& thermal_config,
                   const aml_thermal_info_t* thermal_info);
  uint32_t GetVoltage(fuchsia_hardware_thermal_PowerDomain power_domain) {
    if (power_domain == fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN) {
      return thermal_info_.voltage_table[current_big_cluster_voltage_index_].microvolt;
    }
    return thermal_info_.voltage_table[current_little_cluster_voltage_index_].microvolt;
  }

  zx_status_t SetVoltage(fuchsia_hardware_thermal_PowerDomain power_domain, uint32_t microvolt) {
    if (power_domain == fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN) {
      return SetClusterVoltage(&current_big_cluster_voltage_index_, big_cluster_pwm_, microvolt);
    }
    return SetLittleClusterVoltage(microvolt);
  }

 private:
  enum {
    COMPONENT_PDEV = 0,
    COMPONENT_PWM_BIG_CLUSTER = 1,
    COMPONENT_PWM_LITTLE_CLUSTER = 2,
    COMPONENT_COUNT = 3,
  };

  zx_status_t SetClusterVoltage(int* current_voltage_index, const ddk::PwmProtocolClient& pwm,
                                uint32_t microvolt);
  zx_status_t SetBigClusterVoltage(uint32_t microvolt) {
    return SetClusterVoltage(&current_big_cluster_voltage_index_, big_cluster_pwm_, microvolt);
  }
  zx_status_t SetLittleClusterVoltage(uint32_t microvolt) {
    return SetClusterVoltage(&current_little_cluster_voltage_index_, little_cluster_pwm_,
                             microvolt);
  }

  ddk::PwmProtocolClient big_cluster_pwm_;
  ddk::PwmProtocolClient little_cluster_pwm_;
  aml_thermal_info_t thermal_info_;
  int current_big_cluster_voltage_index_;
  int current_little_cluster_voltage_index_;
  bool big_little_ = false;
};
}  // namespace thermal

#endif  // SRC_DEVICES_THERMAL_DRIVERS_AML_THERMAL_S905D2G_AML_VOLTAGE_H_
