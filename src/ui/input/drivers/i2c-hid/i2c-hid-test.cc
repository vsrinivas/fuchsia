// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "i2c-hid.h"

#include <endian.h>
#include <fidl/fuchsia.hardware.acpi/cpp/wire.h>
#include <fidl/fuchsia.hardware.interrupt/cpp/wire.h>
#include <fuchsia/hardware/hidbus/c/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/trace/event.h>
#include <lib/device-protocol/i2c-channel.h>
#include <lib/fake-hidbus-ifc/fake-hidbus-ifc.h>
#include <lib/fake-i2c/fake-i2c.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/sync/completion.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/zx/clock.h>
#include <lib/zx/interrupt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/rights.h>
#include <zircon/types.h>

#include <vector>

#include <fbl/auto_lock.h>
#include <zxtest/zxtest.h>

#include "src/devices/lib/acpi/mock/mock-acpi.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace i2c_hid {

// Ids were chosen arbitrarily.
static constexpr uint16_t kHidVendorId = 0xabcd;
static constexpr uint16_t kHidProductId = 0xdcba;
static constexpr uint16_t kHidVersion = 0x0123;

class FakeI2cHid : public fake_i2c::FakeI2c {
 public:
  // Sets the report descriptor. Must be called before binding the driver because
  // the driver reads |hiddesc_| on bind.
  void SetReportDescriptor(std::vector<uint8_t> report_desc) {
    fbl::AutoLock lock(&report_read_lock_);
    hiddesc_.wReportDescLength = htole16(report_desc.size());
    report_desc_ = std::move(report_desc);
  }

  // Calling this function will make the FakeI2cHid driver return an error when the I2cHidbus
  // tries to read the HidDescriptor. This so we can test that the I2cHidbus driver shuts
  // down correctly when it fails to read the HidDescriptor.
  void SetHidDescriptorFailure(zx_status_t status) { hiddesc_status_ = status; }

  void SendReport(std::vector<uint8_t> report) {
    SendReportWithLength(report, report.size() + sizeof(uint16_t));
  }

  // This lets us send a report with an incorrect length.
  void SendReportWithLength(std::vector<uint8_t> report, size_t len) {
    {
      fbl::AutoLock lock(&report_read_lock_);

      report_ = std::move(report);
      report_len_ = len;
      irq_.trigger(0, zx::clock::get_monotonic());
    }
    sync_completion_wait_deadline(&report_read_, zx::time::infinite().get());
    sync_completion_reset(&report_read_);
  }

  zx_status_t WaitUntilReset() {
    return sync_completion_wait_deadline(&is_reset_, zx::time::infinite().get());
  }

 private:
  zx_status_t TransactCommands(const uint8_t* write_buffer, size_t write_buffer_size,
                               uint8_t* read_buffer, size_t* read_buffer_size)
      __TA_REQUIRES(report_read_lock_) {
    if (write_buffer_size < 4) {
      return ZX_ERR_INTERNAL;
    }

    // Reset Command.
    if (write_buffer[3] == kResetCommand) {
      *read_buffer_size = 0;
      pending_reset_ = true;
      irq_.trigger(0, zx::clock::get_monotonic());
      return ZX_OK;
    }

    // Set Command. At the moment this fake doesn't test for the types of reports, we only ever
    // get/set |report_|.
    if (write_buffer[3] == kSetReportCommand) {
      if (write_buffer_size < 6) {
        return ZX_ERR_INTERNAL;
      }
      // Get the report size.
      uint16_t report_size = static_cast<uint16_t>(write_buffer[6] + (write_buffer[7] << 8));
      if (write_buffer_size < (6 + report_size)) {
        return ZX_ERR_INTERNAL;
      }
      // The report size includes the 2 bytes for the size, which we don't want when setting
      // the report.
      report_.resize(report_size - 2);
      memcpy(report_.data(), write_buffer + 8, report_.size());
      return ZX_OK;
    }

    // Get Command.
    if (write_buffer[3] == kGetReportCommand) {
      // Set the report size as the first two bytes.
      read_buffer[0] = static_cast<uint8_t>((report_.size() + 2) & 0xFF);
      read_buffer[1] = static_cast<uint8_t>(((report_.size() + 2) >> 8) & 0xFF);

      memcpy(read_buffer + 2, report_.data(), report_.size());
      *read_buffer_size = report_.size() + 2;
      return ZX_OK;
    }
    return ZX_ERR_INTERNAL;
  }

