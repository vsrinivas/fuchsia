// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gt6853.h"

#include <lib/device-protocol/i2c-channel.h>
#include <lib/fake-i2c/fake-i2c.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/zx/clock.h>

#include <ddk/protocol/composite.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <mock/ddktl/protocol/gpio.h>
#include <zxtest/zxtest.h>

namespace touch {

class FakeTouchDevice : public fake_i2c::FakeI2c {
 public:
  void WaitForTouchDataRead() {
    sync_completion_wait(&read_completion_, ZX_TIME_INFINITE);
    sync_completion_reset(&read_completion_);
  }

  bool ok() const { return event_reset_; }

 protected:
  zx_status_t Transact(const uint8_t* write_buffer, size_t write_buffer_size, uint8_t* read_buffer,
                       size_t* read_buffer_size) override {
    constexpr uint8_t kTouchData[] = {
        // clang-format off
        0x80, 0x5a, 0x00, 0xb9, 0x03, 0xae, 0x00, 0x00,
        0xc2, 0xf2, 0x01, 0x44, 0x00, 0x6c, 0x00, 0x00,
        0x01, 0x72, 0x00, 0x14, 0x01, 0x13, 0x00, 0x00,
        0xc3, 0x38, 0x01, 0xbe, 0x00, 0xdf, 0x00, 0x00,
        // clang-format on
    };

    if (write_buffer_size < 2) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    const uint16_t address = (write_buffer[0] << 8) | write_buffer[1];
    write_buffer += 2;
    write_buffer_size -= 2;

    if (address == 0x4100 && write_buffer_size >= 1 && write_buffer[0] == 0x00) {
      event_reset_ = true;
    } else if (address == 0x4100) {
      read_buffer[0] = 0x80;
      *read_buffer_size = 1;
    } else if (address == 0x4101) {
      read_buffer[0] = 0x34;
      *read_buffer_size = 1;
    } else if (address == 0x4102) {
      // The interrupt has been received and the driver is reading out the data registers.
      memcpy(read_buffer, kTouchData, sizeof(kTouchData));
      *read_buffer_size = sizeof(kTouchData);
      sync_completion_signal(&read_completion_);
    } else {
      return ZX_ERR_IO;
    }

    return ZX_OK;
  }

 private:
  sync_completion_t read_completion_;
  bool event_reset_ = false;
};

class Gt6853Test : public zxtest::Test {
 public:
  void SetUp() override {
    composite_protocol_ops_t composite_protocol = {
        .get_fragment = GetFragment,
    };

    fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[3], 3);
    protocols[0] = {
        .id = ZX_PROTOCOL_COMPOSITE,
        .proto = {.ops = &composite_protocol, .ctx = this},
    };
    protocols[1] = {
        .id = ZX_PROTOCOL_I2C,
        .proto = {.ops = fake_i2c_.GetProto()->ops, .ctx = fake_i2c_.GetProto()->ctx},
    };
    protocols[2] = {
        .id = ZX_PROTOCOL_GPIO,
        .proto = {.ops = mock_gpio_.GetProto()->ops, .ctx = mock_gpio_.GetProto()->ctx},
    };

    ddk_.SetProtocols(std::move(protocols));

    ASSERT_OK(zx::interrupt::create(zx::resource(ZX_HANDLE_INVALID), 0, ZX_INTERRUPT_VIRTUAL,
                                    &gpio_interrupt_));

    zx::interrupt gpio_interrupt;
    ASSERT_OK(gpio_interrupt_.duplicate(ZX_RIGHT_SAME_RIGHTS, &gpio_interrupt));

    mock_gpio_.ExpectConfigIn(ZX_OK, GPIO_NO_PULL);
    mock_gpio_.ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_LOW, std::move(gpio_interrupt));

    auto status = Gt6853Device::CreateAndGetDevice(nullptr, fake_ddk::kFakeParent);
    ASSERT_TRUE(status.is_ok());
    device_ = status.value();
  }

  void TearDown() override {
    device_async_remove(fake_ddk::kFakeDevice);
    EXPECT_TRUE(ddk_.Ok());
    device_->DdkRelease();
  }

 protected:
  fake_ddk::Bind ddk_;
  FakeTouchDevice fake_i2c_;
  zx::interrupt gpio_interrupt_;
  Gt6853Device* device_ = nullptr;

 private:
  static bool GetFragment(void* ctx, const char* name, zx_device_t** out_fragment) {
    if (strcmp(name, "i2c") == 0 || strcmp(name, "gpio-int") == 0 ||
        strcmp(name, "gpio-reset") == 0) {
      *out_fragment = fake_ddk::kFakeParent;
      return true;
    }

    return false;
  }

  ddk::MockGpio mock_gpio_;
};

