// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-thermal.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <string.h>
#include <zircon/syscalls/port.h>

#include <soc/aml-common/aml-thermal.h>

#include "src/devices/thermal/drivers/aml-thermal-s912/aml-thermal-s912-bind.h"

#define THERMAL_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)

namespace thermal {
namespace {

enum {
  FRAGMENT_SCPI,
  FRAGMENT_GPIO_FAN_0,
  FRAGMENT_GPIO_FAN_1,
  FRAGMENT_COUNT,
};

fthermal::wire::OperatingPoint ScpiToThermalOpps(const scpi_opp_t& opps) {
  fthermal::wire::OperatingPoint thermal_opps = {
      .opp = {},
      .latency = opps.latency,
      .count = opps.count,
  };
  for (uint32_t i = 0; i < opps.count; i++) {
    thermal_opps.opp[i] = {
        .freq_hz = opps.opp[i].freq_hz,
        .volt_uv = opps.opp[i].volt_uv,
    };
  }
  return thermal_opps;
}

}  // namespace

zx_status_t AmlThermal::Create(void* ctx, zx_device_t* device) {
  zxlogf(INFO, "aml_thermal: driver begin...");
  ddk::ScpiProtocolClient scpi(device, "scpi");
  if (!scpi.is_valid()) {
    THERMAL_ERROR("could not get scpi protocol");
    return ZX_ERR_NO_RESOURCES;
  }

  ddk::GpioProtocolClient fan0_gpio(device, "gpio-fan0");
  if (!fan0_gpio.is_valid()) {
    THERMAL_ERROR("could not get fan0 gpio protocol");
    return ZX_ERR_NO_RESOURCES;
  }

  ddk::GpioProtocolClient fan1_gpio(device, "gpio-fan1");
  if (!fan1_gpio.is_valid()) {
    THERMAL_ERROR("could not get fan0 gpio protocol");
    return ZX_ERR_NO_RESOURCES;
  }

  uint32_t sensor_id;
  zx_status_t status = scpi.GetSensor("aml_thermal", &sensor_id);
  if (status != ZX_OK) {
    THERMAL_ERROR("could not thermal get sensor: %d", status);
    return status;
  }

  zx::port port;
  status = zx::port::create(0, &port);
  if (status != ZX_OK) {
    THERMAL_ERROR("could not configure port: %d", status);
    return status;
  }

  auto thermal =
      std::make_unique<AmlThermal>(device, fan0_gpio, fan1_gpio, scpi, sensor_id, std::move(port));

  status = thermal->DdkAdd("vim-thermal");
  if (status != ZX_OK) {
    THERMAL_ERROR("could not add driver: %d", status);
    return status;
  }
  // devmgr is now in charge of this device.
  __UNUSED auto _ = thermal.release();
  return ZX_OK;
}

zx_status_t AmlThermal::ThermalConnect(zx::channel chan) { return ZX_ERR_NOT_SUPPORTED; }

void AmlThermal::GetInfo(GetInfoRequestView request, GetInfoCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, nullptr);
}

void AmlThermal::GetDeviceInfo(GetDeviceInfoRequestView request,
                               GetDeviceInfoCompleter::Sync& completer) {
  completer.Reply(ZX_OK, fidl::ObjectView<fthermal::wire::ThermalDeviceInfo>::FromExternal(&info_));
}

void AmlThermal::GetDvfsInfo(GetDvfsInfoRequestView request,
                             GetDvfsInfoCompleter::Sync& completer) {
  if (static_cast<uint32_t>(request->power_domain) >= fthermal::wire::kMaxDvfsDomains) {
    completer.Reply(ZX_ERR_INVALID_ARGS, nullptr);
    return;
  }

  scpi_opp_t opps = {};
  auto status = scpi_.GetDvfsInfo(static_cast<uint8_t>(request->power_domain), &opps);

  auto thermal_opps = ScpiToThermalOpps(opps);
  auto tracked_opps = fidl::ObjectView<fthermal::wire::OperatingPoint>::FromExternal(&thermal_opps);
  completer.Reply(status, tracked_opps);
}

void AmlThermal::GetTemperatureCelsius(GetTemperatureCelsiusRequestView request,
                                       GetTemperatureCelsiusCompleter::Sync& completer) {
  completer.Reply(ZX_OK, temperature_);
}

void AmlThermal::GetStateChangeEvent(GetStateChangeEventRequestView request,
                                     GetStateChangeEventCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, zx::event());
}

void AmlThermal::GetStateChangePort(GetStateChangePortRequestView request,
                                    GetStateChangePortCompleter::Sync& completer) {
  zx::port dup;
  zx_status_t status = port_.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
  completer.Reply(status, std::move(dup));
}

void AmlThermal::SetTripCelsius(SetTripCelsiusRequestView request,
                                SetTripCelsiusCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED);
}

