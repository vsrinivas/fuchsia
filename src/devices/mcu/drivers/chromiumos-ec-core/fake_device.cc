// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/mcu/drivers/chromiumos-ec-core/fake_device.h"

#include <fidl/fuchsia.hardware.google.ec/cpp/wire_test_base.h>
#include <lib/ddk/debug.h>

#include <memory>

#include <zxtest/zxtest.h>

#include "src/devices/lib/acpi/mock/mock-acpi.h"

namespace chromiumos_ec_core {

void ChromiumosEcTestBase::SetUp() {
  fake_root_ = MockDevice::FakeRootParent();
  ASSERT_OK(loop_.StartThread("chromiumos-ec-core-test"));
}

void ChromiumosEcTestBase::InitDevice() {
  device_ = new ChromiumosEcCore(fake_root_.get());
  ASSERT_OK(device_->Bind());
  fake_root_->AddFidlProtocol(
      fidl::DiscoverableProtocolName<fuchsia_hardware_google_ec::Device>, [this](zx::channel chan) {
        fidl::ServerEnd<fuchsia_hardware_google_ec::Device> server(std::move(chan));
        ec_binding_ = fidl::BindServer(loop_.dispatcher(), std::move(server), &fake_ec_,
                                       [this](FakeEcDevice*, fidl::UnbindInfo,
                                              fidl::ServerEnd<fuchsia_hardware_google_ec::Device>) {
                                         sync_completion_signal(&ec_shutdown_);
                                       });
        return ZX_OK;
      });

  fake_root_->AddFidlProtocol(
      fidl::DiscoverableProtocolName<fuchsia_hardware_acpi::Device>, [this](zx::channel chan) {
        fidl::ServerEnd<fuchsia_hardware_acpi::Device> server(std::move(chan));
        acpi_binding_ = fidl::BindServer(loop_.dispatcher(), std::move(server), &fake_acpi_,
                                         [this](acpi::mock::Device*, fidl::UnbindInfo,
                                                fidl::ServerEnd<fuchsia_hardware_acpi::Device>) {
                                           sync_completion_signal(&acpi_shutdown_);
                                         });
        return ZX_OK;
      });
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
  auto command = commands_.find(MakeKey(request->command, request->command_version));
  if (command == commands_.end()) {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
    return;
  }

  command->second(static_cast<const void*>(request->request.data()), request->request.count(),
                  completer);
}

}  // namespace chromiumos_ec_core
