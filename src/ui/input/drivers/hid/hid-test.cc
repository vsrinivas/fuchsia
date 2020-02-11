// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hid.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <unistd.h>

#include <thread>
#include <vector>

#include <ddktl/protocol/hidbus.h>
#include <hid/ambient-light.h>
#include <hid/boot.h>
#include <hid/paradise.h>
#include <zxtest/zxtest.h>

namespace hid_driver {

struct ProtocolDeviceOps {
  const zx_protocol_device_t* ops;
  void* ctx;
};

// Create our own Fake Ddk Bind class. We want to save the last device arguments that
// have been seen, so the test can get ahold of the instance device and test
// Reads and Writes on it.
class Binder : public fake_ddk::Bind {
 public:
  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    zx_status_t status;

    if (args && args->ops) {
      if (args->ops->message) {
        if ((status = fidl_.SetMessageOp(args->ctx, args->ops->message)) < 0) {
          return status;
        }
      }
    }

    *out = fake_ddk::kFakeDevice;
    add_called_ = true;

    last_ops_.ctx = args->ctx;
    last_ops_.ops = args->ops;

    return ZX_OK;
  }

  ProtocolDeviceOps GetLastDeviceOps() { return last_ops_; }

 private:
  ProtocolDeviceOps last_ops_;
};

class FakeHidbus : public ddk::HidbusProtocol<FakeHidbus> {
 public:
  FakeHidbus() : proto_({&hidbus_protocol_ops_, this}) {}

  zx_status_t HidbusQuery(uint32_t options, hid_info_t* out_info) {
    *out_info = info_;
    return ZX_OK;
  }

  void SetHidInfo(hid_info_t info) { info_ = info; }

  void SetStartStatus(zx_status_t status) { start_status_ = status; }

  zx_status_t HidbusStart(const hidbus_ifc_protocol_t* ifc) {
    if (start_status_ != ZX_OK) {
      return start_status_;
    }
    ifc_ = *ifc;
    return ZX_OK;
  }

  void SendReport(const uint8_t* report_data, size_t report_size) {
    ASSERT_NE(ifc_.ops, nullptr);
    ifc_.ops->io_queue(ifc_.ctx, report_data, report_size, zx_clock_get_monotonic());
  }

  void SendReportWithTime(const uint8_t* report_data, size_t report_size, zx_time_t time) {
    ASSERT_NE(ifc_.ops, nullptr);
    ifc_.ops->io_queue(ifc_.ctx, report_data, report_size, time);
  }

  void HidbusStop() { ifc_.ops = nullptr; }