void AmlThermal::GetDvfsOperatingPoint(GetDvfsOperatingPointRequestView request,
                                       GetDvfsOperatingPointCompleter::Sync& completer) {
  switch (request->power_domain) {
    case fthermal::wire::PowerDomain::kBigClusterPowerDomain:
      completer.Reply(ZX_OK, static_cast<uint16_t>(cur_bigcluster_opp_idx_));
      break;
    case fthermal::wire::PowerDomain::kLittleClusterPowerDomain:
      completer.Reply(ZX_OK, static_cast<uint16_t>(cur_littlecluster_opp_idx_));
      break;
    default:
      completer.Reply(ZX_ERR_INVALID_ARGS, 0);
  }
}

void AmlThermal::SetDvfsOperatingPoint(SetDvfsOperatingPointRequestView request,
                                       SetDvfsOperatingPointCompleter::Sync& completer) {
  zx_status_t status = ZX_OK;
  switch (request->power_domain) {
    case fthermal::wire::PowerDomain::kBigClusterPowerDomain:
      if (request->op_idx != cur_bigcluster_opp_idx_) {
        status = scpi_.SetDvfsIdx(static_cast<uint8_t>(request->power_domain), request->op_idx);
      }
      cur_bigcluster_opp_idx_ = request->op_idx;
      break;

    case fthermal::wire::PowerDomain::kLittleClusterPowerDomain:
      if (request->op_idx != cur_littlecluster_opp_idx_) {
        status = scpi_.SetDvfsIdx(static_cast<uint8_t>(request->power_domain), request->op_idx);
      }
      cur_littlecluster_opp_idx_ = request->op_idx;
      break;

    default:
      status = ZX_ERR_INVALID_ARGS;
  }

  completer.Reply(status);
}

void AmlThermal::GetFanLevel(GetFanLevelRequestView request,
                             GetFanLevelCompleter::Sync& completer) {
  completer.Reply(ZX_OK, fan_level_);
}

void AmlThermal::SetFanLevel(SetFanLevelRequestView request,
                             SetFanLevelCompleter::Sync& completer) {
  completer.Reply(SetFanLevelInternal(static_cast<FanLevel>(request->fan_level)));
}

void AmlThermal::JoinWorkerThread() {
  const auto status = thrd_join(worker_, nullptr);
  if (status != thrd_success) {
    THERMAL_ERROR("worker thread failed: %d", status);
  }
}

void AmlThermal::DdkRelease() {
  if (worker_) {
    JoinWorkerThread();
  }
  delete this;
}

void AmlThermal::DdkUnbind(ddk::UnbindTxn txn) {
  sync_completion_signal(&quit_);
  txn.Reply();
}

void AmlThermal::DdkInit(ddk::InitTxn txn) {
  zx_status_t status = Init();
  txn.Reply(status);
}

zx_status_t AmlThermal::Init() {
  auto status = fan0_gpio_.ConfigOut(0);
  if (status != ZX_OK) {
    THERMAL_ERROR("could not configure FAN_CTL0 gpio: %d", status);
    return status;
  }

  status = fan1_gpio_.ConfigOut(0);
  if (status != ZX_OK) {
    THERMAL_ERROR("could not configure FAN_CTL1 gpio: %d", status);
    return status;
  }

  size_t size;
  status = device_get_fragment_metadata(zxdev(), "scpi", DEVICE_METADATA_THERMAL_CONFIG, nullptr, 0,
                                        &size);
  if (status != ZX_OK) {
    THERMAL_ERROR("could not read device metadata size: %d", status);
    return status;
  }

  std::vector<uint8_t> raw_metadata(size);
  size_t read;
  status = device_get_fragment_metadata(zxdev(), "scpi", DEVICE_METADATA_THERMAL_CONFIG,
                                        raw_metadata.data(), raw_metadata.size(), &read);
  if (status != ZX_OK) {
    THERMAL_ERROR("could not read device metadata: %d", status);
    return status;
  } else if (read != size) {
    THERMAL_ERROR("could not read device metadata");
    return ZX_ERR_NO_MEMORY;
  }
  fidl::DecodedMessage<fthermal::wire::ThermalDeviceInfo> decoded(raw_metadata.data(), size);
  if (!decoded.ok()) {
    THERMAL_ERROR("could not decode device metadata: %d", status);
    return decoded.status();
  }
  info_ = *decoded.PrimaryObject();

  scpi_opp_t opps;
  status = scpi_.GetDvfsInfo(
      static_cast<uint8_t>(fthermal::wire::PowerDomain::kBigClusterPowerDomain), &opps);
  if (status != ZX_OK) {
    THERMAL_ERROR("could not get bigcluster dvfs opps: %d", status);
    return status;
  }

  info_.opps[0] = ScpiToThermalOpps(opps);

  status = scpi_.GetDvfsInfo(
      static_cast<uint8_t>(fthermal::wire::PowerDomain::kLittleClusterPowerDomain), &opps);
  if (status != ZX_OK) {
    THERMAL_ERROR("could not get littlecluster dvfs opps: %d", status);
    return status;
  }

  info_.opps[1] = ScpiToThermalOpps(opps);

  auto start_thread = [](void* arg) { return static_cast<AmlThermal*>(arg)->Worker(); };
  status = thrd_create_with_name(&worker_, start_thread, this, "aml_thermal_notify_thread");
  if (status != ZX_OK) {
    THERMAL_ERROR("could not start worker thread: %d", status);
    return status;
  }

  return ZX_OK;
}

