// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <lib/mock-i2c/mock-i2c.h>
#include <lib/simple-codec/simple-codec-client.h>

#include <memory>

#include <sdk/lib/inspect/testing/cpp/zxtest/inspect.h>
#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"
#include "src/media/audio/drivers/codecs/da7219/da7219.h"

namespace audio {

using inspect::InspectTestHelper;

class Da7219Test : public InspectTestHelper, public zxtest::Test {
  void SetUp() override { fake_root_ = MockDevice::FakeRootParent(); }

  void TearDown() override { mock_i2c_.VerifyAndClear(); }

 protected:
  std::shared_ptr<zx_device> fake_root_;
  mock_i2c::MockI2c mock_i2c_;
};

struct Da7219Codec : public Da7219 {
  explicit Da7219Codec(zx_device_t* parent, ddk::I2cChannel i2c) : Da7219(parent, std::move(i2c)) {}
  codec_protocol_t GetProto() { return {&this->codec_protocol_ops_, this}; }
};

TEST_F(Da7219Test, GetInfo) {
  // IDs check.
  mock_i2c_.ExpectWrite({0x81}).ExpectReadStop({0x23}, ZX_OK);
  mock_i2c_.ExpectWrite({0x82}).ExpectReadStop({0x93}, ZX_OK);
  mock_i2c_.ExpectWrite({0x83}).ExpectReadStop({0x02}, ZX_OK);

  ASSERT_OK(
      SimpleCodecServer::CreateAndAddToDdk<Da7219Codec>(fake_root_.get(), mock_i2c_.GetProto()));
  auto* child_dev = fake_root_->GetLatestChild();
  auto codec = child_dev->GetDeviceContext<Da7219Codec>();
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  auto info = client.GetInfo();
  EXPECT_EQ(info.value().unique_id.compare(""), 0);
  EXPECT_EQ(info.value().manufacturer.compare("Dialog"), 0);
  EXPECT_EQ(info.value().product_name.compare("DA7219"), 0);
}

}  // namespace audio
