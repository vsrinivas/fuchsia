// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/acpi/drivers/intel-thermal/intel_thermal.h"

#include <fidl/fuchsia.hardware.acpi/cpp/markers.h>
#include <fidl/fuchsia.hardware.acpi/cpp/wire_types.h>
#include <fidl/fuchsia.hardware.thermal/cpp/wire_types.h>
#include <iconv.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fit/defer.h>
#include <zircon/types.h>

#include "src/devices/acpi/drivers/intel-thermal/intel_thermal-bind.h"
#include "src/devices/lib/acpi/client.h"

namespace intel_thermal {
namespace facpi = fuchsia_hardware_acpi::wire;
namespace fthermal = fuchsia_hardware_thermal::wire;

namespace {
constexpr float kKelvinCelsiusOffset = 273.15f;
inline float DecikelvinToCelsius(uint64_t temp_decikelvin) {
  return (static_cast<float>(temp_decikelvin) / 10.0f) - kKelvinCelsiusOffset;
}

inline uint64_t CelsiusToDecikelvin(float temp_celsius) {
  return static_cast<uint64_t>(roundf((temp_celsius + kKelvinCelsiusOffset) * 10.0f));
}
}  // namespace

zx_status_t IntelThermal::Bind(void* ctx, zx_device_t* dev) {
  auto client = acpi::Client::Create(dev);
  if (client.is_error()) {
    zxlogf(ERROR, "Failed to create ACPI client: %s", zx_status_get_string(client.error_value()));
    return client.error_value();
  }

  async_dispatcher_t* dispatcher = fdf::Dispatcher::GetCurrent()->async_dispatcher();
  auto device = std::make_unique<IntelThermal>(dev, std::move(client.value()), dispatcher);
  zx_status_t status = device->Bind();
  if (status == ZX_OK) {
    // The DDK takes ownership of the device.
    __UNUSED auto unused = device.release();
  }

  return status;
}

zx_status_t IntelThermal::Bind() {
  std::scoped_lock lock(lock_);
  zx_status_t status = zx::event::create(0, &event_);
  if (status != ZX_OK) {
    return status;
  }

  auto ptyp = EvaluateInteger("PTYP");
  if (ptyp.is_error()) {
    return ptyp.error_value();
  }

  if (ptyp.value() != kTypeThermalSensor) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto trip_points = EvaluateInteger("PATC");
  if (trip_points.is_error()) {
    return trip_points.error_value();
  }
  trip_point_count_ = static_cast<uint32_t>(trip_points.value());

  auto description = acpi_.borrow()->EvaluateObject("_STR", facpi::EvaluateObjectMode::kPlainObject,
                                                    fidl::VectorView<facpi::Object>());
  if (!description.ok()) {
    zxlogf(ERROR, "FIDL EvaluateObject failed: %s", zx_status_get_string(description.status()));
    return description.status();
  }

  if (description.value().is_error()) {
    zxlogf(ERROR, "EvaluateObject failed: %d", int(description.value().error_value()));
    return ZX_ERR_INTERNAL;
  }

  if (!description->value()->result.is_object() ||
      !description->value()->result.object().is_buffer_val()) {
    zxlogf(ERROR, "EvaluateObject returned a bad type, expected a buffer.");
    return ZX_ERR_WRONG_TYPE;
  }
  auto& buf = description->value()->result.object().buffer_val();

  /* The description is a UTF16 string. Use iconv(3) to convert between UTF16 and ASCII. */
  char dest_buf[256];
  char* dst_ptr = dest_buf;
  iconv_t converter = iconv_open("ascii", "utf16le");
  char* ptr = reinterpret_cast<char*>(buf.mutable_data());
  size_t src_count = buf.count();
  size_t dst_count = sizeof(dest_buf);
  size_t converted = iconv(converter, &ptr, &src_count, &dst_ptr, &dst_count);
  if (converted != static_cast<size_t>(-1)) {
    inspect_.GetRoot().CreateString("description", std::string(dest_buf), &inspect_);
  } else {
    zxlogf(ERROR, "iconv() failed: %s", strerror(errno));
  }
  iconv_close(converter);

  return DdkAdd(ddk::DeviceAddArgs("intel_thermal")
                    .set_inspect_vmo(inspect_.DuplicateVmo())
                    .set_proto_id(ZX_PROTOCOL_THERMAL));
}

void IntelThermal::DdkInit(ddk::InitTxn txn) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_acpi::NotifyHandler>();
  if (endpoints.is_error()) {
    zxlogf(ERROR, "CreateEndpoints failed: %s", endpoints.status_string());
    txn.Reply(endpoints.status_value());
    return;
  }

  fidl::BindServer<fidl::WireServer<fuchsia_hardware_acpi::NotifyHandler>>(
      dispatcher_, std::move(endpoints->server), this);

  auto result = acpi_.borrow()->InstallNotifyHandler(facpi::NotificationMode::kDevice,
                                                     std::move(endpoints->client));
  if (!result.ok()) {
    zxlogf(ERROR, "InstallNotifyHandler failed: %s", result.FormatDescription().data());
    txn.Reply(result.status());
    return;
  }

  if (result.value().is_error()) {
    zxlogf(ERROR, "InstallNotifyHandler failed: %d", int(result.value().error_value()));
    txn.Reply(ZX_ERR_INTERNAL);
    return;
  }

  txn.Reply(ZX_OK);
}

void IntelThermal::DdkRelease() { delete this; }

