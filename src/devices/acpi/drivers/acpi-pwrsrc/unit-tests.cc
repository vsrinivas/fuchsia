// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.acpi/cpp/markers.h>
#include <fidl/fuchsia.hardware.power/cpp/markers.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sync/completion.h>

#include <memory>

#include <zxtest/zxtest.h>

#include "src/devices/acpi/drivers/acpi-pwrsrc/acpi_pwrsrc.h"
#include "src/devices/lib/acpi/mock/mock-acpi.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace acpi_pwrsrc {
namespace facpi = fuchsia_hardware_acpi::wire;

class AcpiPwrsrcTest : public zxtest::Test {
 public:
  AcpiPwrsrcTest() : loop_(&kAsyncLoopConfigNeverAttachToThread) {}
  using AcpiDevice = acpi::mock::Device;
  void SetUp() override {
    fake_root_ = MockDevice::FakeRootParent();
    ASSERT_OK(loop_.StartThread("intel-thermal-test-fidl"));
    ASSERT_OK(loop_.StartThread("intel-thermal-test-fidl"));

    fake_acpi_.SetEvaluateObject(
        [this](auto request, auto& completer) { EvaluateObject(request, completer); });
    fake_acpi_.SetInstallNotifyHandler(
        [this](auto request, auto& completer) { InstallNotifyHandler(request, completer); });

    auto client = fake_acpi_.CreateClient(loop_.dispatcher());
    ASSERT_OK(client.status_value());

    auto pwrsrc = std::make_unique<AcpiPwrsrc>(fake_root_.get(), std::move(client.value()),
                                               loop_.dispatcher());
    ASSERT_OK(pwrsrc->Bind());
    AcpiPwrsrc* ptr = pwrsrc.release();

    ptr->zxdev()->InitOp();
    ASSERT_OK(ptr->zxdev()->WaitUntilInitReplyCalled(zx::time::infinite()));
    device_ = ptr->zxdev();

    // Start the FIDL server.
    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_power::Source>();
    ASSERT_OK(endpoints.status_value());

    fidl::BindServer<fidl::WireServer<fuchsia_hardware_power::Source>>(
        loop_.dispatcher(), std::move(endpoints->server), ptr);
    source_client_.Bind(std::move(endpoints->client));
  }
  void TearDown() override {
    device_async_remove(device_);
    ASSERT_OK(mock_ddk::ReleaseFlaggedDevices(fake_root_.get()));
  }

  void EvaluateObject(AcpiDevice::EvaluateObjectRequestView request,
                      AcpiDevice::EvaluateObjectCompleter::Sync& completer) {
    ASSERT_EQ(request->mode, facpi::EvaluateObjectMode::kPlainObject);
    std::string object(request->path.data(), request->path.size());
    ASSERT_EQ(object, "_PSR");
    fidl::Arena<> arena;
    completer.ReplySuccess(
        facpi::EncodedObject::WithObject(arena, facpi::Object::WithIntegerVal(arena, online_)));
    sync_completion_signal(&psr_called_);
  }

  void InstallNotifyHandler(AcpiDevice::InstallNotifyHandlerRequestView request,
                            AcpiDevice::InstallNotifyHandlerCompleter::Sync& completer) {
    ASSERT_EQ(request->mode, facpi::NotificationMode::kDevice);
    ASSERT_FALSE(notify_client_.is_valid());
    notify_client_.Bind(std::move(request->handler));
    completer.ReplySuccess();
  }

 protected:
  std::shared_ptr<zx_device> fake_root_;
  AcpiDevice fake_acpi_;
  async::Loop loop_;
  zx_device* device_;
  bool online_ = false;
  sync_completion_t psr_called_;

  fidl::WireSyncClient<fuchsia_hardware_power::Source> source_client_;
  fidl::WireSyncClient<fuchsia_hardware_acpi::NotifyHandler> notify_client_;
};

TEST_F(AcpiPwrsrcTest, TestGetInfo) {
  auto info = source_client_->GetPowerInfo();
  ASSERT_OK(info.status());
  ASSERT_OK(info->status);
  ASSERT_EQ(info->info.type, fuchsia_hardware_power::wire::PowerType::kAc);
  ASSERT_EQ(info->info.state, 0);
}

TEST_F(AcpiPwrsrcTest, TestNotify) {
  auto event = source_client_->GetStateChangeEvent();
  ASSERT_OK(event.status());
  ASSERT_OK(event->status);

  ASSERT_STATUS(event->handle.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite_past(), nullptr),
                ZX_ERR_TIMED_OUT);

  // Try a spurious notification, where the state doesn't actually change.
  sync_completion_reset(&psr_called_);
  notify_client_->Handle(kPowerSourceStateChanged);

  sync_completion_wait_deadline(&psr_called_, ZX_TIME_INFINITE);
  loop_.RunUntilIdle();
  ASSERT_STATUS(event->handle.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite_past(), nullptr),
                ZX_ERR_TIMED_OUT);

  online_ = true;
  notify_client_->Handle(kPowerSourceStateChanged);
  ASSERT_OK(event->handle.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), nullptr));

  // And calling GetPowerInfo should clear the event.
  auto info = source_client_->GetPowerInfo();
  ASSERT_OK(info.status());
  ASSERT_OK(info->status);
  ASSERT_STATUS(event->handle.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite_past(), nullptr),
                ZX_ERR_TIMED_OUT);
}

}  // namespace acpi_pwrsrc
