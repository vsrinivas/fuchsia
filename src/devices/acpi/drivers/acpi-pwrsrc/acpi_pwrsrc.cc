// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/acpi/drivers/acpi-pwrsrc/acpi_pwrsrc.h"

#include <fidl/fuchsia.hardware.acpi/cpp/wire_types.h>
#include <fidl/fuchsia.hardware.power/cpp/wire_types.h>
#include <lib/async/cpp/task.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/fdf/cpp/dispatcher.h>

#include "src/devices/acpi/drivers/acpi-pwrsrc/acpi_pwrsrc-bind.h"
#include "src/devices/lib/acpi/client.h"

namespace acpi_pwrsrc {
namespace facpi = fuchsia_hardware_acpi::wire;

zx_status_t AcpiPwrsrc::Bind(void* ctx, zx_device_t* dev) {
  auto acpi = acpi::Client::Create(dev);
  if (acpi.is_error()) {
    return acpi.error_value();
  }

  async_dispatcher_t* dispatcher = fdf::Dispatcher::GetCurrent()->async_dispatcher();
  auto pwrsrc = std::make_unique<AcpiPwrsrc>(dev, std::move(acpi.value()), dispatcher);
  zx_status_t status = pwrsrc->Bind();
  if (status == ZX_OK) {
    // The DDK takes ownership of the device.
    __UNUSED auto unused = pwrsrc.release();
  }

  return status;
}

zx_status_t AcpiPwrsrc::Bind() {
  zx_status_t status = zx::event::create(0, &state_event_);
  if (status != ZX_OK) {
    return status;
  }

  return DdkAdd(ddk::DeviceAddArgs("acpi_pwrsrc").set_proto_id(ZX_PROTOCOL_POWER));
}

void AcpiPwrsrc::DdkInit(ddk::InitTxn txn) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_acpi::NotifyHandler>();
  if (endpoints.is_error()) {
    txn.Reply(endpoints.error_value());
    return;
  }

  fidl::BindServer<fidl::WireServer<fuchsia_hardware_acpi::NotifyHandler>>(
      dispatcher_, std::move(endpoints->server), this);

  auto result = acpi_.borrow()->InstallNotifyHandler(facpi::NotificationMode::kDevice,
                                                     std::move(endpoints->client));
  if (!result.ok()) {
    txn.Reply(result.status());
    return;
  }

  zx_status_t status = CheckOnline();
  if (status != ZX_OK) {
    txn.Reply(status);
    return;
  }

  txn.Reply(ZX_OK);
}

void AcpiPwrsrc::DdkRelease() { delete this; }

void AcpiPwrsrc::GetPowerInfo(GetPowerInfoRequestView request,
                              GetPowerInfoCompleter::Sync& completer) {
  fuchsia_hardware_power::wire::SourceInfo info;
  info.type = fuchsia_hardware_power::wire::PowerType::kAc;
  {
    std::scoped_lock lock(lock_);
    info.state = online_ ? fuchsia_hardware_power::wire::kPowerStateOnline : 0;
  }

  // Clear the signal.
  state_event_.signal(ZX_USER_SIGNAL_0, 0);

  completer.Reply(ZX_OK, info);
}

void AcpiPwrsrc::GetStateChangeEvent(GetStateChangeEventRequestView request,
                                     GetStateChangeEventCompleter::Sync& completer) {
  zx::event dup;
  zx_status_t status = state_event_.duplicate(ZX_RIGHT_WAIT | ZX_RIGHT_TRANSFER, &dup);
  if (status == ZX_OK) {
    // Clear event before returning.
    state_event_.signal(ZX_USER_SIGNAL_0, 0);
  }
  completer.Reply(status, std::move(dup));
}

void AcpiPwrsrc::GetBatteryInfo(GetBatteryInfoRequestView request,
                                GetBatteryInfoCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, fuchsia_hardware_power::wire::BatteryInfo());
}

void AcpiPwrsrc::Handle(HandleRequestView request, HandleCompleter::Sync& completer) {
  if (request->value == kPowerSourceStateChanged) {
    // TODO(fxbug.dev/37719): there seems to exist an ordering problem in
    // some ACPI EC firmware such that the event notification takes place before
    // the actual state update, resulting in the immediate call to _PSR obtaining stale data.
    // Instead, we must delay the PSR evaluation so as to allow time for the
    // actual state to update following the 0x80 event notification.
    async::PostDelayedTask(
        dispatcher_, [this]() { __UNUSED auto result = CheckOnline(); }, zx::msec(200));
  }

  completer.Reply();
}

zx_status_t AcpiPwrsrc::CheckOnline() {
  auto result =
      acpi_.borrow()->EvaluateObject("_PSR", facpi::EvaluateObjectMode::kPlainObject,
                                     fidl::VectorView<fuchsia_hardware_acpi::wire::Object>());
  if (!result.ok()) {
    zxlogf(ERROR, "EvaluateObject FIDL call failed: %s", result.FormatDescription().data());
    return result.status();
  }

  if (result->result.is_err()) {
    zxlogf(ERROR, "_PSR call failed: %d", int(result->result.err()));
    return ZX_ERR_INTERNAL;
  }

  if (!result->result.response().result.is_object() ||
      !result->result.response().result.object().is_integer_val()) {
    zxlogf(ERROR, "_PSR call returned wrong type");
    return ZX_ERR_INTERNAL;
  }

  uint64_t online = result->result.response().result.object().integer_val();
  {
    std::scoped_lock lock(lock_);
    if (online_ != online) {
      online_ = online;
      state_event_.signal(0, ZX_USER_SIGNAL_0);
    }
  }
  return ZX_OK;
}

static zx_driver_ops_t acpi_pwrsrc_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = AcpiPwrsrc::Bind,
};

}  // namespace acpi_pwrsrc

// clang-format off
ZIRCON_DRIVER(acpi-pwrsrc, acpi_pwrsrc::acpi_pwrsrc_driver_ops, "zircon", "0.1");