void IntelThermal::GetDeviceInfo(GetDeviceInfoRequestView request,
                                 GetDeviceInfoCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, nullptr);
}
void IntelThermal::GetDvfsInfo(GetDvfsInfoRequestView request,
                               GetDvfsInfoCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, nullptr);
}
void IntelThermal::GetDvfsOperatingPoint(GetDvfsOperatingPointRequestView request,
                                         GetDvfsOperatingPointCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, 0);
}
void IntelThermal::GetFanLevel(GetFanLevelRequestView request,
                               GetFanLevelCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, 0);
}
void IntelThermal::GetInfo(GetInfoRequestView request, GetInfoCompleter::Sync& completer) {
  event_.signal(ZX_USER_SIGNAL_0, 0);

  std::scoped_lock lock(lock_);
  fthermal::ThermalInfo info;
  auto passive_temp = EvaluateInteger("_PSV");
  if (passive_temp.is_error()) {
    completer.Reply(passive_temp.status_value(), nullptr);
    return;
  }

  info.passive_temp_celsius = DecikelvinToCelsius(passive_temp.value());

  auto critical_temp = EvaluateInteger("_CRT");
  if (critical_temp.is_error()) {
    completer.Reply(critical_temp.status_value(), nullptr);
    return;
  }

  info.critical_temp_celsius = DecikelvinToCelsius(critical_temp.value());

  info.max_trip_count = trip_point_count_;
  memcpy(info.active_trip.data(), trip_points_, sizeof(trip_points_));

  auto cur_temp = EvaluateInteger("_TMP");
  if (cur_temp.is_error()) {
    completer.Reply(cur_temp.status_value(), nullptr);
    return;
  }

  info.state = 0;
  if (have_trip_[0] && (DecikelvinToCelsius(cur_temp.value()) > trip_points_[0])) {
    info.state |= fthermal::kThermalStateTripViolation;
  }

  completer.Reply(ZX_OK, fidl::ObjectView<fthermal::ThermalInfo>::FromExternal(&info));
}

void IntelThermal::GetStateChangeEvent(GetStateChangeEventRequestView request,
                                       GetStateChangeEventCompleter::Sync& completer) {
  zx::event duplicate;
  zx_status_t status = event_.duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate);
  if (status == ZX_OK) {
    event_.signal(ZX_USER_SIGNAL_0, 0);
  }

  completer.Reply(status, std::move(duplicate));
}

void IntelThermal::GetStateChangePort(GetStateChangePortRequestView request,
                                      GetStateChangePortCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, zx::port());
}

void IntelThermal::GetTemperatureCelsius(GetTemperatureCelsiusRequestView request,
                                         GetTemperatureCelsiusCompleter::Sync& completer) {
  auto temp = EvaluateInteger("_TMP");
  if (temp.is_error()) {
    completer.Reply(temp.status_value(), 0);
  }

  completer.Reply(ZX_OK, DecikelvinToCelsius(temp.value()));
}

void IntelThermal::SetDvfsOperatingPoint(SetDvfsOperatingPointRequestView request,
                                         SetDvfsOperatingPointCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED);
}

void IntelThermal::SetFanLevel(SetFanLevelRequestView request,
                               SetFanLevelCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED);
}

void IntelThermal::SetTripCelsius(SetTripCelsiusRequestView request,
                                  SetTripCelsiusCompleter::Sync& completer) {
  // only one trip point for now
  if (request->id >= 1) {
    completer.Reply(ZX_ERR_NOT_SUPPORTED);
    return;
  }

  std::scoped_lock lock(lock_);

  fidl::Arena<> arena;
  facpi::Object arg = facpi::Object::WithIntegerVal(arena, CelsiusToDecikelvin(request->temp));
  auto result =
      acpi_.borrow()->EvaluateObject("PAT0", facpi::EvaluateObjectMode::kPlainObject,
                                     fidl::VectorView<facpi::Object>::FromExternal(&arg, 1));
  if (!result.ok()) {
    zxlogf(ERROR, "Failed to send FIDL EvaluateObject for PAT0: %s",
           result.FormatDescription().data());
    completer.Reply(result.status());
    return;
  }
  if (result.value().is_error()) {
    zxlogf(ERROR, "Failed to call PAT0: %d", int(result.value().error_value()));
    completer.Reply(ZX_ERR_INTERNAL);
    return;
  }

  have_trip_[0] = true;
  trip_points_[0] = request->temp;
  completer.Reply(ZX_OK);
}

void IntelThermal::Handle(HandleRequestView request, HandleCompleter::Sync& completer) {
  if (request->value == kThermalEvent) {
    event_.signal(0, ZX_USER_SIGNAL_0);
  }
  completer.Reply();
}

zx::status<uint64_t> IntelThermal::EvaluateInteger(const char* name) {
  auto result = acpi_.borrow()->EvaluateObject(fidl::StringView::FromExternal(name),
                                               facpi::EvaluateObjectMode::kPlainObject,
                                               fidl::VectorView<facpi::Object>());
  if (!result.ok()) {
    return zx::error(result.status());
  }

  if (result.value().is_error()) {
    zxlogf(ERROR, "EvaluateObject(%s) failed: %d", name, int(result.value().error_value()));
    return zx::error(ZX_ERR_INTERNAL);
  }

  if (!result->value()->result.is_object() || !result->value()->result.object().is_integer_val()) {
    zxlogf(ERROR, "EvaluateObject(%s) returned the wrong type", name);
    return zx::error(ZX_ERR_WRONG_TYPE);
  }

  return zx::ok(result->value()->result.object().integer_val());
}

static zx_driver_ops_t intel_thermal_driver_ops{
    .version = DRIVER_OPS_VERSION,
    .bind = IntelThermal::Bind,
};

}  // namespace intel_thermal

// clang-format off
ZIRCON_DRIVER(intel-thermal, intel_thermal::intel_thermal_driver_ops, "zircon", "0.1");
