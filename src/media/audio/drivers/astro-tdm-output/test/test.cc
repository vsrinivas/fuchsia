// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mock-i2c/mock-i2c.h>

#include <mock/ddktl/protocol/gpio.h>
#include <zxtest/zxtest.h>

#include "../audio-stream-out.h"

namespace audio {
namespace astro {

struct Tas27xxGoodInitTest : Tas27xx {
  Tas27xxGoodInitTest(ddk::I2cChannel i2c) : Tas27xx(std::move(i2c)) {}
  zx_status_t Init() override { return ZX_OK; }
};

struct Tas27xxBadInitTest : Tas27xx {
  Tas27xxBadInitTest(ddk::I2cChannel i2c) : Tas27xx(std::move(i2c)) {}
  zx_status_t Init() override { return ZX_ERR_INTERNAL; }
};

struct AstroAudioStreamOutCodecInitTest : public AstroAudioStreamOut {
  AstroAudioStreamOutCodecInitTest(zx_device_t* parent, std::unique_ptr<Tas27xx> codec,
                                   const gpio_protocol_t* audio_enable_gpio)
      : AstroAudioStreamOut(parent) {
    codec_ = std::move(codec);
    audio_en_ = ddk::GpioProtocolClient(audio_enable_gpio);
  }

  zx_status_t InitPDev() override {
    return InitCodec();  // Only init the Codec, no the rest of the audio stream initialization.
  }
  void ShutdownHook() override {}  // Do not perform shutdown since we don't initialize in InitPDev.
};

TEST(AstroAudioStreamOutTest, CodecInitGood) {
  fake_ddk::Bind tester;

  mock_i2c::MockI2c mock_i2c;

  ddk::MockGpio audio_enable_gpio;
  audio_enable_gpio.ExpectWrite(ZX_OK, 1);

  auto codec = std::make_unique<Tas27xxGoodInitTest>(mock_i2c.GetProto());
  auto server = audio::SimpleAudioStream::Create<AstroAudioStreamOutCodecInitTest>(
      fake_ddk::kFakeParent, std::move(codec), audio_enable_gpio.GetProto());

  ASSERT_NOT_NULL(server);
  server->DdkUnbindDeprecated();
  EXPECT_TRUE(tester.Ok());
  audio_enable_gpio.VerifyAndClear();
}

TEST(AstroAudioStreamOutTest, CodecInitBad) {
  fake_ddk::Bind tester;

  mock_i2c::MockI2c mock_i2c;

  ddk::MockGpio audio_enable_gpio;
  audio_enable_gpio.ExpectWrite(ZX_OK, 1);
  audio_enable_gpio.ExpectWrite(ZX_OK, 0);

  auto codec = std::make_unique<Tas27xxBadInitTest>(mock_i2c.GetProto());
  auto server = audio::SimpleAudioStream::Create<AstroAudioStreamOutCodecInitTest>(
      fake_ddk::kFakeParent, std::move(codec), audio_enable_gpio.GetProto());

  ASSERT_NULL(server);
  // Not tester.Ok() since the we don't add the device.
  audio_enable_gpio.VerifyAndClear();
}

}  // namespace astro
}  // namespace audio
