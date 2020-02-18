// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mock-i2c/mock-i2c.h>

#include <mock/ddktl/protocol/gpio.h>
#include <zxtest/zxtest.h>

#include "../audio-stream-out.h"

namespace audio {
namespace sherlock {

struct Tas5720GoodInitTest : Tas5720 {
  Tas5720GoodInitTest(ddk::I2cChannel i2c) : Tas5720(std::move(i2c)) {}
  zx_status_t Init(std::optional<uint8_t> slot) override { return ZX_OK; }
  zx_status_t SetGain(float gain) override { return ZX_OK; }
};

struct Tas5720BadInitTest : Tas5720 {
  Tas5720BadInitTest(ddk::I2cChannel i2c) : Tas5720(std::move(i2c)) {}
  zx_status_t Init(std::optional<uint8_t> slot) override { return ZX_ERR_INTERNAL; }
};

struct Tas5720SomeBadInitTest : Tas5720 {
  Tas5720SomeBadInitTest(ddk::I2cChannel i2c) : Tas5720(std::move(i2c)) {}
  zx_status_t Init(std::optional<uint8_t> slot) override {
    if (slot.value() == 0) {
      return ZX_OK;
    } else {
      return ZX_ERR_INTERNAL;
    }
  }
  zx_status_t SetGain(float gain) override {
    return ZX_OK;
  }  // Gains work since not all Inits fail.
};

struct SherlockAudioStreamOutCodecInitTest : public SherlockAudioStreamOut {
  SherlockAudioStreamOutCodecInitTest(zx_device_t* parent,
                                      fbl::Array<std::unique_ptr<Tas5720>> codecs,
                                      const gpio_protocol_t* audio_enable_gpio)
      : SherlockAudioStreamOut(parent) {
    codecs_ = std::move(codecs);
    audio_en_ = ddk::GpioProtocolClient(audio_enable_gpio);
  }

  zx_status_t InitPdev() TA_REQ(domain_token()) override {
    return InitCodecs();  // Only init the Codec, no the rest of the audio stream initialization.
  }
  void ShutdownHook() override {}  // Do not perform shutdown since we don't initialize in InitPDev.
};

TEST(SherlockAudioStreamOutTest, CodecInitGood) {
  fake_ddk::Bind tester;

  mock_i2c::MockI2c mock_i2c;

  ddk::MockGpio audio_enable_gpio;
  audio_enable_gpio.ExpectWrite(ZX_OK, 1);

  auto codecs = fbl::Array(new std::unique_ptr<Tas5720>[3], 3);
  codecs[0] = std::make_unique<Tas5720GoodInitTest>(mock_i2c.GetProto());
  codecs[1] = std::make_unique<Tas5720GoodInitTest>(mock_i2c.GetProto());
  codecs[2] = std::make_unique<Tas5720GoodInitTest>(mock_i2c.GetProto());
  auto server = audio::SimpleAudioStream::Create<SherlockAudioStreamOutCodecInitTest>(
      fake_ddk::kFakeParent, std::move(codecs), audio_enable_gpio.GetProto());

  ASSERT_NOT_NULL(server);
  server->DdkUnbindDeprecated();
  EXPECT_TRUE(tester.Ok());
  audio_enable_gpio.VerifyAndClear();
}

TEST(SherlockAudioStreamOutTest, CodecInitBad) {
  fake_ddk::Bind tester;

  mock_i2c::MockI2c mock_i2c;

  ddk::MockGpio audio_enable_gpio;
  audio_enable_gpio.ExpectWrite(ZX_OK, 1);
  audio_enable_gpio.ExpectWrite(ZX_OK, 0);

  auto codecs = fbl::Array(new std::unique_ptr<Tas5720>[3], 3);
  codecs[0] = std::make_unique<Tas5720BadInitTest>(mock_i2c.GetProto());
  codecs[1] = std::make_unique<Tas5720BadInitTest>(mock_i2c.GetProto());
  codecs[2] = std::make_unique<Tas5720BadInitTest>(mock_i2c.GetProto());
  auto server = audio::SimpleAudioStream::Create<SherlockAudioStreamOutCodecInitTest>(
      fake_ddk::kFakeParent, std::move(codecs), audio_enable_gpio.GetProto());

  ASSERT_NULL(server);
  // Not tester.Ok() since the we don't add the device.
  audio_enable_gpio.VerifyAndClear();
}

TEST(SherlockAudioStreamOutTest, CodecInitOnlySomeBad) {
  fake_ddk::Bind tester;

  mock_i2c::MockI2c mock_i2c;

  ddk::MockGpio audio_enable_gpio;
  audio_enable_gpio.ExpectWrite(ZX_OK, 1);
  audio_enable_gpio.ExpectWrite(ZX_OK, 0);

  auto codecs = fbl::Array(new std::unique_ptr<Tas5720>[3], 3);
  codecs[0] = std::make_unique<Tas5720SomeBadInitTest>(mock_i2c.GetProto());
  codecs[1] = std::make_unique<Tas5720SomeBadInitTest>(mock_i2c.GetProto());
  codecs[2] = std::make_unique<Tas5720SomeBadInitTest>(mock_i2c.GetProto());
  auto server = audio::SimpleAudioStream::Create<SherlockAudioStreamOutCodecInitTest>(
      fake_ddk::kFakeParent, std::move(codecs), audio_enable_gpio.GetProto());

  ASSERT_NULL(server);
  // Not tester.Ok() since the we don't add the device.
  audio_enable_gpio.VerifyAndClear();
}

}  // namespace sherlock
}  // namespace audio
