// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-power.h"

#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <fuchsia/hardware/pwm/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/platform-defs.h>
#include <lib/device-protocol/pdev.h>

#include <algorithm>

#include <fbl/alloc_checker.h>
#include <soc/aml-common/aml-pwm-regs.h>

#include "src/devices/power/drivers/aml-meson-power/aml-meson-power-bind.h"

namespace power {

namespace {

// Sleep for 200 microseconds inorder to let the voltage change
// take effect. Source: Amlogic SDK.
constexpr uint32_t kVoltageSettleTimeUs = 200;
// Step up or down 3 steps in the voltage table while changing
// voltage and not directly. Source: Amlogic SDK
constexpr int kMaxVoltageChangeSteps = 3;

zx_status_t InitPwmProtocolClient(const ddk::PwmProtocolClient& client) {
  if (client.is_valid() == false) {
    zxlogf(ERROR, "%s: failed to get PWM fragment\n", __func__);
    return ZX_ERR_INTERNAL;
  }

  zx_status_t result;
  if ((result = client.Enable()) != ZX_OK) {
    zxlogf(ERROR, "%s: Could not enable PWM", __func__);
  }

  return result;
}

bool IsSortedDescending(const std::vector<aml_voltage_table_t>& vt) {
  for (size_t i = 0; i < vt.size() - 1; i++) {
    if (vt[i].microvolt < vt[i + 1].microvolt)
      // Bail early if we find a voltage that isn't strictly descending.
      return false;
  }
  return true;
}

zx_status_t GetAmlVoltageTable(zx_device_t* parent,
                               std::vector<aml_voltage_table_t>* voltage_table) {
  size_t metadata_size;
  zx_status_t st =
      device_get_metadata_size(parent, DEVICE_METADATA_AML_VOLTAGE_TABLE, &metadata_size);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get Voltage Table size, st = %d", __func__, st);
    return st;
  }

  // The metadata is an array of aml_voltage_table_t so the metadata size must be an
  // integer multiple of sizeof(aml_voltage_table_t).
  if (metadata_size % (sizeof(aml_voltage_table_t)) != 0) {
    zxlogf(
        ERROR,
        "%s: Metadata size [%lu] was not an integer multiple of sizeof(aml_voltage_table_t) [%lu]",
        __func__, metadata_size, sizeof(aml_voltage_table_t));
    return ZX_ERR_INTERNAL;
  }

  const size_t voltage_table_count = metadata_size / sizeof(aml_voltage_table_t);

  auto voltage_table_metadata = std::make_unique<aml_voltage_table_t[]>(voltage_table_count);

  size_t actual;
  st = device_get_metadata(parent, DEVICE_METADATA_AML_VOLTAGE_TABLE, voltage_table_metadata.get(),
                           metadata_size, &actual);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get Voltage Table, st = %d", __func__, st);
    return st;
  }
  if (actual != metadata_size) {
    zxlogf(ERROR, "%s: device_get_metadata expected to read %lu bytes, actual read %lu", __func__,
           metadata_size, actual);
    return ZX_ERR_INTERNAL;
  }

  voltage_table->reserve(voltage_table_count);
  std::copy(&voltage_table_metadata[0], &voltage_table_metadata[0] + voltage_table_count,
            std::back_inserter(*voltage_table));

  if (!IsSortedDescending(*voltage_table)) {
    zxlogf(ERROR, "%s: Voltage table was not sorted in strictly descending order", __func__);
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

zx_status_t GetAmlPwmPeriod(zx_device_t* parent, voltage_pwm_period_ns_t* result) {
  size_t metadata_size;
  zx_status_t st =
      device_get_metadata_size(parent, DEVICE_METADATA_AML_PWM_PERIOD_NS, &metadata_size);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get PWM Period Metadata, st = %d", __func__, st);
    return st;
  }

  if (metadata_size != sizeof(*result)) {
    zxlogf(ERROR, "%s: Expected PWM Period metadata to be %lu bytes, got %lu", __func__,
           sizeof(*result), metadata_size);
    return ZX_ERR_INTERNAL;
  }

  size_t actual;
  st = device_get_metadata(parent, DEVICE_METADATA_AML_PWM_PERIOD_NS, result, sizeof(*result),
                           &actual);

  if (actual != sizeof(*result)) {
    zxlogf(ERROR, "%s: Expected PWM metadata size = %lu, got %lu", __func__, sizeof(*result),
           actual);
  }

  return st;
}

}  // namespace