  zx_status_t Transact(const uint8_t* write_buffer, size_t write_buffer_size, uint8_t* read_buffer,
                       size_t* read_buffer_size) override {
    fbl::AutoLock lock(&report_read_lock_);
    // General Read.
    if (write_buffer_size == 0) {
      // Reading the Reset status.
      if (pending_reset_) {
        SetRead(kResetReport, sizeof(kResetReport), read_buffer, read_buffer_size);
        pending_reset_ = false;
        sync_completion_signal(&is_reset_);
        return ZX_OK;
      }
      // First two bytes are the report length.
      *reinterpret_cast<uint16_t*>(read_buffer) = htole16(report_len_);

      memcpy(read_buffer + sizeof(uint16_t), report_.data(), report_.size());
      *read_buffer_size = report_.size() + sizeof(uint16_t);
      sync_completion_signal(&report_read_);
      return ZX_OK;
    }
    // Reading the Hid descriptor.
    if (CompareWrite(write_buffer, write_buffer_size, kHidDescCommand, sizeof(kHidDescCommand))) {
      if (hiddesc_status_ != ZX_OK) {
        return hiddesc_status_;
      }
      SetRead(&hiddesc_, sizeof(hiddesc_), read_buffer, read_buffer_size);
      return ZX_OK;
    }
    // Reading the Hid Report descriptor.
    if (CompareWrite(write_buffer, write_buffer_size,
                     reinterpret_cast<const uint8_t*>(&kReportDescRegister),
                     sizeof(kReportDescRegister))) {
      SetRead(report_desc_.data(), report_desc_.size(), read_buffer, read_buffer_size);
      return ZX_OK;
    }

    // General commands.
    bool is_general_command = (write_buffer_size >= 2) && (write_buffer[0] == kHidCommand[0]) &&
                              (write_buffer[1] == kHidCommand[1]);
    if (is_general_command) {
      return TransactCommands(write_buffer, write_buffer_size, read_buffer, read_buffer_size);
    }
    return ZX_ERR_INTERNAL;
  }

  // Register values were just picked arbitrarily.
  static constexpr uint16_t kInputRegister = htole16(0x5);
  static constexpr uint16_t kOutputRegister = htole16(0x6);
  static constexpr uint16_t kCommandRegister = htole16(0x7);
  static constexpr uint16_t kDataRegister = htole16(0x8);
  static constexpr uint16_t kReportDescRegister = htole16(0x9);

  static constexpr uint16_t kMaxInputLength = 0x1000;

  static constexpr uint8_t kResetReport[2] = {0, 0};
  static constexpr uint8_t kHidDescCommand[2] = {0x01, 0x00};
  static constexpr uint8_t kHidCommand[2] = {static_cast<uint8_t>(kCommandRegister & 0xff),
                                             static_cast<uint8_t>(kCommandRegister >> 8)};

  I2cHidDesc hiddesc_ = []() {
    I2cHidDesc hiddesc = {};
    hiddesc.wHIDDescLength = htole16(sizeof(I2cHidDesc));
    hiddesc.wInputRegister = kInputRegister;
    hiddesc.wOutputRegister = kOutputRegister;
    hiddesc.wCommandRegister = kCommandRegister;
    hiddesc.wDataRegister = kDataRegister;
    hiddesc.wMaxInputLength = kMaxInputLength;
    hiddesc.wReportDescRegister = kReportDescRegister;
    hiddesc.wVendorID = htole16(kHidVendorId);
    hiddesc.wProductID = htole16(kHidProductId);
    hiddesc.wVersionID = htole16(kHidVersion);
    return hiddesc;
  }();
  zx_status_t hiddesc_status_ = ZX_OK;

  std::atomic<bool> pending_reset_ = false;
  sync_completion_t is_reset_;

  fbl::Mutex report_read_lock_;
  sync_completion_t report_read_;
  std::vector<uint8_t> report_desc_ __TA_GUARDED(report_read_lock_);
  std::vector<uint8_t> report_ __TA_GUARDED(report_read_lock_);
  size_t report_len_ __TA_GUARDED(report_read_lock_) = 0;
};

