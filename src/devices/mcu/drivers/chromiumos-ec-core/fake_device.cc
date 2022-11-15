// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/mcu/drivers/chromiumos-ec-core/fake_device.h"

#include <fidl/fuchsia.hardware.google.ec/cpp/wire_test_base.h>
#include <lib/ddk/debug.h>
#include <lib/sys/component/cpp/handlers.h>

#include <memory>

#include <zxtest/zxtest.h>

#include "src/devices/lib/acpi/mock/mock-acpi.h"

namespace chromiumos_ec_core {

void ChromiumosEcTestBase::SetUp() {
  fake_root_ = MockDevice::FakeRootParent();
  ASSERT_OK(loop_.StartThread("chromiumos-ec-core-test"));
  fake_acpi_.SetInstallNotifyHandler(
      [this](acpi::mock::Device::InstallNotifyHandlerRequestView request, auto& completer) {
        ASSERT_FALSE(handler_.is_valid());
        handler_ = std::move(request->handler);
        completer.ReplySuccess();
      });
}

void ChromiumosEcTestBase::InitDevice() {
  device_ = new ChromiumosEcCore(fake_root_.get());
  ASSERT_OK(device_->Bind());

  component::ServiceInstanceHandler handler;
  fuchsia_hardware_google_ec::Service::Handler ec_handler(&handler);
  auto ec_connector = [this](fidl::ServerEnd<fuchsia_hardware_google_ec::Device> server) {
    ec_binding_ = fidl::BindServer(loop_.dispatcher(), std::move(server), &fake_ec_,
                                   [this](FakeEcDevice*, fidl::UnbindInfo,
                                          fidl::ServerEnd<fuchsia_hardware_google_ec::Device>) {
                                     sync_completion_signal(&ec_shutdown_);
                                   });
  };

  ASSERT_OK(ec_handler.add_device(std::move(ec_connector)).status_value());
  ASSERT_OK(
      outgoing_.AddService<fuchsia_hardware_google_ec::Service>(std::move(handler)).status_value());

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_OK(endpoints.status_value());
  ASSERT_OK(outgoing_.Serve(std::move(endpoints->server)).status_value());
  fake_root_->AddFidlService(fuchsia_hardware_google_ec::Service::Name,
                             std::move(endpoints->client));

  handler = {};
  fuchsia_hardware_acpi::Service::Handler acpi_handler(&handler);
  auto acpi_connector = [this](fidl::ServerEnd<fuchsia_hardware_acpi::Device> server) {
    acpi_binding_ = fidl::BindServer(loop_.dispatcher(), std::move(server), &fake_acpi_,
                                     [this](acpi::mock::Device*, fidl::UnbindInfo,
                                            fidl::ServerEnd<fuchsia_hardware_acpi::Device>) {
                                       sync_completion_signal(&acpi_shutdown_);
                                     });
  };

  ASSERT_OK(acpi_handler.add_device(std::move(acpi_connector)).status_value());
  ASSERT_OK(
      outgoing_.AddService<fuchsia_hardware_acpi::Service>(std::move(handler)).status_value());

  endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_OK(endpoints.status_value());
  ASSERT_OK(outgoing_.Serve(std::move(endpoints->server)).status_value());
  fake_root_->AddFidlService(fuchsia_hardware_acpi::Service::Name, std::move(endpoints->client));

  device_->zxdev()->InitOp();
  ASSERT_OK(device_->zxdev()->WaitUntilInitReplyCalled(zx::time::infinite()));
  initialised_ = true;
}

void ChromiumosEcTestBase::TearDown() {
  ASSERT_TRUE(initialised_);
  device_->DdkAsyncRemove();
  ASSERT_OK(mock_ddk::ReleaseFlaggedDevices(fake_root_.get()));

  if (acpi_binding_) {
    acpi_binding_->Close(ZX_ERR_CANCELED);
  }
  if (ec_binding_) {
    ec_binding_->Close(ZX_ERR_CANCELED);
  }
  if (ec_binding_) {
    sync_completion_wait(&ec_shutdown_, ZX_TIME_INFINITE);
  }
  if (acpi_binding_) {
    sync_completion_wait(&acpi_shutdown_, ZX_TIME_INFINITE);
  }
  loop_.Shutdown();
}

void FakeEcDevice::NotImplemented_(const std::string& name, fidl::CompleterBase& completer) {
  zxlogf(ERROR, "%s: not implemented", name.data());
  completer.Close(ZX_ERR_NOT_SUPPORTED);
}

void FakeEcDevice::RunCommand(RunCommandRequestView request, RunCommandCompleter::Sync& completer) {
  if (request->command == EC_CMD_GET_FEATURES) {
    completer.ReplySuccess(fuchsia_hardware_google_ec::wire::EcStatus::kSuccess,
                           MakeVectorView(features_));
    return;
  }

  if (request->command == EC_CMD_GET_VERSION) {
    ec_response_get_version response = {
        .reserved = "",
        .current_image = 1234,
    };
    std::strcpy(response.version_string_ro, (board_ + "1234").c_str());
    std::strcpy(response.version_string_rw, (board_ + "1234").c_str());

    completer.ReplySuccess(fuchsia_hardware_google_ec::wire::EcStatus::kSuccess,
                           MakeVectorView(response));
    return;
  }

  auto command = commands_.find(MakeKey(request->command, request->command_version));
  if (command == commands_.end()) {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
    return;
  }

  command->second(static_cast<const void*>(request->request.data()), request->request.count(),
                  completer);
}

}  // namespace chromiumos_ec_core