zx_status_t AmlThermal::NotifyThermalDaemon(uint32_t trip_index) const {
  zx_port_packet_t pkt;
  pkt.key = trip_index;
  pkt.type = ZX_PKT_TYPE_USER;
  return port_.queue(&pkt);
}

zx_status_t AmlThermal::SetFanLevelInternal(FanLevel level) {
  // Levels per individual system fan.
  uint8_t fan0_level;
  uint8_t fan1_level;

  switch (level) {
    case FAN_L0:
      fan0_level = 0;
      fan1_level = 0;
      break;
    case FAN_L1:
      fan0_level = 1;
      fan1_level = 0;
      break;
    case FAN_L2:
      fan0_level = 0;
      fan1_level = 1;
      break;
    case FAN_L3:
      fan0_level = 1;
      fan1_level = 1;
      break;
    default:
      THERMAL_ERROR("unknown fan level: %d", level);
      return ZX_ERR_INVALID_ARGS;
  }

  auto status = fan0_gpio_.Write(fan0_level);
  if (status != ZX_OK) {
    THERMAL_ERROR("could not set FAN_CTL0 level: %d", status);
    return status;
  }

  status = fan1_gpio_.Write(fan1_level);
  if (status != ZX_OK) {
    THERMAL_ERROR("could not set FAN_CTL1 level: %d", status);
    return status;
  }

  fan_level_ = level;
  return ZX_OK;
}

int AmlThermal::Worker() {
  zx_status_t status;
  uint32_t trip_pt = 0;
  const uint32_t trip_limit = info_.num_trip_points - 1;
  bool crit = false;
  bool signal = false;

  // Notify thermal daemon of initial settings.
  status = NotifyThermalDaemon(trip_pt);
  if (status != ZX_OK) {
    THERMAL_ERROR("could not notify thermal daemon: %d", status);
    return status;
  }

  do {
    uint32_t temp_integer = 0;
    status = scpi_.GetSensorValue(sensor_id_, &temp_integer);
    if (status != ZX_OK) {
      THERMAL_ERROR("could not read temperature: %d", status);
      return status;
    }

    temperature_ = static_cast<float>(temp_integer);

    signal = true;
    if (trip_pt != trip_limit &&
        temperature_ >= info_.trip_point_info[trip_pt + 1].up_temp_celsius) {
      trip_pt++;  // Triggered next trip point.
    } else if (trip_pt && temperature_ < info_.trip_point_info[trip_pt].down_temp_celsius) {
      if (trip_pt == trip_limit) {
        // A prev trip point triggered, so the temperature is falling
        // down below the critical temperature.  Make a note of that.
        crit = false;
      }
      trip_pt--;  // Triggered prev trip point.
    } else if (trip_pt == trip_limit && temperature_ >= info_.critical_temp_celsius && !crit) {
      // The device temperature is crossing the critical temperature, set
      // the CPU freq to the lowest possible setting to ensure the
      // temperature doesn't rise any further.
      crit = true;
      status = scpi_.SetDvfsIdx(
          static_cast<uint8_t>(fthermal::wire::PowerDomain::kBigClusterPowerDomain), 0);
      if (status != ZX_OK) {
        THERMAL_ERROR("unable to set DVFS OPP for Big cluster");
        return status;
      }

      status = scpi_.SetDvfsIdx(
          static_cast<uint8_t>(fthermal::wire::PowerDomain::kLittleClusterPowerDomain), 0);
      if (status != ZX_OK) {
        THERMAL_ERROR("unable to set DVFS OPP for Little cluster");
        return status;
      }
    } else {
      signal = false;
    }

    if (signal) {
      // Notify the thermal daemon about which trip point triggered.
      status = NotifyThermalDaemon(trip_pt);
      if (status != ZX_OK) {
        THERMAL_ERROR("could not notify thermal daemon: %d", status);
        return status;
      }
    }

  } while (sync_completion_wait(&quit_, duration_.get()) == ZX_ERR_TIMED_OUT);
  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = AmlThermal::Create;
  return ops;
}();

}  // namespace thermal

ZIRCON_DRIVER(aml_thermal, thermal::driver_ops, "zircon", "0.1");