class I2cHidTest : public zxtest::Test,
                   public fidl::WireServer<fuchsia_hardware_interrupt::Provider> {
 public:
  I2cHidTest() : loop_(&kAsyncLoopConfigNeverAttachToThread) {}
  void SetUp() override {
    parent_ = MockDevice::FakeRootParent();

    ASSERT_OK(loop_.StartThread("i2c-hid-test-thread"));
    acpi_device_.SetEvaluateObject(
        [](acpi::mock::Device::EvaluateObjectRequestView view,
           acpi::mock::Device::EvaluateObjectCompleter::Sync& completer) {
          fidl::Arena<> alloc;

          auto encoded = fuchsia_hardware_acpi::wire::EncodedObject::WithObject(
              alloc, fuchsia_hardware_acpi::wire::Object::WithIntegerVal(alloc, 0x01));

          completer.ReplySuccess(std::move(encoded));
          ASSERT_TRUE(completer.result_of_reply().ok());
        });

    ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq_));
    zx::interrupt interrupt;
    ASSERT_OK(irq_.duplicate(ZX_RIGHT_SAME_RIGHTS, &interrupt));
    fake_i2c_hid_.SetInterrupt(std::move(interrupt));

    component::ServiceHandler handler;
    fuchsia_hardware_interrupt::Service::Handler service(&handler);

    auto provider_handler = [this](fidl::ServerEnd<fuchsia_hardware_interrupt::Provider> request) {
      fidl::BindServer(loop_.dispatcher(), std::move(request), this);
    };

    ASSERT_OK(service.add_provider(std::move(provider_handler)).status_value());

    ASSERT_OK(outgoing_.AddService<fuchsia_hardware_interrupt::Service>(std::move(handler))
                  .status_value());

    auto io_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    ASSERT_OK(io_endpoints.status_value());

    ASSERT_OK(outgoing_.Serve(std::move(io_endpoints->server)).status_value());

    parent_->AddFidlService(fuchsia_hardware_interrupt::Service::Name,
                            std::move(io_endpoints->client), "irq001");

    auto client = acpi_device_.CreateClient(loop_.dispatcher());
    ASSERT_OK(client.status_value());
    device_ = new I2cHidbus(parent_.get(), std::move(client.value()));

    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_i2c::Device>();
    EXPECT_TRUE(endpoints.is_ok());

    fidl::BindServer<FakeI2cHid>(loop_.dispatcher(), std::move(endpoints->server), &fake_i2c_hid_);

    i2c_ = std::move(endpoints->client);
    // Each test is responsible for calling Bind().
  }

  void Get(GetCompleter::Sync& completer) override {
    zx::interrupt clone;
    ASSERT_OK(irq_.duplicate(ZX_RIGHT_SAME_RIGHTS, &clone));
    completer.ReplySuccess(std::move(clone));
  }

  void TearDown() override {
    device_->DdkAsyncRemove();

    EXPECT_OK(mock_ddk::ReleaseFlaggedDevices(device_->zxdev()));
  }

  void StartHidBus() { device_->HidbusStart(fake_hid_bus_.GetProto()); }

 protected:
  acpi::mock::Device acpi_device_;
  I2cHidbus* device_;
  std::shared_ptr<MockDevice> parent_;
  FakeI2cHid fake_i2c_hid_;
  fake_hidbus_ifc::FakeHidbusIfc fake_hid_bus_;
  fidl::ClientEnd<fuchsia_hardware_i2c::Device> i2c_;
  zx::interrupt irq_;
  async::Loop loop_;
  component::OutgoingDirectory outgoing_ = component::OutgoingDirectory::Create(loop_.dispatcher());
};

TEST_F(I2cHidTest, HidTestBind) {
  ASSERT_OK(device_->Bind(std::move(i2c_)));
  device_->zxdev()->InitOp();
  ASSERT_OK(device_->zxdev()->WaitUntilInitReplyCalled(zx::time::infinite()));
  EXPECT_OK(device_->zxdev()->InitReplyCallStatus());
}

TEST_F(I2cHidTest, HidTestQuery) {
  ASSERT_OK(device_->Bind(std::move(i2c_)));
  device_->zxdev()->InitOp();
  ASSERT_OK(fake_i2c_hid_.WaitUntilReset());

  StartHidBus();

  hid_info_t info = {};
  ASSERT_OK(device_->HidbusQuery(0, &info));
  ASSERT_EQ(kHidVendorId, info.vendor_id);
  ASSERT_EQ(kHidProductId, info.product_id);
  ASSERT_EQ(kHidVersion, info.version);
}

TEST_F(I2cHidTest, HidTestReadReportDesc) {
  uint8_t returned_report_desc[HID_MAX_DESC_LEN];
  size_t returned_report_desc_len;
  std::vector<uint8_t> report_desc(4);
  report_desc[0] = 1;
  report_desc[1] = 100;
  report_desc[2] = 255;
  report_desc[3] = 5;

  fake_i2c_hid_.SetReportDescriptor(report_desc);
  ASSERT_OK(device_->Bind(std::move(i2c_)));
  device_->zxdev()->InitOp();

  ASSERT_OK(device_->HidbusGetDescriptor(HID_DESCRIPTION_TYPE_REPORT, returned_report_desc,
                                         sizeof(returned_report_desc), &returned_report_desc_len));
  ASSERT_EQ(returned_report_desc_len, report_desc.size());
  for (size_t i = 0; i < returned_report_desc_len; i++) {
    ASSERT_EQ(returned_report_desc[i], report_desc[i]);
  }
}

