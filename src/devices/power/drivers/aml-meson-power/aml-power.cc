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
    // Optional fragment. See comment in AmlPower::Create.
    return ZX_OK;
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
  if (!voltage_table) {
    zxlogf(ERROR, "Need voltage table pointer");
    return ZX_ERR_INVALID_ARGS;
  }

  size_t metadata_size;
  zx_status_t st =
      device_get_metadata_size(parent, DEVICE_METADATA_AML_VOLTAGE_TABLE, &metadata_size);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get Voltage Table size, st = %d", __func__, st);
    return ZX_ERR_NOT_FOUND;
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
    return ZX_ERR_NOT_FOUND;
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
  if (!result) {
    zxlogf(ERROR, "Need result pointer");
    return ZX_ERR_INVALID_ARGS;
  }

  voltage_pwm_period_ns_t period;
  size_t metadata_size;
  zx_status_t st =
      device_get_metadata_size(parent, DEVICE_METADATA_AML_PWM_PERIOD_NS, &metadata_size);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get PWM Period Metadata size, st = %d", __func__, st);
    return ZX_ERR_NOT_FOUND;
  }

  if (metadata_size != sizeof(period)) {
    zxlogf(ERROR, "%s: Expected PWM Period metadata to be %lu bytes, got %lu", __func__,
           sizeof(period), metadata_size);
    return ZX_ERR_INTERNAL;
  }

  size_t actual;
  st = device_get_metadata(parent, DEVICE_METADATA_AML_PWM_PERIOD_NS, &period, sizeof(period),
                           &actual);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get PWM Period Metadata, st = %d", __func__, st);
    return ZX_ERR_NOT_FOUND;
  }
  if (actual != sizeof(period)) {
    zxlogf(ERROR, "%s: Expected PWM metadata size = %lu, got %lu", __func__, sizeof(period),
           actual);
    return ZX_ERR_INTERNAL;
  }
  *result = period;

  return ZX_OK;
}

uint32_t CalculateVregVoltage(const vreg_params_t params, const uint32_t idx) {
  return params.min_uv + idx * params.step_size_uv;
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
  if (!min_voltage || !max_voltage) {
    zxlogf(ERROR, "Need non-nullptr for min_voltage and max_voltage");
    return ZX_ERR_INVALID_ARGS;
  }

  if (index >= num_domains_) {
    zxlogf(ERROR,
           "%s: Requested GetSupportedVoltageRange for a domain that doesn't exist, idx = %u",
           __func__, index);
    return ZX_ERR_OUT_OF_RANGE;
  }

  auto result = GetClusterArgs(index);
  if (result.is_error() || !result.value().current_voltage_index) {
    zxlogf(ERROR, "%s: Could not get Cluster args", __func__);
    return result.status_value();
  }
  auto args = result.value();

  if (args.vreg.is_valid()) {
    vreg_params_t params;
    args.vreg.GetRegulatorParams(&params);
    *min_voltage = CalculateVregVoltage(params, 0);
    *max_voltage = CalculateVregVoltage(params, params.num_steps);
    zxlogf(DEBUG, "%s: Getting %s Cluster VReg Range max = %u, min = %u", __func__,
           index ? "Little" : "Big", *max_voltage, *min_voltage);

    return ZX_OK;
  }
  if (args.pwm.is_valid()) {
    // Voltage table is sorted in descending order so the minimum voltage is the last element and
    // the maximum voltage is the first element.
    *min_voltage = voltage_table_.back().microvolt;
    *max_voltage = voltage_table_.front().microvolt;
    zxlogf(DEBUG, "%s: Getting %s Cluster VReg Range max = %u, min = %u", __func__,
           index ? "Little" : "Big", *max_voltage, *min_voltage);

    return ZX_OK;
  }

  zxlogf(ERROR,
         "%s: Neither Vreg nor PWM are supported for this cluster. This should never happen.",
         __func__);
  return ZX_ERR_INTERNAL;
}

