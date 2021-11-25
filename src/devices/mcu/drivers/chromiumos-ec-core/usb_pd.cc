// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/mcu/drivers/chromiumos-ec-core/usb_pd.h"

#include <lib/ddk/debug.h>
#include <lib/fpromise/promise.h>

#include <chromiumos-platform-ec/ec_commands.h>

#include "src/devices/mcu/drivers/chromiumos-ec-core/chromiumos_ec_core.h"
#include "src/devices/mcu/drivers/chromiumos-ec-core/subdriver.h"

namespace chromiumos_ec_core::usb_pd {

void RegisterUsbPdDriver(ChromiumosEcCore* ec) {
  AcpiCrOsEcUsbPdDevice* device;
  zx_status_t status = AcpiCrOsEcUsbPdDevice::Bind(ec->zxdev(), ec, &device);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to initialise usb-pd device: %s", zx_status_get_string(status));
  }
}

zx_status_t AcpiCrOsEcUsbPdDevice::Bind(zx_device_t* parent, ChromiumosEcCore* ec,
                                        AcpiCrOsEcUsbPdDevice** device) {
  zx::event event;
  zx_status_t status = zx::event::create(0, &event);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to create event object: %s", zx_status_get_string(status));
    return status;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<AcpiCrOsEcUsbPdDevice> dev(
      new (&ac) AcpiCrOsEcUsbPdDevice(ec, parent, std::move(event)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
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

void AcpiCrOsEcUsbPdDevice::DdkInit(ddk::InitTxn txn) {
  notify_deleter_.emplace(ec_->AddNotifyHandler([this](uint32_t value) { NotifyHandler(value); }));
  auto promise =
      GetPorts()
          .and_then([this]() { return UpdateState(); })
          .then([txn = std::move(txn)](fpromise::result<bool, zx_status_t>& result) mutable {
            if (result.is_ok()) {
              txn.Reply(ZX_OK);
            } else {
              txn.Reply(result.take_error());
            }
          });

  ec_->executor().schedule_task(std::move(promise));
}
void AcpiCrOsEcUsbPdDevice::NotifyHandler(uint32_t value) {
  if (value != AcpiCrOsEcUsbPdDevice::kPowerChangedNotification) {
    return;
  }

  HandleEvent();
}

void AcpiCrOsEcUsbPdDevice::HandleEvent() {
  ec_->executor().schedule_task(UpdateState()
                                    .and_then([this](bool& changed) {
                                      if (changed) {
                                        event_.signal(0, ZX_USER_SIGNAL_0);
                                      }
                                    })
                                    .or_else([](zx_status_t& error) {
                                      zxlogf(ERROR, "Failed to update state: %s",
                                             zx_status_get_string(error));
                                    }));
}

fpromise::promise<void, zx_status_t> AcpiCrOsEcUsbPdDevice::GetPorts() {
  if (!ports_.empty()) {
    zxlogf(ERROR, "GetPorts() called after ports_ already initialized");
    return fpromise::make_error_promise(ZX_ERR_BAD_STATE);
  }

  return ec_->IssueCommand(EC_CMD_USB_PD_PORTS, 0)
      .and_then([this](CommandResult& result) -> fpromise::result<void, zx_status_t> {
        auto* ports = result.GetData<ec_response_usb_pd_ports>();
        if (ports == nullptr) {
          zxlogf(ERROR, "Did not get enough data for ec_response_usb_pd_ports");
          return fpromise::error(ZX_ERR_WRONG_TYPE);
        }
        for (size_t port = 0; port != ports->num_ports; ++port) {
          ports_.push_back(PortState::kNotCharging);
        }

        return fpromise::ok();
      });
}

fpromise::promise<bool, zx_status_t> AcpiCrOsEcUsbPdDevice::UpdateState() {
  std::vector<fpromise::promise<bool, zx_status_t>> promises;
  promises.reserve(ports_.size());
  for (uint8_t port = 0; port != ports_.size(); ++port) {
    ec_params_usb_pd_power_info request = {
        .port = port,
    };
    promises.emplace_back(
        ec_->IssueCommand(EC_CMD_USB_PD_POWER_INFO, 0, request)
            .and_then([this, port](CommandResult& result) -> fpromise::result<bool, zx_status_t> {
              auto response = result.GetData<ec_response_usb_pd_power_info>();
              PortState new_state;
              switch (response->role) {
                case USB_PD_PORT_POWER_DISCONNECTED:
                case USB_PD_PORT_POWER_SOURCE:
                case USB_PD_PORT_POWER_SINK_NOT_CHARGING:
                  new_state = PortState::kNotCharging;
                  break;
                case USB_PD_PORT_POWER_SINK:
                  new_state = PortState::kCharging;
                  break;
                default:
                  zxlogf(ERROR, "EC returned invalid role for port %u: %u", port, response->role);
                  return fpromise::error(ZX_ERR_INTERNAL);
              }

              bool changed = false;
              if (ports_[port] != new_state) {
                changed = true;
              }

              ports_[port] = new_state;
              return fpromise::ok(changed);
            }));
  }

  return fpromise::join_promise_vector(std::move(promises))
      .then([](fpromise::result<std::vector<fpromise::result<bool, zx_status_t>>, void>& result)
                -> fpromise::result<bool, zx_status_t> {
        auto results = result.take_value();
        bool changed = false;
        for (auto& result : results) {
          if (result.is_ok() && result.take_value()) {
            changed = true;
          }
          if (result.is_error()) {
            return result.take_error_result();
          }
        }
        return fpromise::ok(changed);
      });
}

void AcpiCrOsEcUsbPdDevice::GetPowerInfo(GetPowerInfoRequestView request,
                                         GetPowerInfoCompleter::Sync& completer) {
  ec_->executor().schedule_task(UpdateState().then(
      [this, completer = completer.ToAsync()](fpromise::result<bool, zx_status_t>& result) mutable {
        if (result.is_error()) {
          completer.Reply(result.take_error(), {});
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
      }));
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

}  // namespace chromiumos_ec_core::usb_pd
