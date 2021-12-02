// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ti-tca6408a.h"

#include <lib/ddk/metadata.h>
#include <lib/fake-i2c/fake-i2c.h>

#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"

namespace gpio {

class FakeTiTca6408aDevice : public fake_i2c::FakeI2c {
 public:
  uint8_t input_port() const { return input_port_; }
  void set_input_port(uint8_t input_port) { input_port_ = input_port; }
  uint8_t output_port() const { return output_port_; }
  uint8_t polarity_inversion() const { return polarity_inversion_; }
  uint8_t configuration() const { return configuration_; }

 protected:
  zx_status_t Transact(const uint8_t* write_buffer, size_t write_buffer_size, uint8_t* read_buffer,
                       size_t* read_buffer_size) override {
    if (write_buffer_size > 2) {
      return ZX_ERR_IO;
    }

    const uint8_t address = write_buffer[0];

    uint8_t* reg = nullptr;
    switch (address) {
      case 0:
        reg = &input_port_;
        break;
      case 1:
        reg = &output_port_;
        break;
      case 2:
        reg = &polarity_inversion_;
        break;
      case 3:
        reg = &configuration_;
        break;
      default:
        return ZX_ERR_IO;
    };

    if (write_buffer_size == 1) {
      *read_buffer = *reg;
      *read_buffer_size = 1;
    } else {
      *reg = write_buffer[1];
    }

    return ZX_OK;
  }

 private:
  uint8_t input_port_ = 0;
  uint8_t output_port_ = 0b1111'1111;
  uint8_t polarity_inversion_ = 0;
  uint8_t configuration_ = 0b1111'1111;
};

class TiTca6408aTest : public zxtest::Test {
 public:
  TiTca6408aTest() : ddk_(MockDevice::FakeRootParent()) {}

  void SetUp() override {
    constexpr uint32_t kPinIndexOffset = 100;
    ddk_->SetMetadata(DEVICE_METADATA_PRIVATE, &kPinIndexOffset, sizeof(kPinIndexOffset));
    ddk_->AddProtocol(ZX_PROTOCOL_I2C, fake_i2c_.GetProto()->ops, fake_i2c_.GetProto()->ctx, "i2c");

    // This TiTca6408a gets released by the MockDevice destructor.
    ASSERT_OK(TiTca6408a::Create(nullptr, ddk_.get()));

    MockDevice* device = ddk_->GetLatestChild();
    ASSERT_NOT_NULL(device);

    TiTca6408a* dut = device->GetDeviceContext<TiTca6408a>();
    ASSERT_NOT_NULL(dut);

    const gpio_impl_protocol_t proto{.ops = &dut->gpio_impl_protocol_ops_, .ctx = dut};
    gpio_ = ddk::GpioImplProtocolClient(&proto);

    EXPECT_EQ(fake_i2c_.polarity_inversion(), 0);
  }

 protected:
  FakeTiTca6408aDevice fake_i2c_;
  ddk::GpioImplProtocolClient gpio_;