  zx_status_t HidbusGetDescriptor(hid_description_type_t desc_type, void* out_data_buffer,
                                  size_t data_size, size_t* out_data_actual) {
    if (data_size < report_desc_.size()) {
      return ZX_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(out_data_buffer, report_desc_.data(), report_desc_.size());
    *out_data_actual = report_desc_.size();

    return ZX_OK;
  }

  void SetDescriptor(const uint8_t* desc, size_t desc_len) {
    report_desc_ = std::vector<uint8_t>(desc, desc + desc_len);
  }

  zx_status_t HidbusGetReport(hid_report_type_t rpt_type, uint8_t rpt_id, void* out_data_buffer,
                              size_t data_size, size_t* out_data_actual) {
    if (rpt_id != last_set_report_id_) {
      return ZX_ERR_INTERNAL;
    }
    if (data_size < last_set_report_.size()) {
      return ZX_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(out_data_buffer, last_set_report_.data(), last_set_report_.size());
    *out_data_actual = last_set_report_.size();

    return ZX_OK;
  }

  zx_status_t HidbusSetReport(hid_report_type_t rpt_type, uint8_t rpt_id, const void* data_buffer,
                              size_t data_size) {
    last_set_report_id_ = rpt_id;
    auto data_bytes = reinterpret_cast<const uint8_t*>(data_buffer);
    last_set_report_ = std::vector<uint8_t>(data_bytes, data_bytes + data_size);

    return ZX_OK;
  }

  zx_status_t HidbusGetIdle(uint8_t rpt_id, uint8_t* out_duration) {
    *out_duration = 0;
    return ZX_OK;
  }

  zx_status_t HidbusSetIdle(uint8_t rpt_id, uint8_t duration) { return ZX_OK; }

  zx_status_t HidbusGetProtocol(hid_protocol_t* out_protocol) {
    *out_protocol = hid_protocol_;
    return ZX_OK;
  }

  zx_status_t HidbusSetProtocol(hid_protocol_t protocol) {
    hid_protocol_ = protocol;
    return ZX_OK;
  }

  hidbus_protocol_t* GetProto() { return &proto_; }

 protected:
  std::vector<uint8_t> report_desc_;

  std::vector<uint8_t> last_set_report_;
  uint8_t last_set_report_id_;

  hid_protocol_t hid_protocol_ = HID_PROTOCOL_REPORT;
  hidbus_protocol_t proto_ = {};
  hid_info_t info_ = {};
  hidbus_ifc_protocol_t ifc_;
  zx_status_t start_status_ = ZX_OK;
};

class HidDeviceTest : public zxtest::Test {
 public:
  void SetUp() override {
    client_ = ddk::HidbusProtocolClient(fake_hidbus_.GetProto());
    device_ = new HidDevice(fake_ddk::kFakeParent);

    // Each test is responsible for calling Bind().
  }

  void TearDown() override {
    TeardownInstanceDriver();
    device_->DdkAsyncRemove();
    EXPECT_TRUE(ddk_.Ok());

    // This should delete the object, which means this test should not leak.
    device_->DdkRelease();
  }

  void SetupBootMouseDevice() {
    size_t desc_size;
    const uint8_t* boot_mouse_desc = get_boot_mouse_report_desc(&desc_size);
    fake_hidbus_.SetDescriptor(boot_mouse_desc, desc_size);

    hid_info_t info = {};
    info.device_class = HID_DEVICE_CLASS_POINTER;
    info.boot_device = true;
    fake_hidbus_.SetHidInfo(info);
  }

  void SetupInstanceDriver() {
    ASSERT_OK(device_->DdkOpen(&instance_driver_, 0));
    instance_ops_ = ddk_.GetLastDeviceOps();

    sync_client_ =
        llcpp::fuchsia::hardware::input::Device::SyncClient(std::move(ddk_.FidlClient()));

    auto result = sync_client_->GetReportsEvent();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
    report_event_ = std::move(result->event);
  }

  void TeardownInstanceDriver() {
    if (instance_driver_ == nullptr) {
      return;
    }

    instance_ops_.ops->close(instance_ops_.ctx, 0);
    instance_driver_ = nullptr;
  }

  zx_status_t ReadOneReport(uint8_t* report_data, size_t report_size, size_t* returned_size) {
    zx_status_t status = report_event_.wait_one(DEV_STATE_READABLE, zx::time::infinite(), nullptr);
    if (status != ZX_OK) {
      return status;
    }

    auto result = sync_client_->ReadReport();
    status = result.status();
    if (status != ZX_OK) {
      return status;
    }
    status = result->status;
    if (status != ZX_OK) {
      return status;
    }
    if (result->data.count() > report_size) {
      return ZX_ERR_BUFFER_TOO_SMALL;
    }

    for (size_t i = 0; i < result->data.count(); i++) {
      report_data[i] = result->data[i];
    }
    *returned_size = result->data.count();
    return ZX_OK;
  }

 protected:
  zx_device_t* instance_driver_ = nullptr;
  ProtocolDeviceOps instance_ops_;
  std::optional<llcpp::fuchsia::hardware::input::Device::SyncClient> sync_client_;
  zx::event report_event_;

  HidDevice* device_;
  Binder ddk_;
  FakeHidbus fake_hidbus_;
  ddk::HidbusProtocolClient client_;
};

TEST_F(HidDeviceTest, LifeTimeTest) {
  SetupBootMouseDevice();
  ASSERT_OK(device_->Bind(client_));
}

TEST_F(HidDeviceTest, TestQuery) {
  // Ids were chosen arbitrarily.
  constexpr uint16_t kVendorId = 0xacbd;
  constexpr uint16_t kProductId = 0xdcba;
  constexpr uint16_t kVersion = 0x1234;

  SetupBootMouseDevice();
  hid_info_t info = {};
  info.device_class = HID_DEVICE_CLASS_POINTER;
  info.boot_device = true;
  info.vendor_id = kVendorId;
  info.product_id = kProductId;
  info.version = kVersion;
  fake_hidbus_.SetHidInfo(info);

  ASSERT_OK(device_->Bind(client_));

  zx_device_t* open_dev;
  ASSERT_OK(device_->DdkOpen(&open_dev, 0));
  // Opening the device created an instance device to be created, and we can
  // get its arguments here.
  ProtocolDeviceOps dev_ops = ddk_.GetLastDeviceOps();

  auto sync_client =
      llcpp::fuchsia::hardware::input::Device::SyncClient(std::move(ddk_.FidlClient()));
  auto result = sync_client.GetDeviceIds();
  ASSERT_OK(result.status());
  llcpp::fuchsia::hardware::input::DeviceIds ids = result->ids;

  ASSERT_EQ(kVendorId, ids.vendor_id);
  ASSERT_EQ(kProductId, ids.product_id);
  ASSERT_EQ(kVersion, ids.version);

  // Close the instance device.
  dev_ops.ops->close(dev_ops.ctx, 0);
}

TEST_F(HidDeviceTest, BootMouseSendReport) {
  SetupBootMouseDevice();
  uint8_t mouse_report[] = {0xDE, 0xAD, 0xBE};
  ASSERT_OK(device_->Bind(client_));

  SetupInstanceDriver();

  fake_hidbus_.SendReport(mouse_report, sizeof(mouse_report));

  uint8_t returned_report[3] = {};
  size_t actual;
  ASSERT_OK(ReadOneReport(returned_report, sizeof(returned_report), &actual));

  ASSERT_EQ(actual, sizeof(returned_report));
  for (size_t i = 0; i < actual; i++) {
    ASSERT_EQ(returned_report[i], mouse_report[i]);
  }
}

TEST_F(HidDeviceTest, BootMouseSendReportWithTime) {
  SetupBootMouseDevice();
  uint8_t mouse_report[] = {0xDE, 0xAD, 0xBE};
  ASSERT_OK(device_->Bind(client_));

  // Regsiter a device listener
  std::pair<sync_completion_t, zx_time_t> callback_data;
  callback_data.second = 0xabcd;

  hid_report_listener_protocol_ops_t listener_ops;
  listener_ops.receive_report = [](void* ctx, const uint8_t* report_list, size_t report_count,
                                   zx_time_t report_time) {
    auto callback_data = static_cast<std::pair<sync_completion_t, zx_time_t>*>(ctx);
    ASSERT_EQ(callback_data->second, report_time);
    sync_completion_signal(&callback_data->first);
  };
  hid_report_listener_protocol_t listener = {&listener_ops, &callback_data};
  device_->HidDeviceRegisterListener(&listener);

  fake_hidbus_.SendReportWithTime(mouse_report, sizeof(mouse_report), callback_data.second);
  sync_completion_wait_deadline(&callback_data.first, zx::time::infinite().get());
  ASSERT_NO_FATAL_FAILURES();
}

TEST_F(HidDeviceTest, BootMouseSendReportInPieces) {
  SetupBootMouseDevice();
  uint8_t mouse_report[] = {0xDE, 0xAD, 0xBE};
  ASSERT_OK(device_->Bind(client_));

  SetupInstanceDriver();

  fake_hidbus_.SendReport(&mouse_report[0], sizeof(uint8_t));
  fake_hidbus_.SendReport(&mouse_report[1], sizeof(uint8_t));
  fake_hidbus_.SendReport(&mouse_report[2], sizeof(uint8_t));

  uint8_t returned_report[3] = {};
  size_t actual;
  ASSERT_OK(ReadOneReport(returned_report, sizeof(returned_report), &actual));

  ASSERT_EQ(actual, sizeof(returned_report));
  for (size_t i = 0; i < actual; i++) {
    ASSERT_EQ(returned_report[i], mouse_report[i]);
  }
}

TEST_F(HidDeviceTest, BootMouseSendMultipleReports) {
  SetupBootMouseDevice();
  uint8_t double_mouse_report[] = {0xDE, 0xAD, 0xBE, 0x12, 0x34, 0x56};
  ASSERT_OK(device_->Bind(client_));

  SetupInstanceDriver();

  fake_hidbus_.SendReport(double_mouse_report, sizeof(double_mouse_report));

  uint8_t returned_report[3] = {};
  size_t actual;

  // Read the first report.
  ASSERT_OK(ReadOneReport(returned_report, sizeof(returned_report), &actual));
  ASSERT_EQ(actual, sizeof(returned_report));
  for (size_t i = 0; i < actual; i++) {
    ASSERT_EQ(returned_report[i], double_mouse_report[i]);
  }

  // Read the second report.
  ASSERT_OK(ReadOneReport(returned_report, sizeof(returned_report), &actual));
  ASSERT_EQ(actual, sizeof(returned_report));
  for (size_t i = 0; i < actual; i++) {
    ASSERT_EQ(returned_report[i], double_mouse_report[i + 3]);
  }
}

TEST(HidDeviceTest, FailToRegister) {
  FakeHidbus fake_hidbus;
  HidDevice device(fake_ddk::kFakeParent);

  fake_hidbus.SetStartStatus(ZX_ERR_INTERNAL);
  auto client = ddk::HidbusProtocolClient(fake_hidbus.GetProto());
  ASSERT_EQ(device.Bind(client), ZX_ERR_INTERNAL);
}

TEST_F(HidDeviceTest, ReadReportSingleReport) {
  SetupBootMouseDevice();
  ASSERT_OK(device_->Bind(client_));

  uint8_t mouse_report[] = {0xDE, 0xAD, 0xBE};

  SetupInstanceDriver();

  // Send the reports.
  zx_time_t time = 0xabcd;
  fake_hidbus_.SendReportWithTime(mouse_report, sizeof(mouse_report), time);

  auto result = sync_client_->ReadReport();
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
  ASSERT_EQ(time, result->time);
  ASSERT_EQ(sizeof(mouse_report), result->data.count());
  for (size_t i = 0; i < result->data.count(); i++) {
    EXPECT_EQ(mouse_report[i], result->data[i]);
  }

  result = sync_client_->ReadReport();
  ASSERT_OK(result.status());
  ASSERT_EQ(result->status, ZX_ERR_SHOULD_WAIT);
}

TEST_F(HidDeviceTest, ReadReportDoubleReport) {
  SetupBootMouseDevice();
  ASSERT_OK(device_->Bind(client_));

  uint8_t double_mouse_report[] = {0xDE, 0xAD, 0xBE, 0x12, 0x34, 0x56};

  SetupInstanceDriver();

  // Send the reports.
  zx_time_t time = 0xabcd;
  fake_hidbus_.SendReportWithTime(double_mouse_report, sizeof(double_mouse_report), time);

  auto result = sync_client_->ReadReport();
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
  ASSERT_EQ(time, result->time);
  ASSERT_EQ(sizeof(hid_boot_mouse_report_t), result->data.count());
  for (size_t i = 0; i < result->data.count(); i++) {
    EXPECT_EQ(double_mouse_report[i], result->data[i]);
  }

  result = sync_client_->ReadReport();
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
  ASSERT_EQ(time, result->time);
  ASSERT_EQ(sizeof(hid_boot_mouse_report_t), result->data.count());
  for (size_t i = 0; i < result->data.count(); i++) {
    EXPECT_EQ(double_mouse_report[i + sizeof(hid_boot_mouse_report_t)], result->data[i]);
  }

  result = sync_client_->ReadReport();
  ASSERT_OK(result.status());
  ASSERT_EQ(result->status, ZX_ERR_SHOULD_WAIT);
}

TEST_F(HidDeviceTest, ReadReportsSingleReport) {
  SetupBootMouseDevice();
  ASSERT_OK(device_->Bind(client_));

  uint8_t mouse_report[] = {0xDE, 0xAD, 0xBE};

  SetupInstanceDriver();

  // Send the reports.
  fake_hidbus_.SendReport(mouse_report, sizeof(mouse_report));

  auto sync_client =
      llcpp::fuchsia::hardware::input::Device::SyncClient(std::move(ddk_.FidlClient()));
  auto result = sync_client_->ReadReports();
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
  ASSERT_EQ(sizeof(mouse_report), result->data.count());
  for (size_t i = 0; i < result->data.count(); i++) {
    EXPECT_EQ(mouse_report[i], result->data[i]);
  }
}

TEST_F(HidDeviceTest, ReadReportsDoubleReport) {
  SetupBootMouseDevice();
  ASSERT_OK(device_->Bind(client_));

  uint8_t double_mouse_report[] = {0xDE, 0xAD, 0xBE, 0x12, 0x34, 0x56};

  SetupInstanceDriver();

  // Send the reports.
  fake_hidbus_.SendReport(double_mouse_report, sizeof(double_mouse_report));

  auto result = sync_client_->ReadReports();
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
  ASSERT_EQ(sizeof(double_mouse_report), result->data.count());
  for (size_t i = 0; i < result->data.count(); i++) {
    EXPECT_EQ(double_mouse_report[i], result->data[i]);
  }
}

TEST_F(HidDeviceTest, ReadReportsBlockingWait) {
  SetupBootMouseDevice();
  ASSERT_OK(device_->Bind(client_));

  SetupInstanceDriver();

  // Send the reports, but delayed.
  uint8_t mouse_report[] = {0xDE, 0xAD, 0xBE};
  std::thread report_thread([&]() {
    sleep(1);
    fake_hidbus_.SendReport(mouse_report, sizeof(mouse_report));
  });

  ASSERT_OK(report_event_.wait_one(DEV_STATE_READABLE, zx::time::infinite(), nullptr));

  // Get the report.
  auto result = sync_client_->ReadReports();
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
  ASSERT_EQ(sizeof(mouse_report), result->data.count());
  for (size_t i = 0; i < result->data.count(); i++) {
    EXPECT_EQ(mouse_report[i], result->data[i]);
  }

  report_thread.join();
}

// Test that only whole reports get sent through.
TEST_F(HidDeviceTest, ReadReportsOneAndAHalfReports) {
  SetupBootMouseDevice();
  ASSERT_OK(device_->Bind(client_));

  SetupInstanceDriver();

  // Send the report.
  uint8_t mouse_report[] = {0xDE, 0xAD, 0xBE};
  fake_hidbus_.SendReport(mouse_report, sizeof(mouse_report));

  // Send a half of a report.
  uint8_t half_report[] = {0xDE, 0xAD};
  fake_hidbus_.SendReport(half_report, sizeof(half_report));

  auto result = sync_client_->ReadReports();
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
  ASSERT_EQ(sizeof(mouse_report), result->data.count());
  for (size_t i = 0; i < result->data.count(); i++) {
    EXPECT_EQ(mouse_report[i], result->data[i]);
  }
}

// This tests that we can set the boot mode for a non-boot device, and that the device will
// have it's report descriptor set to the boot mode descriptor. For this, we will take an
// arbitrary descriptor and claim that it can be set to a boot-mode keyboard. We then
// test that the report descriptor we get back is for the boot keyboard.
// (The descriptor doesn't matter, as long as a device claims its a boot device it should
//  support this transformation in hardware).
TEST_F(HidDeviceTest, SettingBootModeMouse) {
  size_t desc_size;
  const uint8_t* desc = get_paradise_touchpad_v1_report_desc(&desc_size);
  fake_hidbus_.SetDescriptor(desc, desc_size);

  // This info is the reason why the device will be set to a boot mouse mode.
  hid_info_t info = {};
  info.device_class = HID_DEVICE_CLASS_POINTER;
  info.boot_device = true;
  fake_hidbus_.SetHidInfo(info);

  ASSERT_OK(device_->Bind(client_));

  size_t boot_mouse_desc_size;
  const uint8_t* boot_mouse_desc = get_boot_mouse_report_desc(&boot_mouse_desc_size);
  ASSERT_EQ(boot_mouse_desc_size, device_->GetReportDescLen());
  const uint8_t* received_desc = device_->GetReportDesc();
  for (size_t i = 0; i < boot_mouse_desc_size; i++) {
    ASSERT_EQ(boot_mouse_desc[i], received_desc[i]);
  }
}

// This tests that we can set the boot mode for a non-boot device, and that the device will
// have it's report descriptor set to the boot mode descriptor. For this, we will take an
// arbitrary descriptor and claim that it can be set to a boot-mode keyboard. We then
// test that the report descriptor we get back is for the boot keyboard.
// (The descriptor doesn't matter, as long as a device claims its a boot device it should
//  support this transformation in hardware).
TEST_F(HidDeviceTest, SettingBootModeKbd) {
  size_t desc_size;
  const uint8_t* desc = get_paradise_touchpad_v1_report_desc(&desc_size);
  fake_hidbus_.SetDescriptor(desc, desc_size);

  // This info is the reason why the device will be set to a boot mouse mode.
  hid_info_t info = {};
  info.device_class = HID_DEVICE_CLASS_KBD;
  info.boot_device = true;
  fake_hidbus_.SetHidInfo(info);

  ASSERT_OK(device_->Bind(client_));

  size_t boot_kbd_desc_size;
  const uint8_t* boot_kbd_desc = get_boot_kbd_report_desc(&boot_kbd_desc_size);
  ASSERT_EQ(boot_kbd_desc_size, device_->GetReportDescLen());
  const uint8_t* received_desc = device_->GetReportDesc();
  for (size_t i = 0; i < boot_kbd_desc_size; i++) {
    ASSERT_EQ(boot_kbd_desc[i], received_desc[i]);
  }
}

TEST_F(HidDeviceTest, BanjoGetDescriptor) {
  SetupBootMouseDevice();
  ASSERT_OK(device_->Bind(fake_hidbus_.GetProto()));

  size_t known_size;
  const uint8_t* known_descriptor = get_boot_mouse_report_desc(&known_size);

  uint8_t report_descriptor[HID_MAX_DESC_LEN];
  size_t actual;
  ASSERT_OK(device_->HidDeviceGetDescriptor(report_descriptor, sizeof(report_descriptor), &actual));

  ASSERT_EQ(known_size, actual);
  ASSERT_BYTES_EQ(known_descriptor, report_descriptor, known_size);
}

TEST_F(HidDeviceTest, BanjoRegisterListenerSendReport) {
  SetupBootMouseDevice();
  ASSERT_OK(device_->Bind(fake_hidbus_.GetProto()));

  uint8_t mouse_report[] = {0xDE, 0xAD, 0xBE};

  struct ReportCtx {
    sync_completion_t* completion;
    uint8_t* known_report;
  };

  sync_completion_t seen_report;
  ReportCtx ctx;
  ctx.completion = &seen_report;
  ctx.known_report = mouse_report;

  hid_report_listener_protocol_ops_t ops;
  ops.receive_report = [](void* ctx, const uint8_t* report_list, size_t report_count,
                          zx_time_t time) {
    ASSERT_EQ(sizeof(mouse_report), report_count);
    auto report_ctx = reinterpret_cast<ReportCtx*>(ctx);
    ASSERT_BYTES_EQ(report_ctx->known_report, report_list, report_count);
    sync_completion_signal(report_ctx->completion);
  };

  hid_report_listener_protocol_t listener;
  listener.ctx = &ctx;
  listener.ops = &ops;

  ASSERT_OK(device_->HidDeviceRegisterListener(&listener));

  fake_hidbus_.SendReport(mouse_report, sizeof(mouse_report));

  ASSERT_OK(sync_completion_wait(&seen_report, zx::time::infinite().get()));
  device_->HidDeviceUnregisterListener();
}

TEST_F(HidDeviceTest, BanjoGetSetReport) {
  const uint8_t* desc;
  size_t desc_size = get_ambient_light_report_desc(&desc);
  fake_hidbus_.SetDescriptor(desc, desc_size);

  hid_info_t info = {};
  info.device_class = HID_DEVICE_CLASS_OTHER;
  info.boot_device = false;
  fake_hidbus_.SetHidInfo(info);

  ASSERT_OK(device_->Bind(fake_hidbus_.GetProto()));

  ambient_light_feature_rpt_t feature_report = {};
  feature_report.rpt_id = AMBIENT_LIGHT_RPT_ID_FEATURE;
  // Below value are chosen arbitrarily.
  feature_report.state = 100;
  feature_report.interval_ms = 50;
  feature_report.threshold_high = 40;
  feature_report.threshold_low = 10;

  ASSERT_OK(device_->HidDeviceSetReport(HID_REPORT_TYPE_FEATURE, AMBIENT_LIGHT_RPT_ID_FEATURE,
                                        reinterpret_cast<uint8_t*>(&feature_report),
                                        sizeof(feature_report)));

  ambient_light_feature_rpt_t received_report = {};
  size_t actual;
  ASSERT_OK(device_->HidDeviceGetReport(HID_REPORT_TYPE_FEATURE, AMBIENT_LIGHT_RPT_ID_FEATURE,
                                        reinterpret_cast<uint8_t*>(&received_report),
                                        sizeof(received_report), &actual));

  ASSERT_EQ(sizeof(received_report), actual);
  ASSERT_BYTES_EQ(&feature_report, &received_report, actual);
}

}  // namespace hid_driver