TEST(I2cHidTest, HidTestReportDescFailureLifetimeTest) {
  I2cHidbus* device_;
  std::shared_ptr<MockDevice> parent = MockDevice::FakeRootParent();
  FakeI2cHid fake_i2c_hid_;
  fake_hidbus_ifc::FakeHidbusIfc fake_hid_bus_;
  ddk::I2cChannel channel_;

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  EXPECT_OK(loop.StartThread());

  auto i2c_endpoints = fidl::CreateEndpoints<fuchsia_hardware_i2c::Device>();
  EXPECT_TRUE(i2c_endpoints.is_ok());

  fidl::BindServer(loop.dispatcher(), std::move(i2c_endpoints->server), &fake_i2c_hid_);

  zx::status endpoints = fidl::CreateEndpoints<fuchsia_hardware_acpi::Device>();
  ASSERT_OK(endpoints.status_value());
  endpoints->server.reset();
  device_ = new I2cHidbus(parent.get(),
                          acpi::Client::Create(fidl::WireSyncClient(std::move(endpoints->client))));
  channel_ = ddk::I2cChannel(std::move(i2c_endpoints->client));

  fake_i2c_hid_.SetHidDescriptorFailure(ZX_ERR_TIMED_OUT);
  ASSERT_OK(device_->Bind(std::move(channel_)));

  device_->zxdev()->InitOp();

  EXPECT_OK(device_->zxdev()->WaitUntilInitReplyCalled(zx::time::infinite()));
  EXPECT_NOT_OK(device_->zxdev()->InitReplyCallStatus());

  loop.Shutdown();

  device_async_remove(parent.get());
  mock_ddk::ReleaseFlaggedDevices(parent.get());
}

TEST_F(I2cHidTest, HidTestReadReport) {
  ASSERT_OK(device_->Bind(std::move(i2c_)));
  device_->zxdev()->InitOp();
  ASSERT_OK(fake_i2c_hid_.WaitUntilReset());

  StartHidBus();

  // Any arbitrary values or vector length could be used here.
  std::vector<uint8_t> rpt(4);
  rpt[0] = 1;
  rpt[1] = 100;
  rpt[2] = 255;
  rpt[3] = 5;
  fake_i2c_hid_.SendReport(rpt);

  std::vector<uint8_t> returned_rpt;
  ASSERT_OK(fake_hid_bus_.WaitUntilNextReport(&returned_rpt));

  ASSERT_EQ(returned_rpt.size(), rpt.size());
  for (size_t i = 0; i < returned_rpt.size(); i++) {
    EXPECT_EQ(returned_rpt[i], rpt[i]);
  }
}

TEST_F(I2cHidTest, HidTestBadReportLen) {
  ASSERT_OK(device_->Bind(std::move(i2c_)));
  device_->zxdev()->InitOp();
  ASSERT_OK(fake_i2c_hid_.WaitUntilReset());

  StartHidBus();

  // Send a report with a length that's too long.
  std::vector<uint8_t> too_long_rpt{0xAA};
  fake_i2c_hid_.SendReportWithLength(too_long_rpt, UINT16_MAX);

  // Send a normal report.
  std::vector<uint8_t> normal_rpt{0xBB};
  fake_i2c_hid_.SendReport(normal_rpt);

  // Wait until the reports are in.
  std::vector<uint8_t> returned_rpt;
  ASSERT_OK(fake_hid_bus_.WaitUntilNextReport(&returned_rpt));

  // We should've only seen one report since the too long report will cause an error.
  ASSERT_EQ(fake_hid_bus_.NumReportsSeen(), 1);

  // Double check that the returned report is the normal one.
  ASSERT_EQ(returned_rpt.size(), normal_rpt.size());
  ASSERT_EQ(returned_rpt[0], normal_rpt[0]);
}

