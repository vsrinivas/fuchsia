// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/acpi/drivers/acpi-battery/acpi_battery.h"

#include <fidl/fuchsia.hardware.acpi/cpp/wire.h>
#include <fidl/fuchsia.hardware.power/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/fit/defer.h>
#include <lib/zx/clock.h>
#include <zircon/types.h>

#include "src/devices/acpi/drivers/acpi-battery/acpi_battery-bind.h"
#include "src/devices/lib/acpi/client.h"

namespace acpi_battery {

namespace facpi = fuchsia_hardware_acpi::wire;
constexpr uint64_t kStaBatteryPresent = (1 << 4);

zx_status_t AcpiBattery::Bind(void* ctx, zx_device_t* parent) {
  auto acpi = acpi::Client::Create(parent);
  if (acpi.is_error()) {
    zxlogf(ERROR, "Failed to get ACPI device: %s", zx_status_get_string(acpi.error_value()));
    return acpi.error_value();
  }
  auto device = std::make_unique<AcpiBattery>(parent, std::move(acpi.value()));
  auto status = device->Bind();
  if (status == ZX_OK) {
    // The DDK takes ownership of the device.
    __UNUSED auto unused = device.release();
  }
  return status;
}

zx_status_t AcpiBattery::Bind() {
  zx_status_t status = zx::event::create(0, &state_event_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to create event: %s", zx_status_get_string(status));
    return status;
  }

  return DdkAdd(ddk::DeviceAddArgs("acpi-battery")
                    .set_inspect_vmo(inspect_.DuplicateVmo())
                    .set_proto_id(ZX_PROTOCOL_POWER));
}

void AcpiBattery::DdkInit(ddk::InitTxn txn) {
  zx_status_t status = ZX_OK;
  auto reply = fit::defer([&status, &txn]() { txn.Reply(status); });
  status = CheckAcpiState();
  if (status != ZX_OK) {
    zxlogf(ERROR, "CheckAcpiState failed: %s", zx_status_get_string(status));
    return;
  }
  status = CheckAcpiBatteryInformation();
  if (status != ZX_OK) {
    zxlogf(ERROR, "CheckAcpiBatteryInformation failed: %s", zx_status_get_string(status));
    return;
  }
  status = CheckAcpiBatteryState();
  if (status != ZX_OK) {
    zxlogf(ERROR, "CheckAcpiBatteryState failed: %s", zx_status_get_string(status));
    return;
  }

  // Set up the notify handler.
  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_acpi::NotifyHandler>();
  if (endpoints.is_error()) {
    status = endpoints.error_value();
    zxlogf(ERROR, "CreateEndpoints failed: %s", zx_status_get_string(status));
    return;
  }

  async_dispatcher_t* dispatcher = device_get_dispatcher(zxdev());
  fidl::BindServer<fidl::WireServer<fuchsia_hardware_acpi::NotifyHandler>>(
      dispatcher, std::move(endpoints->server), this);

  auto result = acpi_.borrow()->InstallNotifyHandler(facpi::NotificationMode::kDevice,
                                                     std::move(endpoints->client));
  if (!result.ok()) {
    zxlogf(ERROR, "Failed to send installnotifyhandler FIDL request: %s",
           result.FormatDescription().data());
    status = result.status();
    return;
  }

  if (result->result.is_err()) {
    zxlogf(ERROR, "Failed to InstallNotifyHandler: %d", int(result->result.err()));
    status = ZX_ERR_INTERNAL;
    return;
  }
}

void AcpiBattery::DdkRelease() { delete this; }

zx_status_t AcpiBattery::SignalClient() {
  zx_status_t status = state_event_.signal(0, ZX_USER_SIGNAL_0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to set signal on event: %s", zx_status_get_string(status));
  }
  return status;
}

zx_status_t AcpiBattery::ClearSignal() {
  zx_status_t status = state_event_.signal(ZX_USER_SIGNAL_0, 0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to clear signal on event: %s", zx_status_get_string(status));
  }
  return status;
}

zx_status_t AcpiBattery::CheckAcpiState() {
  auto result = acpi_.borrow()->EvaluateObject("_STA", facpi::EvaluateObjectMode::kPlainObject, {});
  if (!result.ok()) {
    zxlogf(ERROR, "EvaluateObject FIDL call failed: %s", zx_status_get_string(result.status()));
    return result.status();
  }

  if (result->result.is_err()) {
    zxlogf(ERROR, "EvaluateObject failed: %d", int(result->result.err()));
    return ZX_ERR_INTERNAL;
  }

  if (!result->result.response().result.is_object() ||
      !result->result.response().result.object().is_integer_val()) {
    zxlogf(ERROR, "Unexpected response from EvaluateObject");
    return ZX_ERR_INTERNAL;
  }

  std::scoped_lock lock(lock_);
  uint64_t state = result->result.response().result.object().integer_val();
  uint8_t old = source_info_.state;
  if (state & kStaBatteryPresent) {
    source_info_.state |= fpower::kPowerStateOnline;
  } else {
    source_info_.state &= ~fpower::kPowerStateOnline;
  }

  zx_status_t status = ZX_OK;
  if (source_info_.state != old) {
    status = SignalClient();
  }

  return status;
}

zx_status_t AcpiBattery::CheckAcpiBatteryInformation() {
  auto result = acpi_.borrow()->EvaluateObject("_BIF", facpi::EvaluateObjectMode::kPlainObject, {});
  if (!result.ok()) {
    zxlogf(ERROR, "EvaluateObject FIDL call failed: %s", zx_status_get_string(result.status()));
    return result.status();
  }

  if (result->result.is_err()) {
    zxlogf(ERROR, "EvaluateObject failed: %d", int(result->result.err()));
    return ZX_ERR_INTERNAL;
  }

  if (!result->result.response().result.is_object() ||
      !result->result.response().result.object().is_package_val() ||
      result->result.response().result.object().package_val().value.count() < BifFields::kBifMax) {
    zxlogf(ERROR, "Unexpected response from EvaluateObject");
    return ZX_ERR_INTERNAL;
  }

  // Validate the _BIF package elements' types.
  auto& elements = result->result.response().result.object().package_val().value;
  for (size_t i = 0; i < BifFields::kModelNumber; i++) {
    if (!elements[i].is_integer_val()) {
      zxlogf(ERROR, "_BIF expected field %zu to be an integer", i);
      return ZX_ERR_INTERNAL;
    }
  }
  for (size_t i = BifFields::kModelNumber; i < BifFields::kBifMax; i++) {
    if (!elements[i].is_string_val()) {
      zxlogf(ERROR, "_BIF expected field %zu to be a string", i);
      return ZX_ERR_INTERNAL;
    }
  }

  auto intval = [&elements](BifFields field) {
    return static_cast<uint32_t>(elements[field].integer_val());
  };

  std::scoped_lock lock(lock_);
  battery_info_.unit = fpower::BatteryUnit(intval(BifFields::kPowerUnit));
  battery_info_.design_capacity = intval(BifFields::kDesignCapacity);
  battery_info_.last_full_capacity = intval(BifFields::kLastFullChargeCapacity);
  battery_info_.design_voltage = intval(BifFields::kDesignVoltage);
  battery_info_.capacity_warning = intval(BifFields::kDesignCapacityWarning);
  battery_info_.capacity_low = intval(BifFields::kDesignCapacityLow);
  battery_info_.capacity_granularity_low_warning = intval(BifFields::kCapacityGranularity1);
  battery_info_.capacity_granularity_warning_full = intval(BifFields::kCapacityGranularity2);

  auto set_inspect = [&elements](BifFields field, inspect::StringProperty& prop) {
    auto& str = elements[field].string_val();
    prop.Set(std::string(str.data(), str.size()));
  };
  set_inspect(BifFields::kModelNumber, model_number_);
  set_inspect(BifFields::kSerialNumber, serial_number_);
  set_inspect(BifFields::kBatteryType, battery_type_);

  return ZX_OK;
}

zx_status_t AcpiBattery::CheckAcpiBatteryState() {
  auto result = acpi_.borrow()->EvaluateObject("_BST", facpi::EvaluateObjectMode::kPlainObject, {});
  if (!result.ok()) {
    zxlogf(ERROR, "EvaluateObject FIDL call failed: %s", zx_status_get_string(result.status()));
    return result.status();
  }

  if (result->result.is_err()) {
    zxlogf(ERROR, "EvaluateObject failed: %d", int(result->result.err()));
    return ZX_ERR_INTERNAL;
  }

  if (!result->result.response().result.is_object() ||
      !result->result.response().result.object().is_package_val() ||
      result->result.response().result.object().package_val().value.count() < BstFields::kBstMax) {
    zxlogf(ERROR, "Unexpected response from EvaluateObject");
    return ZX_ERR_INTERNAL;
  }

  // Validate the _BST package elements' types.
  auto& elements = result->result.response().result.object().package_val().value;
  for (size_t i = 0; i < BstFields::kBstMax; i++) {
    if (!elements[i].is_integer_val()) {
      zxlogf(ERROR, "_BST expected field %zu to be an integer", i);
      return ZX_ERR_INTERNAL;
    }
  }

  std::scoped_lock lock(lock_);
  uint8_t old_state = source_info_.state;
  auto acpi_state = elements[BstFields::kBatteryState].integer_val();
  if (acpi_state & AcpiBatteryState::kDischarging) {
    source_info_.state |= fpower::kPowerStateDischarging;
  } else {
    source_info_.state &= ~fpower::kPowerStateDischarging;
  }

  if (acpi_state & AcpiBatteryState::kCharging) {
    source_info_.state |= fpower::kPowerStateCharging;
  } else {
    source_info_.state &= ~fpower::kPowerStateCharging;
  }

  if (acpi_state & AcpiBatteryState::kCritical) {
    source_info_.state |= fpower::kPowerStateCritical;
  } else {
    source_info_.state &= ~fpower::kPowerStateCritical;
  }

  // Valid values fall within the range 0-0x7fffffff, so we can safely cast to int32_t.
  int32_t rate = static_cast<int32_t>(elements[BstFields::kBatteryCurrentRate].integer_val());
  if (rate >= 0 && acpi_state & AcpiBatteryState::kDischarging) {
    rate = -rate;
  }
  battery_info_.present_rate = rate;
  auto old_charge = battery_info_.remaining_capacity;
  if (battery_info_.last_full_capacity) {
    old_charge = (battery_info_.remaining_capacity * 100) / battery_info_.last_full_capacity;
  }

  battery_info_.remaining_capacity =
      static_cast<uint32_t>(elements[BstFields::kBatteryRemainingCapacity].integer_val());
  battery_info_.present_voltage =
      static_cast<uint32_t>(elements[BstFields::kBatteryCurrentVoltage].integer_val());

  auto new_charge = battery_info_.remaining_capacity;
  if (battery_info_.last_full_capacity) {
    new_charge = (battery_info_.remaining_capacity * 100) / battery_info_.last_full_capacity;
  }

  // signal on change of charging state (e.g charging vs discharging) as well as significant
  // change in charge (percentage point).
  if (old_state != source_info_.state || old_charge != new_charge) {
    zx_status_t status = SignalClient();
    if (status != ZX_OK) {
      return status;
    }
  }

  return ZX_OK;
}

void AcpiBattery::GetPowerInfo(GetPowerInfoRequestView request,
                               GetPowerInfoCompleter::Sync& completer) {
  std::scoped_lock lock(lock_);
  completer.Reply(ZX_OK, source_info_);
  ClearSignal();
}

void AcpiBattery::GetStateChangeEvent(GetStateChangeEventRequestView request,
                                      GetStateChangeEventCompleter::Sync& completer) {
  std::scoped_lock lock(lock_);
  zx::event clone;
  zx_status_t status = state_event_.duplicate(ZX_RIGHT_WAIT | ZX_RIGHT_TRANSFER, &clone);
  if (status == ZX_OK) {
    // Clear signal before returning.
    ClearSignal();
  }
  completer.Reply(status, std::move(clone));
}

void AcpiBattery::GetBatteryInfo(GetBatteryInfoRequestView request,
                                 GetBatteryInfoCompleter::Sync& completer) {
  zx_status_t status = CheckAcpiBatteryState();
  std::scoped_lock lock(lock_);
  completer.Reply(status, battery_info_);
}

void AcpiBattery::Handle(HandleRequestView request, HandleCompleter::Sync& completer) {
  auto reply = fit::defer([&completer]() { completer.Reply(); });
  switch (request->value) {
    case BatteryStatusNotification::kBatteryStatusChanged: {
      zx::time now = zx::clock::get_monotonic();
      if (now < (last_notify_timestamp_ + kAcpiEventNotifyLimit)) {
        zxlogf(DEBUG, "rate-limiting event 0x%x", request->value);
        return;
      }
      CheckAcpiBatteryState();
      break;
    }
    case BatteryStatusNotification::kBatteryInformationChanged: {
      CheckAcpiBatteryInformation();
      CheckAcpiState();
      break;
    }
  }
}

static zx_driver_ops_t acpi_battery_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = AcpiBattery::Bind;
  return ops;
}();

}  // namespace acpi_battery

// clang-format off
ZIRCON_DRIVER(acpi-battery, acpi_battery::acpi_battery_driver_ops, "zircon", "0.1");