TEST_F(Gt6853Test, GetDescriptor) {
  fuchsia_input_report::InputDevice::SyncClient client(std::move(ddk_.FidlClient()));

  auto response = client.GetDescriptor();

  ASSERT_TRUE(response.ok());
  ASSERT_TRUE(response->descriptor.has_device_info());
  ASSERT_TRUE(response->descriptor.has_touch());
  ASSERT_TRUE(response->descriptor.touch().has_input());
  ASSERT_TRUE(response->descriptor.touch().input().has_contacts());
  ASSERT_TRUE(response->descriptor.touch().input().has_max_contacts());
  ASSERT_TRUE(response->descriptor.touch().input().has_touch_type());
  ASSERT_EQ(response->descriptor.touch().input().contacts().count(), 10);

  EXPECT_EQ(response->descriptor.device_info().vendor_id,
            static_cast<uint32_t>(fuchsia_input_report::VendorId::GOOGLE));
  EXPECT_EQ(
      response->descriptor.device_info().product_id,
      static_cast<uint32_t>(fuchsia_input_report::VendorGoogleProductId::FOCALTECH_TOUCHSCREEN));

  for (size_t i = 0; i < 10; i++) {
    const auto& contact = response->descriptor.touch().input().contacts()[i];
    ASSERT_TRUE(contact.has_position_x());
    ASSERT_TRUE(contact.has_position_y());

    EXPECT_EQ(contact.position_x().range.min, 0);
    EXPECT_EQ(contact.position_x().range.max, 600);
    EXPECT_EQ(contact.position_x().unit.type, fuchsia_input_report::UnitType::NONE);
    EXPECT_EQ(contact.position_x().unit.exponent, 0);

    EXPECT_EQ(contact.position_y().range.min, 0);
    EXPECT_EQ(contact.position_y().range.max, 1024);
    EXPECT_EQ(contact.position_y().unit.type, fuchsia_input_report::UnitType::NONE);
    EXPECT_EQ(contact.position_y().unit.exponent, 0);
  }

  EXPECT_EQ(response->descriptor.touch().input().max_contacts(), 10);
  EXPECT_EQ(response->descriptor.touch().input().touch_type(),
            fuchsia_input_report::TouchType::TOUCHSCREEN);
}

TEST_F(Gt6853Test, ReadReport) {
  fuchsia_input_report::InputDevice::SyncClient client(std::move(ddk_.FidlClient()));

  zx::channel reader_client, reader_server;
  ASSERT_OK(zx::channel::create(0, &reader_client, &reader_server));
  client.GetInputReportsReader(std::move(reader_server));
  fuchsia_input_report::InputReportsReader::SyncClient reader(std::move(reader_client));
  device_->WaitForNextReader();

  EXPECT_OK(gpio_interrupt_.trigger(0, zx::clock::get_monotonic()));

  fake_i2c_.WaitForTouchDataRead();

  const auto response = reader.ReadInputReports();
  ASSERT_TRUE(response.ok());
  ASSERT_TRUE(response->result.is_response());

  const auto& reports = response->result.response().reports;

  ASSERT_EQ(reports.count(), 1);
  ASSERT_TRUE(reports[0].has_touch());
  ASSERT_TRUE(reports[0].touch().has_contacts());
  ASSERT_EQ(reports[0].touch().contacts().count(), 4);

  EXPECT_EQ(reports[0].touch().contacts()[0].contact_id(), 0);
  EXPECT_EQ(reports[0].touch().contacts()[0].position_x(), 0x005a);
  EXPECT_EQ(reports[0].touch().contacts()[0].position_y(), 0x03b9);

  EXPECT_EQ(reports[0].touch().contacts()[1].contact_id(), 2);
  EXPECT_EQ(reports[0].touch().contacts()[1].position_x(), 0x01f2);
  EXPECT_EQ(reports[0].touch().contacts()[1].position_y(), 0x0044);

  EXPECT_EQ(reports[0].touch().contacts()[2].contact_id(), 1);
  EXPECT_EQ(reports[0].touch().contacts()[2].position_x(), 0x0072);
  EXPECT_EQ(reports[0].touch().contacts()[2].position_y(), 0x0114);

  EXPECT_EQ(reports[0].touch().contacts()[3].contact_id(), 3);
  EXPECT_EQ(reports[0].touch().contacts()[3].position_x(), 0x0138);
  EXPECT_EQ(reports[0].touch().contacts()[3].position_y(), 0x00be);

  EXPECT_TRUE(fake_i2c_.ok());
}

}  // namespace touch