TEST_F(I2cHidTest, HidTestReadReportNoIrq) {
  // Replace the device's interrupt with an invalid one.
  fake_i2c_hid_.SetInterrupt(zx::interrupt());
  irq_.reset();

  ASSERT_OK(device_->Bind(std::move(i2c_)));
  device_->zxdev()->InitOp();
  ASSERT_OK(fake_i2c_hid_.WaitUntilReset());

  StartHidBus();

  // Any arbitrary values or vector length could be used here.
  std::vector<uint8_t> rpt(4);
  rpt[0] = 1;
  rpt[1] = 100;
  rpt[2] = 255;
  rpt[3] = 5;
  fake_i2c_hid_.SendReport(rpt);

  std::vector<uint8_t> returned_rpt;
  ASSERT_OK(fake_hid_bus_.WaitUntilNextReport(&returned_rpt));

  ASSERT_EQ(returned_rpt.size(), rpt.size());
  for (size_t i = 0; i < returned_rpt.size(); i++) {
    EXPECT_EQ(returned_rpt[i], rpt[i]);
  }
}

TEST_F(I2cHidTest, HidTestDedupeReportsNoIrq) {
  // Replace the device's interrupt with an invalid one.
  fake_i2c_hid_.SetInterrupt(zx::interrupt());
  irq_.reset();

  ASSERT_OK(device_->Bind(std::move(i2c_)));
  device_->zxdev()->InitOp();
  ASSERT_OK(fake_i2c_hid_.WaitUntilReset());

  StartHidBus();

  // Send three reports.
  std::vector<uint8_t> rpt1(4);
  rpt1[0] = 1;
  rpt1[1] = 100;
  rpt1[2] = 255;
  rpt1[3] = 5;
  fake_i2c_hid_.SendReport(rpt1);
  fake_i2c_hid_.SendReport(rpt1);
  fake_i2c_hid_.SendReport(rpt1);

  std::vector<uint8_t> returned_rpt1;
  ASSERT_OK(fake_hid_bus_.WaitUntilNextReport(&returned_rpt1));

  // We should've only seen one report since the repeats should have been deduped.
  ASSERT_EQ(fake_hid_bus_.NumReportsSeen(), 1);

  ASSERT_EQ(returned_rpt1.size(), rpt1.size());
  for (size_t i = 0; i < returned_rpt1.size(); i++) {
    EXPECT_EQ(returned_rpt1[i], rpt1[i]);
  }

  // Send three different reports.
  std::vector<uint8_t> rpt2(4);
  rpt2[0] = 1;
  rpt2[1] = 200;
  rpt2[2] = 100;
  rpt2[3] = 6;
  fake_i2c_hid_.SendReport(rpt2);
  fake_i2c_hid_.SendReport(rpt2);
  fake_i2c_hid_.SendReport(rpt2);

  std::vector<uint8_t> returned_rpt2;
  ASSERT_OK(fake_hid_bus_.WaitUntilNextReport(&returned_rpt2));

  // We should've only seen two report since the repeats should have been deduped.
  ASSERT_EQ(fake_hid_bus_.NumReportsSeen(), 2);

  ASSERT_EQ(returned_rpt2.size(), rpt2.size());
  for (size_t i = 0; i < returned_rpt2.size(); i++) {
    EXPECT_EQ(returned_rpt2[i], rpt2[i]);
  }

  // Send a report with different length.
  std::vector<uint8_t> rpt3(5);
  rpt3[0] = 1;
  rpt3[1] = 200;
  rpt3[2] = 100;
  rpt3[3] = 6;
  rpt3[4] = 10;
  fake_i2c_hid_.SendReport(rpt3);

  std::vector<uint8_t> returned_rpt3;
  ASSERT_OK(fake_hid_bus_.WaitUntilNextReport(&returned_rpt3));

  ASSERT_EQ(fake_hid_bus_.NumReportsSeen(), 3);

  ASSERT_EQ(returned_rpt3.size(), rpt3.size());
  for (size_t i = 0; i < returned_rpt3.size(); i++) {
    EXPECT_EQ(returned_rpt3[i], rpt3[i]);
  }
}

TEST_F(I2cHidTest, HidTestSetReport) {
  ASSERT_OK(device_->Bind(std::move(i2c_)));
  device_->zxdev()->InitOp();
  ASSERT_OK(fake_i2c_hid_.WaitUntilReset());

  // Any arbitrary values or vector length could be used here.
  uint8_t report_data[4] = {1, 100, 255, 5};

  ASSERT_OK(
      device_->HidbusSetReport(HID_REPORT_TYPE_FEATURE, 0x1, report_data, sizeof(report_data)));

  uint8_t received_data[4] = {};
  size_t out_len;
  ASSERT_OK(device_->HidbusGetReport(HID_REPORT_TYPE_FEATURE, 0x1, received_data,
                                     sizeof(received_data), &out_len));
  ASSERT_EQ(out_len, 4);
  for (size_t i = 0; i < out_len; i++) {
    EXPECT_EQ(received_data[i], report_data[i]);
  }
}

}  // namespace i2c_hid
