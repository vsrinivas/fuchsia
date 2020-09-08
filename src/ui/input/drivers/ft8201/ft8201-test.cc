// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ft8201.h"

#include <lib/device-protocol/i2c-channel.h>
#include <lib/fake-i2c/fake-i2c.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/zx/clock.h>

#include <ddk/protocol/composite.h>
#include <ddktl/metadata/light-sensor.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <mock/ddktl/protocol/gpio.h>
#include <zxtest/zxtest.h>

namespace touch {

class FakeTouchDevice : public fake_i2c::FakeI2c {
 protected:
  zx_status_t Transact(const uint8_t* write_buffer, size_t write_buffer_size, uint8_t* read_buffer,
                       size_t* read_buffer_size) override {
    return ZX_ERR_NOT_SUPPORTED;
  }
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
  }

  EXPECT_EQ(response->descriptor.touch().input().max_contacts(), 10);
  EXPECT_EQ(response->descriptor.touch().input().touch_type(),
            fuchsia_input_report::TouchType::TOUCHSCREEN);
}

}  // namespace touch
