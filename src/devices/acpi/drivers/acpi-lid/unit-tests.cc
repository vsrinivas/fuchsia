// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.acpi/cpp/markers.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/mock-hidbus-ifc/mock-hidbus-ifc.h>
#include <lib/sync/completion.h>

#include <memory>

#include <zxtest/zxtest.h>

#include "src/devices/acpi/drivers/acpi-lid/acpi_lid.h"
#include "src/devices/lib/acpi/mock/mock-acpi.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace acpi_lid {
namespace facpi = fuchsia_hardware_acpi::wire;

class AcpiLidTest : public zxtest::Test {
 public:
  AcpiLidTest() : loop_(&kAsyncLoopConfigNeverAttachToThread) {}
  using AcpiDevice = acpi::mock::Device;
  void SetUp() override {
    fake_root_ = MockDevice::FakeRootParent();
    ASSERT_OK(loop_.StartThread("acpi-lid-test-fidl"));
    ASSERT_OK(loop_.StartThread("acpi-lid-test-fidl"));

    fake_acpi_.SetEvaluateObject(
        [this](auto request, auto& completer) { EvaluateObject(request, completer); });
    fake_acpi_.SetInstallNotifyHandler(
        [this](auto request, auto& completer) { InstallNotifyHandler(request, completer); });
    fake_acpi_.SetRemoveNotifyHandler([this](auto& completer) { RemoveNotifyHandler(completer); });
    fake_acpi_.SetSetWakeDevice(
        [this](auto request, auto& completer) { SetWakeDevice(request, completer); });
  }

  void EvaluateObject(AcpiDevice::EvaluateObjectRequestView request,
                      AcpiDevice::EvaluateObjectCompleter::Sync& completer) {
    ASSERT_EQ(request->mode, facpi::EvaluateObjectMode::kPlainObject);
    std::string object(request->path.data(), request->path.size());
    ASSERT_EQ(object, "_LID");
    fidl::Arena<> arena;
    completer.ReplySuccess(
        facpi::EncodedObject::WithObject(arena, facpi::Object::WithIntegerVal(arena, lid_open_)));
    sync_completion_signal(&eval_lid_called_);
  }

  void InstallNotifyHandler(AcpiDevice::InstallNotifyHandlerRequestView request,
                            AcpiDevice::InstallNotifyHandlerCompleter::Sync& completer) {
    ASSERT_EQ(request->mode, facpi::NotificationMode::kDevice);
    ASSERT_FALSE(notify_client_.is_valid());
    notify_client_.Bind(std::move(request->handler));
    completer.ReplySuccess();
  }

  void RemoveNotifyHandler(AcpiDevice::RemoveNotifyHandlerCompleter::Sync& completer) {
    ASSERT_TRUE(notify_client_.is_valid());
    notify_client_ = {};
    completer.ReplySuccess();
  }

  void SetWakeDevice(AcpiDevice::SetWakeDeviceRequestView request,
                     AcpiDevice::SetWakeDeviceCompleter::Sync& completer) {
    sync_completion_signal(&wake_device_set);
    completer.ReplySuccess();
  }

  void CreateAndBindLidDevice(bool lid_open) {
    lid_open_ = lid_open;
    auto client = fake_acpi_.CreateClient(loop_.dispatcher());
    ASSERT_OK(client.status_value());

    auto acpi_lid =
        std::make_unique<AcpiLid>(fake_root_.get(), std::move(client.value()), loop_.dispatcher());
    ASSERT_OK(acpi_lid->Bind());
    lid_ptr_ = acpi_lid.release();
  }

  void InitLidDevice() {
    lid_ptr_->zxdev()->InitOp();
    ASSERT_OK(lid_ptr_->zxdev()->WaitUntilInitReplyCalled(zx::time::infinite()));
    device_ = lid_ptr_->zxdev();
  }

  void SetUpLidDevice(bool lid_open) {
    CreateAndBindLidDevice(lid_open);
    InitLidDevice();
  }

  void TearDownLidDevice() {
    device_async_remove(device_);
    ASSERT_OK(mock_ddk::ReleaseFlaggedDevices(fake_root_.get()));
    device_ = nullptr;
    lid_ptr_ = nullptr;
  }

 protected:
  std::shared_ptr<zx_device> fake_root_;
  AcpiDevice fake_acpi_;
  async::Loop loop_;
  AcpiLid* lid_ptr_;
  zx_device* device_;
  bool lid_open_ = true;
  sync_completion_t eval_lid_called_;
  sync_completion_t wake_device_set;

  fidl::WireSyncClient<fuchsia_hardware_acpi::NotifyHandler> notify_client_;
};

TEST_F(AcpiLidTest, TestInitOpen) {
  CreateAndBindLidDevice(/*lid_open=*/true);
  ASSERT_EQ(lid_ptr_->State(), LidState::kUnknown);

  InitLidDevice();
  ASSERT_EQ(lid_ptr_->State(), LidState::kOpen);

  TearDownLidDevice();
}

TEST_F(AcpiLidTest, TestInitClosed) {
  CreateAndBindLidDevice(/*lid_open=*/false);
  ASSERT_EQ(lid_ptr_->State(), LidState::kUnknown);

  InitLidDevice();
  ASSERT_EQ(lid_ptr_->State(), LidState::kClosed);

  TearDownLidDevice();
}

