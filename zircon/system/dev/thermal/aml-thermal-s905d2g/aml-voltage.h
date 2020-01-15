// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_THERMAL_AML_THERMAL_S905D2G_AML_VOLTAGE_H_
#define ZIRCON_SYSTEM_DEV_THERMAL_AML_THERMAL_S905D2G_AML_VOLTAGE_H_

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
  zx_status_t Create(zx_device_t* parent, aml_thermal_info_t* thermal_info);
  // For testing
  zx_status_t Init(const pwm_protocol_t* pwm_AO_D, const pwm_protocol_t* pwm_A, uint32_t pid,
                   aml_thermal_info_t* thermal_info);
  zx_status_t Init(aml_thermal_info_t* thermal_info);
  uint32_t GetVoltage(fuchsia_hardware_thermal_PowerDomain power_domain) {
    if (power_domain == fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN) {
      return GetBigClusterVoltage();
    }
    return GetLittleClusterVoltage();
  }

  zx_status_t SetVoltage(fuchsia_hardware_thermal_PowerDomain power_domain, uint32_t microvolt) {
    if (power_domain == fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN) {
      return SetBigClusterVoltage(microvolt);
    }
    return SetLittleClusterVoltage(microvolt);
  }

 private:
  enum {
    COMPONENT_PDEV = 0,
    COMPONENT_PWM_AO_D = 1,
    COMPONENT_PWM_A = 2,
    COMPONENT_COUNT = 3,
  };

  uint32_t GetBigClusterVoltage();
  uint32_t GetLittleClusterVoltage();
  zx_status_t SetClusterVoltage(int* current_voltage_index, const ddk::PwmProtocolClient& pwm,
                                uint32_t microvolt);
  zx_status_t SetBigClusterVoltage(uint32_t microvolt) {
    if (pid_ == PDEV_PID_AMLOGIC_S905D2) {
      // Astro
      return SetClusterVoltage(&current_big_cluster_voltage_index_, pwm_AO_D_, microvolt);
    }
    if (pid_ == PDEV_PID_AMLOGIC_T931) {
      // Sherlock
      return SetClusterVoltage(&current_big_cluster_voltage_index_, pwm_A_, microvolt);
    }
    zxlogf(ERROR, "aml-cpufreq: unsupported SOC PID %u\n", pid_);
    return ZX_ERR_INVALID_ARGS;
  }
  zx_status_t SetLittleClusterVoltage(uint32_t microvolt) {
    return SetClusterVoltage(&current_little_cluster_voltage_index_, pwm_AO_D_, microvolt);
  }

  ddk::PwmProtocolClient pwm_AO_D_;
  ddk::PwmProtocolClient pwm_A_;
  aml_thermal_info_t thermal_info_;
  int current_big_cluster_voltage_index_;
  int current_little_cluster_voltage_index_;
  uint32_t pid_;
};
}  // namespace thermal

#endif  // ZIRCON_SYSTEM_DEV_THERMAL_AML_THERMAL_S905D2G_AML_VOLTAGE_H_