 private:
  std::shared_ptr<MockDevice> ddk_;
};

TEST_F(TiTca6408aTest, ConfigInOut) {
  EXPECT_EQ(fake_i2c_.output_port(), 0b1111'1111);
  EXPECT_EQ(fake_i2c_.configuration(), 0b1111'1111);

  EXPECT_OK(gpio_.ConfigOut(100, 0));
  EXPECT_EQ(fake_i2c_.output_port(), 0b1111'1110);
  EXPECT_EQ(fake_i2c_.configuration(), 0b1111'1110);

  EXPECT_OK(gpio_.ConfigIn(100, GPIO_NO_PULL));
  EXPECT_EQ(fake_i2c_.output_port(), 0b1111'1110);
  EXPECT_EQ(fake_i2c_.configuration(), 0b1111'1111);

  EXPECT_OK(gpio_.ConfigOut(105, 0));
  EXPECT_EQ(fake_i2c_.output_port(), 0b1101'1110);
  EXPECT_EQ(fake_i2c_.configuration(), 0b1101'1111);

  EXPECT_OK(gpio_.ConfigIn(105, GPIO_NO_PULL));
  EXPECT_EQ(fake_i2c_.output_port(), 0b1101'1110);
  EXPECT_EQ(fake_i2c_.configuration(), 0b1111'1111);

  EXPECT_OK(gpio_.ConfigOut(105, 1));
  EXPECT_EQ(fake_i2c_.output_port(), 0b1111'1110);
  EXPECT_EQ(fake_i2c_.configuration(), 0b1101'1111);

  EXPECT_OK(gpio_.ConfigOut(107, 0));
  EXPECT_EQ(fake_i2c_.output_port(), 0b0111'1110);
  EXPECT_EQ(fake_i2c_.configuration(), 0b0101'1111);
}

TEST_F(TiTca6408aTest, Read) {
  fake_i2c_.set_input_port(0x55);

  uint8_t value;

  EXPECT_OK(gpio_.Read(100, &value));
  EXPECT_EQ(value, 1);

  EXPECT_OK(gpio_.Read(103, &value));
  EXPECT_EQ(value, 0);

  EXPECT_OK(gpio_.Read(104, &value));
  EXPECT_EQ(value, 1);

  EXPECT_OK(gpio_.Read(107, &value));
  EXPECT_EQ(value, 0);

  EXPECT_OK(gpio_.Read(105, nullptr));
}

TEST_F(TiTca6408aTest, Write) {
  EXPECT_EQ(fake_i2c_.output_port(), 0b1111'1111);
  EXPECT_EQ(fake_i2c_.configuration(), 0b1111'1111);

  EXPECT_OK(gpio_.Write(100, 0));
  EXPECT_EQ(fake_i2c_.output_port(), 0b1111'1110);
  EXPECT_EQ(fake_i2c_.configuration(), 0b1111'1111);

  EXPECT_OK(gpio_.Write(101, 0));
  EXPECT_EQ(fake_i2c_.output_port(), 0b1111'1100);
  EXPECT_EQ(fake_i2c_.configuration(), 0b1111'1111);

  EXPECT_OK(gpio_.Write(103, 0));
  EXPECT_EQ(fake_i2c_.output_port(), 0b1111'0100);
  EXPECT_EQ(fake_i2c_.configuration(), 0b1111'1111);

  EXPECT_OK(gpio_.Write(104, 0));
  EXPECT_EQ(fake_i2c_.output_port(), 0b1110'0100);
  EXPECT_EQ(fake_i2c_.configuration(), 0b1111'1111);

  EXPECT_OK(gpio_.Write(106, 0));
  EXPECT_EQ(fake_i2c_.output_port(), 0b1010'0100);
  EXPECT_EQ(fake_i2c_.configuration(), 0b1111'1111);

  EXPECT_OK(gpio_.Write(101, 1));
  EXPECT_EQ(fake_i2c_.output_port(), 0b1010'0110);
  EXPECT_EQ(fake_i2c_.configuration(), 0b1111'1111);

  EXPECT_OK(gpio_.Write(104, 1));
  EXPECT_EQ(fake_i2c_.output_port(), 0b1011'0110);
  EXPECT_EQ(fake_i2c_.configuration(), 0b1111'1111);
}

TEST_F(TiTca6408aTest, InvalidArgs) {
  EXPECT_OK(gpio_.ConfigIn(107, GPIO_NO_PULL));
  EXPECT_NOT_OK(gpio_.ConfigIn(108, GPIO_NO_PULL));
  EXPECT_NOT_OK(gpio_.ConfigIn(107, GPIO_PULL_UP));
  EXPECT_NOT_OK(gpio_.ConfigIn(100, GPIO_PULL_DOWN));
  EXPECT_NOT_OK(gpio_.ConfigOut(0, 0));
  EXPECT_NOT_OK(gpio_.ConfigOut(1, 1));
}

}  // namespace gpio