zx::status<AmlPower::ClusterArgs> AmlPower::GetClusterArgs(uint32_t cluster_index) {
  if (cluster_index >= num_domains_) {
    zxlogf(ERROR, "%s: Requested for a domain that doesn't exist, idx = %u", __func__,
           cluster_index);
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  switch (cluster_index) {
    case kBigClusterDomain:
      return zx::ok(ClusterArgs{
          .pwm = big_cluster_pwm_,
          .vreg = big_cluster_vreg_,
          .current_voltage_index = &current_big_cluster_voltage_index_,
      });
    case kLittleClusterDomain:
      return zx::ok(ClusterArgs{
          .pwm = little_cluster_pwm_,
          .vreg = little_cluster_vreg_,
          .current_voltage_index = &current_little_cluster_voltage_index_,
      });
    default:
      zxlogf(ERROR, "%s: Only supports Big and Little Cluster. Do not recognize %d cluster index",
             __func__, cluster_index);
      return zx::error(ZX_ERR_INVALID_ARGS);
  }
}

zx_status_t AmlPower::GetTargetIndex(const ddk::PwmProtocolClient& pwm, uint32_t u_volts,
                                     uint32_t* target_index) {
  if (!target_index) {
    return ZX_ERR_INTERNAL;
  }

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
  *target_index = static_cast<int>(target_idx);

  return ZX_OK;
}

zx_status_t AmlPower::GetTargetIndex(const ddk::VregProtocolClient& vreg, uint32_t u_volts,
                                     uint32_t* target_index) {
  if (!target_index) {
    return ZX_ERR_INTERNAL;
  }

  vreg_params_t params;
  vreg.GetRegulatorParams(&params);
  const auto min_voltage_uv = CalculateVregVoltage(params, 0);
  const auto max_voltage_uv = CalculateVregVoltage(params, params.num_steps);
  // Find the step value that achieves the requested voltage.
  if (u_volts < min_voltage_uv || u_volts > max_voltage_uv) {
    zxlogf(ERROR, "%s: Voltage must be between %u and %u microvolts", __func__, min_voltage_uv,
           max_voltage_uv);
    return ZX_ERR_NOT_SUPPORTED;
  }

  *target_index = (u_volts - min_voltage_uv) / params.step_size_uv;
  ZX_ASSERT(*target_index <= params.num_steps);

  return ZX_OK;
}

zx_status_t AmlPower::Update(const ddk::PwmProtocolClient& pwm, uint32_t idx) {
  aml_pwm::mode_config on = {aml_pwm::ON, {}};
  pwm_config_t cfg = {false, pwm_period_, static_cast<float>(voltage_table_[idx].duty_cycle),
                      reinterpret_cast<uint8_t*>(&on), sizeof(on)};
  return pwm.SetConfig(&cfg);
}

zx_status_t AmlPower::Update(const ddk::VregProtocolClient& vreg, uint32_t idx) {
  return vreg.SetVoltageStep(idx);
}

template <class ProtocolClient>
zx_status_t AmlPower::RequestVoltage(const ProtocolClient& client, uint32_t u_volts,
                                     int* current_voltage_index) {
  if (!current_voltage_index) {
    zxlogf(ERROR, "Need current voltage index");
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t target_idx;
  auto status = GetTargetIndex(client, u_volts, &target_idx);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not get target index\n");
    return status;
  }

  // If this is the first time we are setting up the voltage
  // we directly set it.
  if (*current_voltage_index == kInvalidIndex) {
    status = Update(client, target_idx);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: Could not update", __func__);
      return status;
    }
    usleep(kVoltageSettleTimeUs);
    *current_voltage_index = target_idx;
    return ZX_OK;
  }

  // Otherwise we adjust to the target voltage step by step.
  auto target_index = static_cast<int>(target_idx);
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
    status = Update(client, *current_voltage_index);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: Could not update", __func__);
      return status;
    }
    usleep(kVoltageSettleTimeUs);
  }
  return ZX_OK;
}

