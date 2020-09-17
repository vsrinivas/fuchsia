// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ft8201.h"

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

 protected:
  zx_status_t Transact(const uint8_t* write_buffer, size_t write_buffer_size, uint8_t* read_buffer,
                       size_t* read_buffer_size) override {
    constexpr uint8_t kTouchData[] = {
        // clang-format off
        0x5a, 0x27, 0x71, 0xf1, 0x41, 0xe7,
        0xa8, 0x30, 0xcc, 0x42, 0x61, 0xa0,
        0xf4, 0x9b, 0x57, 0x79, 0xc1, 0x12,
        0x92, 0x95, 0x9a, 0x23, 0x43, 0xc2,
        // clang-format on
    };

    if (write_buffer_size != 1) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    const uint8_t address = write_buffer[0];
    write_buffer++;
    write_buffer_size--;

    if (address == 0x02) {
      read_buffer[0] = 4;
      *read_buffer_size = 1;
    } else if (address == 0x03) {
      // The interrupt or timeout has been received and the driver is reading out the data
      // registers.
      memcpy(read_buffer, kTouchData, sizeof(kTouchData));
      *read_buffer_size = sizeof(kTouchData);
      sync_completion_signal(&read_completion_);
    } else if (address == 0xa3) {
      read_buffer[0] = 0x82;  // Indicate that the firmware on the IC is valid.
      *read_buffer_size = 1;
    } else if (address == 0xa6) {
      read_buffer[0] = 0x05;  // Current firmware version is 0x05, this skips the firmware download.
      *read_buffer_size = 1;
    }

    return ZX_OK;
  }

 private:
  sync_completion_t read_completion_;
};

class Ft8201Test : public zxtest::Test {
 public:
  void SetUp() override {
    composite_protocol_ops_t composite_protocol = {
        .get_fragment_count = GetFragmentCount,
        .get_fragments = GetFragments,
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

    auto status = Ft8201Device::CreateAndGetDevice(nullptr, fake_ddk::kFakeParent);
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
  Ft8201Device* device_ = nullptr;

 private:
  static uint32_t GetFragmentCount(__UNUSED void* ctx) { return 2; }

  static void GetFragments(__UNUSED void* ctx, zx_device_t** out_fragment_list,
                           size_t fragment_count, size_t* out_fragment_actual) {
    *out_fragment_actual = 0;
    for (size_t i = 0; i < std::max<size_t>(2, fragment_count); i++) {
      // Set each device to kFakeParent so fake_ddk will supply protocols for each fragment.
      out_fragment_list[i] = fake_ddk::kFakeParent;
      (*out_fragment_actual)++;
    }
  }

  ddk::MockGpio mock_gpio_;
};

TEST_F(Ft8201Test, GetDescriptor) {
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
    EXPECT_EQ(contact.position_x().range.max, 1279);
    EXPECT_EQ(contact.position_x().unit.type, fuchsia_input_report::UnitType::NONE);
    EXPECT_EQ(contact.position_x().unit.exponent, 0);

    EXPECT_EQ(contact.position_y().range.min, 0);
    EXPECT_EQ(contact.position_y().range.max, 799);
    EXPECT_EQ(contact.position_y().unit.type, fuchsia_input_report::UnitType::NONE);
    EXPECT_EQ(contact.position_y().unit.exponent, 0);

    EXPECT_EQ(contact.pressure().range.min, 0);
    EXPECT_EQ(contact.pressure().range.max, 0xff);
    EXPECT_EQ(contact.pressure().unit.type, fuchsia_input_report::UnitType::NONE);
    EXPECT_EQ(contact.pressure().unit.exponent, 0);
  }

  EXPECT_EQ(response->descriptor.touch().input().max_contacts(), 10);
  EXPECT_EQ(response->descriptor.touch().input().touch_type(),
            fuchsia_input_report::TouchType::TOUCHSCREEN);
}

TEST_F(Ft8201Test, ReadReport) {
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

  EXPECT_EQ(reports[0].touch().contacts()[0].contact_id(), 0x7);
  EXPECT_EQ(reports[0].touch().contacts()[0].position_x(), 0xa27);
  EXPECT_EQ(reports[0].touch().contacts()[0].position_y(), 0x1f1);
  EXPECT_EQ(reports[0].touch().contacts()[0].pressure(), 0x41);

  EXPECT_EQ(reports[0].touch().contacts()[1].contact_id(), 0xc);
  EXPECT_EQ(reports[0].touch().contacts()[1].position_x(), 0x830);
  EXPECT_EQ(reports[0].touch().contacts()[1].position_y(), 0xc42);
  EXPECT_EQ(reports[0].touch().contacts()[1].pressure(), 0x61);

  EXPECT_EQ(reports[0].touch().contacts()[2].contact_id(), 0x5);
  EXPECT_EQ(reports[0].touch().contacts()[2].position_x(), 0x49b);
  EXPECT_EQ(reports[0].touch().contacts()[2].position_y(), 0x779);
  EXPECT_EQ(reports[0].touch().contacts()[2].pressure(), 0xc1);

  EXPECT_EQ(reports[0].touch().contacts()[3].contact_id(), 0x9);
  EXPECT_EQ(reports[0].touch().contacts()[3].position_x(), 0x295);
  EXPECT_EQ(reports[0].touch().contacts()[3].position_y(), 0xa23);
  EXPECT_EQ(reports[0].touch().contacts()[3].pressure(), 0x43);
}

}  // namespace touch