TEST_F(AcpiLidTest, TestSetWakeDevice) {
  SetUpLidDevice(/*lid_open=*/true);

  sync_completion_reset(&wake_device_set);
  lid_ptr_->zxdev()->SuspendNewOp(/*requested_state=*/3, /*enable_wake=*/true,
                                  /*suspend_reason=*/DEVICE_SUSPEND_REASON_SUSPEND_RAM);
  sync_completion_wait_deadline(&wake_device_set, ZX_TIME_INFINITE);
  loop_.RunUntilIdle();
  ASSERT_TRUE(sync_completion_signaled(&wake_device_set));

  TearDownLidDevice();
}

TEST_F(AcpiLidTest, TestHidStartStop) {
  SetUpLidDevice(/*lid_open=*/true);

  mock_hidbus_ifc::MockHidbusIfc<uint8_t> mock_ifc;

  ASSERT_OK(lid_ptr_->HidbusStart(mock_ifc.proto()));
  ASSERT_TRUE(notify_client_.is_valid());

  lid_ptr_->HidbusStop();
  ASSERT_FALSE(notify_client_.is_valid());

  TearDownLidDevice();
}

TEST_F(AcpiLidTest, TestHidbusQuery) {
  SetUpLidDevice(/*lid_open=*/false);

  uint32_t options = 0;
  hid_info_t info;
  ASSERT_OK(lid_ptr_->HidbusQuery(options, &info));
  ASSERT_EQ(info.dev_num, 0);
  ASSERT_EQ(info.device_class, HID_DEVICE_CLASS_OTHER);
  ASSERT_EQ(info.boot_device, false);

  TearDownLidDevice();
}

TEST_F(AcpiLidTest, TestHidGetDescriptor) {
  SetUpLidDevice(/*lid_open=*/false);

  uint8_t data[HID_MAX_DESC_LEN];
  size_t len;
  ASSERT_OK(lid_ptr_->HidbusGetDescriptor(HID_DESCRIPTION_TYPE_REPORT, data, sizeof(data), &len));
  ASSERT_EQ(len, AcpiLid::kHidDescriptorLen);
  EXPECT_BYTES_EQ(data, AcpiLid::kHidDescriptor, AcpiLid::kHidDescriptorLen);

  TearDownLidDevice();
}

TEST_F(AcpiLidTest, TestHidGetReportLidOpen) {
  SetUpLidDevice(/*lid_open=*/true);

  uint8_t report;
  size_t copied_len;
  lid_ptr_->HidbusGetReport(HID_REPORT_TYPE_INPUT, 0, &report, sizeof(report), &copied_len);
  ASSERT_EQ(copied_len, 1U);
  EXPECT_EQ(report, 1U);

  TearDownLidDevice();
}

TEST_F(AcpiLidTest, TestHidGetReportLidClosed) {
  SetUpLidDevice(/*lid_open=*/false);

  uint8_t report;
  size_t copied_len;
  lid_ptr_->HidbusGetReport(HID_REPORT_TYPE_INPUT, 0, &report, sizeof(report), &copied_len);
  ASSERT_EQ(copied_len, 1U);
  EXPECT_EQ(report, 0U);

  TearDownLidDevice();
}

TEST_F(AcpiLidTest, TestUpdateState) {
  SetUpLidDevice(/*lid_open=*/true);
  ASSERT_EQ(lid_ptr_->State(), LidState::kOpen);

  mock_hidbus_ifc::MockHidbusIfc<uint8_t> mock_ifc;
  ASSERT_OK(lid_ptr_->HidbusStart(mock_ifc.proto()));

  // Simulate an ACPI state change notification
  lid_open_ = false;
  sync_completion_reset(&eval_lid_called_);
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)notify_client_->Handle(kLidStateChange);
  sync_completion_wait_deadline(&eval_lid_called_, ZX_TIME_INFINITE);
  loop_.RunUntilIdle();
  ASSERT_OK(mock_ifc.WaitForReports(1));
  uint8_t report = *mock_ifc.reports().begin();
  ASSERT_EQ(report, 0U);
  ASSERT_EQ(lid_ptr_->State(), LidState::kClosed);

  mock_ifc.Reset();

  // Simulate a spurious ACPI state change notification (without an actual
  // state change). No HID reports are expected.
  sync_completion_reset(&eval_lid_called_);
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)notify_client_->Handle(kLidStateChange);
  sync_completion_wait_deadline(&eval_lid_called_, ZX_TIME_INFINITE);
  loop_.RunUntilIdle();
  ASSERT_OK(mock_ifc.WaitForReports(0));
  ASSERT_EQ(lid_ptr_->State(), LidState::kClosed);

  // Simulate an ACPI state change notification
  lid_open_ = true;
  sync_completion_reset(&eval_lid_called_);
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)notify_client_->Handle(kLidStateChange);
  sync_completion_wait_deadline(&eval_lid_called_, ZX_TIME_INFINITE);
  loop_.RunUntilIdle();
  ASSERT_OK(mock_ifc.WaitForReports(1));
  report = *mock_ifc.reports().begin();
  ASSERT_EQ(report, 1U);
  ASSERT_EQ(lid_ptr_->State(), LidState::kOpen);

  lid_ptr_->HidbusStop();
  TearDownLidDevice();
}

}  // namespace acpi_lid