zx_status_t AmlPower::PowerImplRequestVoltage(uint32_t index, uint32_t voltage,
                                              uint32_t* actual_voltage) {
  if (index >= num_domains_) {
    zxlogf(ERROR, "%s: Requested voltage for a range that doesn't exist, idx = %u", __func__,
           index);
    return ZX_ERR_OUT_OF_RANGE;
  }

  auto result = GetClusterArgs(index);
  if (result.is_error() || !result.value().current_voltage_index) {
    zxlogf(ERROR, "%s: Could not get Cluster args", __func__);
    return result.status_value();
  }
  auto args = result.value();

  if (args.pwm.is_valid()) {
    zx_status_t st = RequestVoltage(args.pwm, voltage, args.current_voltage_index);
    if ((st == ZX_OK) && actual_voltage) {
      *actual_voltage = voltage_table_[*args.current_voltage_index].microvolt;
    }
    return st;
  }
  if (args.vreg.is_valid()) {
    zx_status_t st = RequestVoltage(args.vreg, voltage, args.current_voltage_index);
    if ((st == ZX_OK) && actual_voltage) {
      vreg_params_t params;
      args.vreg.GetRegulatorParams(&params);
      *actual_voltage = CalculateVregVoltage(params, *args.current_voltage_index);
    }
    return st;
  }

  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AmlPower::PowerImplGetCurrentVoltage(uint32_t index, uint32_t* current_voltage) {
  if (!current_voltage) {
    zxlogf(ERROR, "Cannot take nullptr for current_voltage");
    return ZX_ERR_INVALID_ARGS;
  }

  if (index >= num_domains_) {
    zxlogf(ERROR, "%s: Requested voltage for a range that doesn't exist, idx = %u", __func__,
           index);
    return ZX_ERR_OUT_OF_RANGE;
  }

  auto result = GetClusterArgs(index);
  if (result.is_error() || !result.value().current_voltage_index) {
    zxlogf(ERROR, "%s: Could not get Cluster args", __func__);
    return result.status_value();
  }
  auto args = result.value();

  if (args.pwm.is_valid()) {
    if (*(args.current_voltage_index) == kInvalidIndex)
      return ZX_ERR_BAD_STATE;
    *current_voltage = voltage_table_[*(args.current_voltage_index)].microvolt;
  } else if (args.vreg.is_valid()) {
    vreg_params_t params;
    args.vreg.GetRegulatorParams(&params);
    *current_voltage = CalculateVregVoltage(params, *(args.current_voltage_index));
  } else {
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

void AmlPower::DdkRelease() { delete this; }

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

  // Create tries to get all possible metadata and fragments. However, each board (based on
  // device_info.pid) requires different combinations of metadata and fragments. First, Create tries
  // to get and initialize all metadata and fragments possible. Then, before creating AmlPower,
  // Create checks whether the metadata and fragments needed by the board are available and fails if
  // they aren't.
  std::vector<aml_voltage_table_t> tmp_voltage_table;
  st = GetAmlVoltageTable(parent, &tmp_voltage_table);
  std::optional<std::vector<aml_voltage_table_t>> voltage_table = std::nullopt;
  if (st == ZX_OK) {
    voltage_table.emplace(tmp_voltage_table);
  } else if (st != ZX_ERR_NOT_FOUND) {
    zxlogf(ERROR, "%s: Failed to get aml voltage table, st = %d", __func__, st);
    return st;
  }

  voltage_pwm_period_ns_t tmp_pwm_period;
  st = GetAmlPwmPeriod(parent, &tmp_pwm_period);
  std::optional<voltage_pwm_period_ns_t> pwm_period = std::nullopt;
  if (st == ZX_OK) {
    pwm_period.emplace(tmp_pwm_period);
  } else if (st != ZX_ERR_NOT_FOUND) {
    zxlogf(ERROR, "%s: Failed to get aml pwm period, st = %d", __func__, st);
    return st;
  }

  ddk::PwmProtocolClient first_cluster_pwm;
  first_cluster_pwm = ddk::PwmProtocolClient(parent, "pwm-ao-d");
  st = InitPwmProtocolClient(first_cluster_pwm);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to initialize Big Cluster PWM Client, st = %d", __func__, st);
    return st;
  }

  ddk::PwmProtocolClient second_cluster_pwm;
  second_cluster_pwm = ddk::PwmProtocolClient(parent, "pwm-a");
  st = InitPwmProtocolClient(second_cluster_pwm);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to initialize Little Cluster PWM Client, st = %d", __func__, st);
    return st;
  }

  ddk::VregProtocolClient first_cluster_vreg(parent, "vreg-pwm-a");
  ddk::VregProtocolClient second_cluster_vreg(
      parent, (device_info.pid == PDEV_PID_LUIS) ? "vreg-pp1000-cpu-a" : "vreg-pwm-ao-d");

  std::unique_ptr<AmlPower> power_impl_device;

  switch (device_info.pid) {
    case PDEV_PID_ASTRO:
      if (!first_cluster_pwm.is_valid() || !voltage_table.has_value() || !pwm_period.has_value()) {
        zxlogf(ERROR,
               "Invalid args. Astro requires first cluster pwm, voltage table, and pwm period");
        return ZX_ERR_INTERNAL;
      }
      power_impl_device = std::make_unique<AmlPower>(parent, first_cluster_pwm,
                                                     std::move(*voltage_table), *pwm_period);
      break;
    case PDEV_PID_LUIS:
      if (!first_cluster_pwm.is_valid() || !voltage_table.has_value() || !pwm_period.has_value() ||
          !second_cluster_vreg.is_valid()) {
        zxlogf(ERROR,
               "Invalid args. Luis requires first cluster pwm, voltage table, pwm period, and "
               "second cluster vreg");
        return ZX_ERR_INTERNAL;
      }
      power_impl_device = std::make_unique<AmlPower>(parent, second_cluster_vreg, first_cluster_pwm,
                                                     std::move(*voltage_table), *pwm_period);
      break;
    case PDEV_PID_SHERLOCK:
      if (!first_cluster_pwm.is_valid() || !voltage_table.has_value() || !pwm_period.has_value() ||
          !second_cluster_pwm.is_valid()) {
        zxlogf(ERROR,
               "Invalid args. Sherlock requires first cluster pwm, voltage table, pwm period, and "
               "second cluster pwm");
        return ZX_ERR_INTERNAL;
      }
      power_impl_device = std::make_unique<AmlPower>(parent, first_cluster_pwm, second_cluster_pwm,
                                                     std::move(*voltage_table), *pwm_period);
      break;
    case PDEV_PID_AMLOGIC_A311D:
      if (!first_cluster_vreg.is_valid() || !second_cluster_vreg.is_valid()) {
        zxlogf(ERROR, "Invalid args. Sherlock requires first cluster vreg and second cluster vreg");
        return ZX_ERR_INTERNAL;
      }
      power_impl_device =
          std::make_unique<AmlPower>(parent, first_cluster_vreg, second_cluster_vreg);
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
