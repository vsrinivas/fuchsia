// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "i2c-hid.h"

#include <endian.h>
#include <lib/device-protocol/i2c.h>
#include <lib/fake-hidbus-ifc/fake-hidbus-ifc.h>
#include <lib/fake-i2c/fake-i2c.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/sync/completion.h>
#include <lib/zx/clock.h>
#include <lib/zx/interrupt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/hw/i2c.h>
#include <zircon/types.h>

#include <vector>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/hidbus.h>
#include <ddk/protocol/i2c.h>
#include <ddk/trace/event.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <zxtest/zxtest.h>

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
    fbl::AutoLock lock(&report_read_lock_);

    report_ = std::move(report);
    report_len_ = len;
    irq_.trigger(0, zx::clock::get_monotonic());
    sync_completion_wait_deadline(&report_read_, zx::time::infinite().get());
    sync_completion_reset(&report_read_);
  }

  zx_status_t WaitUntilReset() {
    return sync_completion_wait_deadline(&is_reset_, zx::time::infinite().get());
  }

 private:
  zx_status_t Transact(const uint8_t* write_buffer, size_t write_buffer_size, uint8_t* read_buffer,
                       size_t* read_buffer_size) override {
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
    // Reset command.
    if (CompareWrite(write_buffer, write_buffer_size, kResetCommand, sizeof(kResetCommand))) {
      *read_buffer_size = 0;
      pending_reset_ = true;
      irq_.trigger(0, zx::clock::get_monotonic());
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
    return ZX_ERR_INTERNAL;
  }

  // Register values were just picked arbitrarily.
  static constexpr uint16_t kInputRegister = htole16(0x5);
  static constexpr uint16_t kOutputRegister = htole16(0x6);
  static constexpr uint16_t kCommandRegister = htole16(0x7);
  static constexpr uint16_t kDataRegister = htole16(0x8);
  static constexpr uint16_t kReportDescRegister = htole16(0x9);

  static constexpr uint16_t kMaxInputLength = 0x1000;

  static constexpr uint8_t kResetCommand[4] = {static_cast<uint8_t>(kCommandRegister & 0xff),
                                               static_cast<uint8_t>(kCommandRegister >> 8), 0x00,
                                               0x01};
  static constexpr uint8_t kResetReport[2] = {0, 0};
  static constexpr uint8_t kHidDescCommand[2] = {0x01, 0x00};

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
  std::vector<uint8_t> report_desc_;
  std::vector<uint8_t> report_;
  size_t report_len_ = 0;
};

class I2cHidTest : public zxtest::Test {
 public:
  void SetUp() override {
    device_ = new I2cHidbus(fake_ddk::kFakeParent);

    zx_handle_t interrupt;
    ASSERT_OK(zx_interrupt_create(ZX_HANDLE_INVALID, 0, ZX_INTERRUPT_VIRTUAL, &interrupt));
    fake_i2c_hid_.SetInterrupt(zx::interrupt(interrupt));

    channel_ = ddk::I2cChannel(fake_i2c_hid_.GetProto());
    // Each test is responsible for calling Bind().
  }

  void TearDown() override {
    device_->DdkUnbindDeprecated();
    EXPECT_TRUE(ddk_.Ok());

    // This should delete the object, which means this test should not leak.
    device_->DdkRelease();
  }

  void StartHidBus() { device_->HidbusStart(fake_hid_bus_.GetProto()); }

 protected:
  I2cHidbus* device_;
  fake_ddk::Bind ddk_;
  FakeI2cHid fake_i2c_hid_;
  fake_hidbus_ifc::FakeHidbusIfc fake_hid_bus_;
  ddk::I2cChannel channel_;
};

TEST_F(I2cHidTest, HidTestBind) { ASSERT_OK(device_->Bind(channel_)); }

TEST_F(I2cHidTest, HidTestQuery) {
  ASSERT_OK(device_->Bind(channel_));
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
  ASSERT_OK(device_->Bind(channel_));

  ASSERT_OK(device_->HidbusGetDescriptor(HID_DESCRIPTION_TYPE_REPORT, returned_report_desc,
                                         sizeof(returned_report_desc), &returned_report_desc_len));
  ASSERT_EQ(returned_report_desc_len, report_desc.size());
  for (size_t i = 0; i < returned_report_desc_len; i++) {
    ASSERT_EQ(returned_report_desc[i], report_desc[i]);
  }
}

TEST(I2cHidTest, HidTestReportDescFailureLifetimeTest) {
  I2cHidbus* device_;
  fake_ddk::Bind ddk_;
  FakeI2cHid fake_i2c_hid_;
  fake_hidbus_ifc::FakeHidbusIfc fake_hid_bus_;
  ddk::I2cChannel channel_;

  device_ = new I2cHidbus(fake_ddk::kFakeParent);
  channel_ = ddk::I2cChannel(fake_i2c_hid_.GetProto());

  fake_i2c_hid_.SetHidDescriptorFailure(ZX_ERR_TIMED_OUT);
  ASSERT_OK(device_->Bind(channel_));

  EXPECT_OK(ddk_.WaitUntilRemove());
  EXPECT_TRUE(ddk_.Ok());

  device_->DdkRelease();
}

TEST_F(I2cHidTest, HidTestReadReport) {
  ASSERT_OK(device_->Bind(channel_));
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
  ASSERT_OK(device_->Bind(channel_));
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

  ASSERT_OK(device_->Bind(channel_));
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

  ASSERT_OK(device_->Bind(channel_));
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

}  // namespace i2c_hid