zx_status_t AmlPower::PowerImplWritePmicCtrlReg(uint32_t index, uint32_t addr, uint32_t value) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AmlPower::PowerImplReadPmicCtrlReg(uint32_t index, uint32_t addr, uint32_t* value) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AmlPower::PowerImplDisablePowerDomain(uint32_t index) {
  if (index >= num_domains_) {
    zxlogf(ERROR, "%s: Requested Disable for a domain that doesn't exist, idx = %u", __func__,
           index);
    return ZX_ERR_OUT_OF_RANGE;
  }

  return ZX_OK;
}

zx_status_t AmlPower::PowerImplEnablePowerDomain(uint32_t index) {
  if (index >= num_domains_) {
    zxlogf(ERROR, "%s: Requested Enable for a domain that doesn't exist, idx = %u", __func__,
           index);
    return ZX_ERR_OUT_OF_RANGE;
  }

  return ZX_OK;
}

zx_status_t AmlPower::PowerImplGetPowerDomainStatus(uint32_t index,
                                                    power_domain_status_t* out_status) {
  if (index >= num_domains_) {
    zxlogf(ERROR,
           "%s: Requested PowerImplGetPowerDomainStatus for a domain that doesn't exist, idx = %u",
           __func__, index);
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (out_status == nullptr) {
    zxlogf(ERROR, "%s: out_status must not be null", __func__);
    return ZX_ERR_INVALID_ARGS;
  }

  // All domains are always enabled.
  *out_status = POWER_DOMAIN_STATUS_ENABLED;
  return ZX_OK;
}

zx_status_t AmlPower::PowerImplGetSupportedVoltageRange(uint32_t index, uint32_t* min_voltage,
                                                        uint32_t* max_voltage) {
  if (index >= num_domains_) {
    zxlogf(ERROR,
           "%s: Requested GetSupportedVoltageRange for a domain that doesn't exist, idx = %u",
           __func__, index);
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (big_cluster_vreg_ && index == kBigClusterDomain) {
    vreg_params_t params;
    big_cluster_vreg_->GetRegulatorParams(&params);

    *min_voltage = params.min_uv;
    *max_voltage = params.min_uv + params.num_steps * params.step_size_uv;

    zxlogf(DEBUG, "%s: Getting Big Cluster VReg Range max = %u, min = %u", __func__, *max_voltage,
           *min_voltage);

    return ZX_OK;
  }

  // Voltage table is sorted in descending order so the minimum voltage is the last element and the
  // maximum voltage is the first element.
  *min_voltage = voltage_table_.back().microvolt;
  *max_voltage = voltage_table_.front().microvolt;

  return ZX_OK;
}

zx_status_t AmlPower::RequestVoltage(const ddk::PwmProtocolClient& pwm, uint32_t u_volts,
                                     int* current_voltage_index) {
  // Find the largest voltage that does not exceed u_volts.
  const aml_voltage_table_t target_voltage = {.microvolt = u_volts, .duty_cycle = 0};

  const auto& target =
      std::lower_bound(voltage_table_.begin(), voltage_table_.end(), target_voltage,
                       [](const aml_voltage_table_t& l, const aml_voltage_table_t& r) {
                         return l.microvolt > r.microvolt;
                       });

  if (target == voltage_table_.end()) {
    zxlogf(ERROR, "%s: Could not find a voltage less than or equal to %u\n", __func__, u_volts);
    return ZX_ERR_NOT_SUPPORTED;
  }

  size_t target_idx = target - voltage_table_.begin();
  if (target_idx >= INT_MAX || target_idx >= voltage_table_.size()) {
    zxlogf(ERROR, "%s: voltage target index out of bounds", __func__);
    return ZX_ERR_OUT_OF_RANGE;
  }
  int target_index = static_cast<int>(target_idx);

  zx_status_t status = ZX_OK;
  // If this is the first time we are setting up the voltage
  // we directly set it.
  if (*current_voltage_index == kInvalidIndex) {
    // Update new duty cycle.
    aml_pwm::mode_config on = {aml_pwm::ON, {}};
    pwm_config_t cfg = {false, pwm_period_, static_cast<float>(target->duty_cycle),
                        reinterpret_cast<uint8_t*>(&on), sizeof(on)};
    if ((status = pwm.SetConfig(&cfg)) != ZX_OK) {
      zxlogf(ERROR, "%s: Could not initialize PWM", __func__);
      return status;
    }
    usleep(kVoltageSettleTimeUs);
    *current_voltage_index = target_index;
    return ZX_OK;
  }

  // Otherwise we adjust to the target voltage step by step.
  while (*current_voltage_index != target_index) {
    if (*current_voltage_index < target_index) {
      if (*current_voltage_index < target_index - kMaxVoltageChangeSteps) {
        // Step up by 3 in the voltage table.
        *current_voltage_index += kMaxVoltageChangeSteps;
      } else {
        *current_voltage_index = target_index;
      }
    } else {
      if (*current_voltage_index > target_index + kMaxVoltageChangeSteps) {
        // Step down by 3 in the voltage table.
        *current_voltage_index -= kMaxVoltageChangeSteps;
      } else {
        *current_voltage_index = target_index;
      }
    }
    // Update new duty cycle.
    aml_pwm::mode_config on = {aml_pwm::ON, {}};
    pwm_config_t cfg = {false, pwm_period_,
                        static_cast<float>(voltage_table_[*current_voltage_index].duty_cycle),
                        reinterpret_cast<uint8_t*>(&on), sizeof(on)};
    if ((status = pwm.SetConfig(&cfg)) != ZX_OK) {
      zxlogf(ERROR, "%s: Could not initialize PWM", __func__);
      return status;
    }
    usleep(kVoltageSettleTimeUs);
  }
  return ZX_OK;
}

zx_status_t AmlPower::SetBigClusterVoltage(uint32_t voltage, uint32_t* actual_voltage) {
  if (big_cluster_pwm_) {
    zx_status_t st =
        RequestVoltage(big_cluster_pwm_.value(), voltage, &current_big_cluster_voltage_index_);
    if (st == ZX_OK) {
      *actual_voltage = voltage_table_[current_big_cluster_voltage_index_].microvolt;
    }
    return st;
  } else if (big_cluster_vreg_) {
    vreg_params_t params;
    big_cluster_vreg_->GetRegulatorParams(&params);
    const uint32_t min_voltage_uv = params.min_uv;
    const uint32_t max_voltage_uv = params.min_uv + (params.step_size_uv * params.num_steps);
    // Find the step value that achieves the requested voltage.
    if (voltage < min_voltage_uv || voltage > max_voltage_uv) {
      zxlogf(ERROR, "%s: Voltage must be between %u and %u microvolts", __func__, min_voltage_uv,
             max_voltage_uv);
      return ZX_ERR_NOT_SUPPORTED;
    }

    uint32_t target_step = (voltage - min_voltage_uv) / params.step_size_uv;
    ZX_ASSERT(target_step <= params.num_steps);

    zx_status_t st = big_cluster_vreg_->SetVoltageStep(target_step);
    if (st == ZX_OK) {
      *actual_voltage = min_voltage_uv + target_step * params.step_size_uv;
    }

    return st;
  } else {
    return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t AmlPower::PowerImplRequestVoltage(uint32_t index, uint32_t voltage,
                                              uint32_t* actual_voltage) {
  if (index >= num_domains_) {
    zxlogf(ERROR, "%s: Requested voltage for a range that doesn't exist, idx = %u", __func__,
           index);
    return ZX_ERR_OUT_OF_RANGE;
  }

  zx_status_t st = ZX_ERR_OUT_OF_RANGE;
  if (index == kBigClusterDomain) {
    return SetBigClusterVoltage(voltage, actual_voltage);
  } else if (index == kLittleClusterDomain) {
    st = RequestVoltage(little_cluster_pwm_.value(), voltage,
                        &current_little_cluster_voltage_index_);
    if (st == ZX_OK) {
      *actual_voltage = voltage_table_[current_little_cluster_voltage_index_].microvolt;
    }
  }

  return st;
}

zx_status_t AmlPower::PowerImplGetCurrentVoltage(uint32_t index, uint32_t* current_voltage) {
  if (index >= num_domains_) {
    zxlogf(ERROR, "%s: Requested voltage for a range that doesn't exist, idx = %u", __func__,
           index);
    return ZX_ERR_OUT_OF_RANGE;
  }

  switch (index) {
    case kBigClusterDomain:
      if (current_big_cluster_voltage_index_ == kInvalidIndex)
        return ZX_ERR_BAD_STATE;
      *current_voltage = voltage_table_[current_big_cluster_voltage_index_].microvolt;
      break;
    case kLittleClusterDomain:
      if (current_little_cluster_voltage_index_ == kInvalidIndex)
        return ZX_ERR_BAD_STATE;
      *current_voltage = voltage_table_[current_little_cluster_voltage_index_].microvolt;
      break;
    default:
      return ZX_ERR_OUT_OF_RANGE;
  }

  return ZX_OK;
}

void AmlPower::DdkRelease() { delete this; }

void AmlPower::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

zx_status_t AmlPower::Create(void* ctx, zx_device_t* parent) {
  zx_status_t st;
  auto pdev = ddk::PDev::FromFragment(parent);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "%s: failed to get pdev protocol", __func__);
    return ZX_ERR_INTERNAL;
  }

  pdev_device_info_t device_info;
  st = pdev.GetDeviceInfo(&device_info);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: failed to get DeviceInfo, st = %d", __func__, st);
    return st;
  }

  std::vector<aml_voltage_table_t> voltage_table;
  st = GetAmlVoltageTable(parent, &voltage_table);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get aml voltage table, st = %d", __func__, st);
    return st;
  }

  voltage_pwm_period_ns_t pwm_period;
  st = GetAmlPwmPeriod(parent, &pwm_period);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get aml pwm period table, st = %d", __func__, st);
    return st;
  }

  ddk::PwmProtocolClient first_cluster_pwm(parent, "pwm-ao-d");
  st = InitPwmProtocolClient(first_cluster_pwm);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to initialize Big Cluster PWM Client, st = %d", __func__, st);
    return st;
  }

  std::optional<ddk::PwmProtocolClient> second_cluster_pwm = std::nullopt;
  std::optional<ddk::VregProtocolClient> second_cluster_vreg = std::nullopt;
  if (device_info.pid == PDEV_PID_LUIS) {
    ddk::VregProtocolClient client(parent, "vreg-pp1000-cpu-a");
    if (!client.is_valid()) {
      zxlogf(ERROR, "%s: failed to get vreg fragment\n", __func__);
      return ZX_ERR_INTERNAL;
    }

    second_cluster_vreg = client;
  } else if (device_info.pid == PDEV_PID_SHERLOCK) {
    ddk::PwmProtocolClient client(parent, "pwm-a");
    st = InitPwmProtocolClient(client);
    if (st != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to initialize Little Cluster PWM Client, st = %d", __func__, st);
      return st;
    }
    second_cluster_pwm = client;
  }

  std::unique_ptr<AmlPower> power_impl_device;

  switch (device_info.pid) {
    case PDEV_PID_ASTRO:
      power_impl_device.reset(
          new AmlPower(parent, std::move(first_cluster_pwm), std::move(voltage_table), pwm_period));
      break;
    case PDEV_PID_LUIS:
      power_impl_device.reset(new AmlPower(parent, std::move(*second_cluster_vreg),
                                           std::move(first_cluster_pwm), std::move(voltage_table),
                                           pwm_period));
      break;
    case PDEV_PID_SHERLOCK:
      power_impl_device.reset(new AmlPower(parent, std::move(*second_cluster_pwm),
                                           std::move(first_cluster_pwm), std::move(voltage_table),
                                           pwm_period));
      break;
    default:
      zxlogf(ERROR, "Unsupported device pid = %u", device_info.pid);
      return ZX_ERR_NOT_SUPPORTED;
  }

  st = power_impl_device->DdkAdd("power-impl", DEVICE_ADD_ALLOW_MULTI_COMPOSITE);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed, st = %d", __func__, st);
  }

  // Let device runner take ownership of this object.
  __UNUSED auto* dummy = power_impl_device.release();

  return st;
}

static constexpr zx_driver_ops_t aml_power_driver_ops = []() {
  zx_driver_ops_t driver_ops = {};
  driver_ops.version = DRIVER_OPS_VERSION;
  driver_ops.bind = AmlPower::Create;
  // driver_ops.run_unit_tests = run_test;  # TODO(gkalsi).
  return driver_ops;
}();

}  // namespace power

ZIRCON_DRIVER(aml_power, power::aml_power_driver_ops, "zircon", "0.1");
