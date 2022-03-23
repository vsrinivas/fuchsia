// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/mcu/drivers/chromiumos-ec-core/chromiumos_ec_core.h"

#include <fidl/fuchsia.hardware.acpi/cpp/wire_types.h>
#include <fidl/fuchsia.hardware.google.ec/cpp/wire_types.h>
#include <fidl/fuchsia.io/cpp/markers.h>
#include <lib/ddk/debug.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/llcpp/server.h>
#include <zircon/errors.h>

#include <chromiumos-platform-ec/ec_commands.h>

#include "src/devices/mcu/drivers/chromiumos-ec-core/chromiumos_ec_core_bind.h"
#include "src/devices/mcu/drivers/chromiumos-ec-core/subdriver.h"

namespace chromiumos_ec_core {
namespace {

// The mapping of features to numbers comes from <chromiumos-platform-ec/ec_commands.h>.
constexpr const char* kEcFeatureNames[] = {
    /* [0] = */ "LIMITED",
    /* [1] = */ "FLASH",
    /* [2] = */ "PWM_FAN",
    /* [3] = */ "PWM_KEYB",
    /* [4] = */ "LIGHTBAR",
    /* [5] = */ "LED",
    /* [6] = */ "MOTION_SENSE",
    /* [7] = */ "KEYB",
    /* [8] = */ "PSTORE",
    /* [9] = */ "PORT80",
    /* [10] = */ "THERMAL",
    /* [11] = */ "BKLIGHT_SWITCH",
    /* [12] = */ "WIFI_SWITCH",
    /* [13] = */ "HOST_EVENTS",
    /* [14] = */ "GPIO",
    /* [15] = */ "I2C",
    /* [16] = */ "CHARGER",
    /* [17] = */ "BATTERY",
    /* [18] = */ "SMART_BATTERY",
    /* [19] = */ "HANG_DETECT",
    /* [20] = */ "PMU",
    /* [21] = */ "SUB_MCU",
    /* [22] = */ "USB_PD",
    /* [23] = */ "USB_MUX",
    /* [24] = */ "MOTION_SENSE_FIFO",
    /* [25] = */ "VSTORE",
    /* [26] = */ "USBC_SS_MUX_VIRTUAL",
    /* [27] = */ "RTC",
    /* [28] = */ "FINGERPRINT",
    /* [29] = */ "TOUCHPAD",
    /* [30] = */ "RWSIG",
    /* [31] = */ "DEVICE_EVENT",
    /* [32] = */ "UNIFIED_WAKE_MASKS",
    /* [33] = */ "HOST_EVENT64",
    /* [34] = */ "EXEC_IN_RAM",
    /* [35] = */ "CEC",
    /* [36] = */ "MOTION_SENSE_TIGHT_TIMESTAMPS",
    /* [37] = */ "REFINED_TABLET_MODE_HYSTERESIS",
    /* [38] = */ "EFS2",
    /* [39] = */ "SCP",
    /* [40] = */ "ISH",
    /* [41] = */ "TYPEC_CMD",
    /* [42] = */ "TYPEC_REQUIRE_AP_MODE_ENTRY",
    /* [43] = */ "TYPEC_MUX_REQUIRE_AP_ACK",
};

}  // namespace

zx_status_t ChromiumosEcCore::Bind(void* ctx, zx_device_t* dev) {
  auto device = std::make_unique<ChromiumosEcCore>(dev);

  zx_status_t status = device->Bind();
  if (status == ZX_OK) {
    // Release ownership of the device to the DDK.
    __UNUSED auto unused = device.release();
  }

  return status;
}

zx_status_t ChromiumosEcCore::Bind() {
  zx_status_t st = loop_.StartThread("cros-ec-core-fidl");
  if (st != ZX_OK) {
    return st;
  }

  // NON_BINDABLE because we will manually add children based on available features.
  return DdkAdd(ddk::DeviceAddArgs("chromiumos_ec_core")
                    .set_inspect_vmo(inspect_.DuplicateVmo())
                    .set_flags(DEVICE_ADD_NON_BINDABLE));
}

void ChromiumosEcCore::DdkInit(ddk::InitTxn txn) {
  auto ec_endpoints = fidl::CreateEndpoints<fuchsia_hardware_google_ec::Device>();
  if (ec_endpoints.is_error()) {
    txn.Reply(ec_endpoints.status_value());
    return;
  }
  zx_status_t result = DdkConnectFidlProtocol(std::move(ec_endpoints->server));
  if (result != ZX_OK) {
    txn.Reply(result);
    return;
  }

  auto acpi_endpoints = fidl::CreateEndpoints<fuchsia_hardware_acpi::Device>();
  if (acpi_endpoints.is_error()) {
    txn.Reply(acpi_endpoints.status_value());
    return;
  }

  result = DdkConnectFidlProtocol(std::move(acpi_endpoints->server));
  if (result != ZX_OK) {
    txn.Reply(result);
    return;
  }

  init_txn_ = std::move(txn);
  auto promise =
      BindFidlClients(std::move(ec_endpoints->client), std::move(acpi_endpoints->client))
          .then([this](fpromise::result<void, zx_status_t>& result)
                    -> fpromise::promise<CommandResult, zx_status_t> {
            if (result.is_error()) {
              return fpromise::make_result_promise<CommandResult, zx_status_t>(
                  fpromise::error(result.error()));
            }
            return IssueCommand(EC_CMD_GET_FEATURES, 0);
          })
          .and_then([this](CommandResult& result) mutable -> fpromise::result<void, zx_status_t> {
            ec_response_get_features* features = result.GetData<ec_response_get_features>();
            if (features == nullptr) {
              zxlogf(ERROR, "Did not get enough bytes for GET_FEATURE");
              init_txn_->Reply(ZX_ERR_BUFFER_TOO_SMALL);
              return fpromise::error(ZX_ERR_BUFFER_TOO_SMALL);
            }

            features_ = *features;
            std::string feature_str;
            for (size_t i = 0; i < std::size(kEcFeatureNames); i++) {
              if (HasFeature(i)) {
                if (!feature_str.empty()) {
                  feature_str += ", ";
                }
                feature_str += kEcFeatureNames[i];
              }
            }

            core_.CreateString(kPropFeatures, feature_str, &inspect_);

            // Bind child drivers.
            BindSubdrivers(this);

            init_txn_->Reply(ZX_OK);
            return fpromise::ok();
          })
          .and_then([this]() { ScheduleInspectCommands(); })
          .or_else([this](zx_status_t& status) {
            zxlogf(ERROR, "IssueCommand failed: %s", zx_status_get_string(status));
            init_txn_->Reply(status);
          });

  executor_.schedule_task(std::move(promise));
}

void ChromiumosEcCore::DdkUnbind(ddk::UnbindTxn txn) {
  if (acpi_client_.is_valid()) {
    acpi_client_.AsyncTeardown();
  } else {
    acpi_teardown_.completer.complete_ok();
  }
  if (ec_client_.is_valid()) {
    ec_client_.AsyncTeardown();
  } else {
    ec_teardown_.completer.complete_ok();
  }
  if (notify_ref_.has_value()) {
    notify_ref_->Close(ZX_ERR_CANCELED);
  } else {
    server_teardown_.completer.complete_ok();
  }
  // Once both clients have been torn down, reply.
  executor_.schedule_task(
      fpromise::join_promises(ec_teardown_.consumer.promise(), acpi_teardown_.consumer.promise(),
                              server_teardown_.consumer.promise())
          .discard_result()
          .then([unbind = std::move(txn)](fpromise::result<>& type) mutable { unbind.Reply(); }));
}

fpromise::promise<void, zx_status_t> ChromiumosEcCore::BindFidlClients(
    fidl::ClientEnd<fuchsia_hardware_google_ec::Device> ec_client,
    fidl::ClientEnd<fuchsia_hardware_acpi::Device> acpi_client) {
  fpromise::bridge<void, zx_status_t> bridge;
  async::PostTask(loop_.dispatcher(), [this, ec_client = std::move(ec_client),
                                       acpi_client = std::move(acpi_client),
                                       completer = std::move(bridge.completer)]() mutable {
    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_acpi::NotifyHandler>();
    if (!endpoints.is_ok()) {
      completer.complete_error(endpoints.status_value());
    }
    notify_ref_ = fidl::BindServer<fidl::WireServer<fuchsia_hardware_acpi::NotifyHandler>>(
        loop_.dispatcher(), std::move(endpoints->server), this,
        [this](fidl::WireServer<fuchsia_hardware_acpi::NotifyHandler>*, fidl::UnbindInfo,
               fidl::ServerEnd<fuchsia_hardware_acpi::NotifyHandler>) {
          server_teardown_.completer.complete_ok();
        });
    ec_client_.Bind(std::move(ec_client), loop_.dispatcher(),
                    fidl::ObserveTeardown([this]() { ec_teardown_.completer.complete_ok(); }));
    acpi_client_.Bind(std::move(acpi_client), loop_.dispatcher(),
                      fidl::ObserveTeardown([this]() { acpi_teardown_.completer.complete_ok(); }));
    acpi_client_
        ->InstallNotifyHandler(fuchsia_hardware_acpi::wire::NotificationMode::kDevice,
                               std::move(endpoints->client))
        .ThenExactlyOnce(
            [completer = std::move(completer)](
                fidl::WireUnownedResult<fuchsia_hardware_acpi::Device::InstallNotifyHandler>&
                    result) mutable {
              if (!result.ok()) {
                zxlogf(ERROR, "Failed to install notify handler: %s",
                       result.FormatDescription().data());
                completer.complete_error(result.status());
              } else if (result->result.is_err()) {
                zxlogf(ERROR, "Failed to install notify handler: %u", int(result->result.err()));
                completer.complete_error(ZX_ERR_INTERNAL);
              } else {
                completer.complete_ok();
              }
            });
  });

  return bridge.consumer.promise();
}

void ChromiumosEcCore::DdkRelease() {
  loop_.Shutdown();
  delete this;
}

void ChromiumosEcCore::Handle(HandleRequestView request, HandleCompleter::Sync& completer) {
  std::scoped_lock lock(callback_lock_);
  for (auto& callback : callbacks_) {
    callback(request->value);
  }

  completer.Reply();
}

bool ChromiumosEcCore::HasFeature(size_t feature) {
  if (feature < 32) {
    return features_.flags[0] & EC_FEATURE_MASK_0(feature);
  }
  if (feature < 64) {
    return features_.flags[1] & EC_FEATURE_MASK_1(feature);
  }
  zxlogf(ERROR, "Unknown feature %zu", feature);
  return false;
}

void ChromiumosEcCore::ScheduleInspectCommands() {
  executor_.schedule_task(
      IssueCommand(EC_CMD_GET_VERSION, 0).and_then([this](CommandResult& result) {
        auto version = result.GetData<ec_response_get_version>();
        if (version == nullptr) {
          zxlogf(ERROR, "GET_VERSION response was too short (0x%lx, want 0x%lx)",
                 result.data.size(), sizeof(*version));
          return;
        }

        core_.CreateString(kPropVersionRo, std::string(version->version_string_ro), &inspect_);
        core_.CreateString(kPropVersionRw, std::string(version->version_string_rw), &inspect_);
        core_.CreateUint(kPropCurrentImage, version->current_image, &inspect_);
      }));

  executor_.schedule_task(
      IssueCommand(EC_CMD_GET_BUILD_INFO, 0).and_then([this](CommandResult& result) {
        core_.CreateString(
            kPropBuildInfo,
            std::string(reinterpret_cast<char*>(result.data.data()), result.data.size()),
            &inspect_);
      }));

  executor_.schedule_task(
      IssueCommand(EC_CMD_GET_CHIP_INFO, 0).and_then([this](CommandResult& result) {
        auto chip_info = result.GetData<ec_response_get_chip_info>();
        if (chip_info == nullptr) {
          zxlogf(ERROR, "GET_CHIP_INFO response was too short");
          return;
        }

        core_.CreateString(kPropChipVendor, chip_info->vendor, &inspect_);
        core_.CreateString(kPropChipName, chip_info->name, &inspect_);
        core_.CreateString(kPropChipRevision, chip_info->revision, &inspect_);
      }));

  executor_.schedule_task(
      IssueCommand(EC_CMD_GET_BOARD_VERSION, 0).and_then([this](CommandResult& result) {
        auto board_version = result.GetData<ec_response_board_version>();
        if (board_version == nullptr) {
          zxlogf(ERROR, "GET_BOARD_VERSION response was too short");
          return;
        }

        core_.CreateUint(kPropBoardVersion, board_version->board_version, &inspect_);
      }));
}

fpromise::promise<CommandResult, zx_status_t> ChromiumosEcCore::IssueRawCommand(
    uint16_t command, uint8_t version, cpp20::span<uint8_t> input) {
  fpromise::bridge<CommandResult, zx_status_t> result;
  ec_client_
      ->RunCommand(command, version,
                   fidl::VectorView<uint8_t>::FromExternal(input.data(), input.size()))
      .ThenExactlyOnce([command, version, completer = std::move(result.completer)](
                           fidl::WireUnownedResult<fuchsia_hardware_google_ec::Device::RunCommand>&
                               response) mutable {
        if (!response.ok()) {
          zxlogf(ERROR, "Failed to send FIDL for EC command %u version %u: %s", command, version,
                 response.FormatDescription().data());
          completer.complete_error(response.status());
          return;
        }

        if (response->result.is_err()) {
          zxlogf(ERROR, "Failed to execute EC command %u version %u: %s", command, version,
                 zx_status_get_string(response->result.err()));
          completer.complete_error(response->result.err());
          return;
        }

        CommandResult ret;
        auto& view = response->result.response().data;
        ret.data.assign(view.begin(), view.end());

        ret.status = response->result.response().result;
        completer.complete_ok(std::move(ret));
      });

  return result.consumer.promise();
}

static zx_driver_ops_t chromiumos_ec_core_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = ChromiumosEcCore::Bind,
};

}  // namespace chromiumos_ec_core

// clang-format off
ZIRCON_DRIVER(chromiumos-ec-core, chromiumos_ec_core::chromiumos_ec_core_driver_ops, "zircon", "0.1");
