// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-pd.h"

#include <lib/ddk/debug.h>

#include <chromiumos-platform-ec/ec_commands.h>

#include "dev.h"

zx_status_t AcpiCrOsEcUsbPdDevice::Bind(zx_device_t* parent,
                                        fbl::RefPtr<cros_ec::EmbeddedController> ec,
                                        std::unique_ptr<cros_ec::AcpiHandle> acpi_handle,
                                        AcpiCrOsEcUsbPdDevice** device) {
  zx::event event;
  zx_status_t status = zx::event::create(0, &event);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to create event object: %s", zx_status_get_string(status));
    return status;
  }

  fbl::AllocChecker ac;
  cros_ec::AcpiHandle* acpi_handle_ptr = acpi_handle.get();
  std::unique_ptr<AcpiCrOsEcUsbPdDevice> dev(new (&ac) AcpiCrOsEcUsbPdDevice(
      std::move(ec), parent, std::move(acpi_handle), std::move(event)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  status = dev->GetPorts();
  if (status != ZX_OK) {
    return status;
  }

  status = dev->UpdateState();
  if (status != ZX_OK) {
    return status;
  }

  status = acpi_handle_ptr->InstallNotifyHandler(ACPI_DEVICE_NOTIFY, NotifyHandler, dev.get());
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not install notify handler: %s", zx_status_get_string(status));
    return status;
  }

  ddk::DeviceAddArgs args("acpi-cros-ec-usb-pd");
  args.set_proto_id(ZX_PROTOCOL_POWER);
  status = dev->DdkAdd(args);
  if (status != ZX_OK) {
    return status;
  }

  // Ownership has transferred to the DDK, so release our unique_ptr, but
  // let the caller have a pointer to it.
  if (device != nullptr) {
    *device = dev.get();
  }
  (void)dev.release();

  return ZX_OK;
}

void AcpiCrOsEcUsbPdDevice::NotifyHandler(ACPI_HANDLE handle, UINT32 value, void* ctx) {
  if (value != AcpiCrOsEcUsbPdDevice::kPowerChangedNotification) {
    return;
  }

  auto dev = reinterpret_cast<AcpiCrOsEcUsbPdDevice*>(ctx);
  zx_status_t status = dev->HandleEvent();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to handle notification: %s", zx_status_get_string(status));
    return;
  }
}

zx_status_t AcpiCrOsEcUsbPdDevice::HandleEvent() {
  bool state_changed;
  zx_status_t status = UpdateState(&state_changed);
  if (status != ZX_OK) {
    return status;
  }

  if (state_changed) {
    event_.signal(0, ZX_USER_SIGNAL_0);
  }
  return ZX_OK;
}

zx_status_t AcpiCrOsEcUsbPdDevice::GetPorts() {
  ec_response_usb_pd_ports response;
  zx_status_t status = ec_->IssueCommand(EC_CMD_USB_PD_PORTS, 0, response);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to issue EC_CMD_USB_PD_PORTS: %s", zx_status_get_string(status));
    return status;
  }

  if (!ports_.empty()) {
    zxlogf(ERROR, "GetPorts() called after ports_ already initialized");
    return ZX_ERR_BAD_STATE;
  }

  for (size_t port = 0; port != response.num_ports; ++port) {
    ports_.push_back(PortState::kNotCharging);
  }

  return ZX_OK;
}

zx_status_t AcpiCrOsEcUsbPdDevice::UpdateState(bool* changed) {
  if (changed != nullptr) {
    *changed = false;
  }
  for (uint8_t port = 0; port != ports_.size(); ++port) {
    ec_params_usb_pd_power_info request = {
        .port = port,
    };
    ec_response_usb_pd_power_info response;
    zx_status_t status = ec_->IssueCommand(EC_CMD_USB_PD_POWER_INFO, 0, request, response);
    if (status != ZX_OK) {
      return status;
    }

    PortState new_state;
    switch (response.role) {
      case USB_PD_PORT_POWER_DISCONNECTED:
      case USB_PD_PORT_POWER_SOURCE:
      case USB_PD_PORT_POWER_SINK_NOT_CHARGING:
        new_state = PortState::kNotCharging;
        break;
      case USB_PD_PORT_POWER_SINK:
        new_state = PortState::kCharging;
        break;
      default:
        zxlogf(ERROR, "EC returned invalid role for port %u: %u", port, response.role);
        return ZX_ERR_INTERNAL;
    }

    if (changed != nullptr && ports_[port] != new_state) {
      *changed = true;
    }
    ports_[port] = new_state;
  }

  return ZX_OK;
}

void AcpiCrOsEcUsbPdDevice::GetPowerInfo(GetPowerInfoRequestView request,
                                         GetPowerInfoCompleter::Sync& completer) {
  zx_status_t status = UpdateState();
  if (status != ZX_OK) {
    completer.Reply(status, {});
    return;
  }

  // If any port is charging then report that we're charging.
  bool charging = false;
  for (auto port : ports_) {
    if (port == PortState::kCharging) {
      charging = true;
    }
  }

  fuchsia_hardware_power::wire::SourceInfo info = {
      .type = fuchsia_hardware_power::wire::PowerType::kAc,
      .state = charging ? fuchsia_hardware_power::wire::kPowerStateCharging
                        : fuchsia_hardware_power::wire::kPowerStateDischarging,
  };

  // Reading state clears the signal
  event_.signal(ZX_USER_SIGNAL_0, 0);

  completer.Reply(ZX_OK, info);
}

void AcpiCrOsEcUsbPdDevice::GetStateChangeEvent(GetStateChangeEventRequestView request,
                                                GetStateChangeEventCompleter::Sync& completer) {
  zx::event client_event;
  zx_status_t status = event_.duplicate(ZX_RIGHT_WAIT | ZX_RIGHT_TRANSFER, &client_event);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to duplicate event object: %s", zx_status_get_string(status));
    completer.Reply(status, {});
    return;
  }

  // Clear the signal before returning, so the next state change triggers the event.
  event_.signal(ZX_USER_SIGNAL_0, 0);

  completer.Reply(ZX_OK, std::move(client_event));
}

void AcpiCrOsEcUsbPdDevice::GetBatteryInfo(GetBatteryInfoRequestView request,
                                           GetBatteryInfoCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, {});
}

void AcpiCrOsEcUsbPdDevice::DdkRelease() { delete this; }
