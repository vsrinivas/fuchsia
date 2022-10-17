// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/simple-codec/simple-codec-server.h>

#include "src/media/audio/drivers/tests/realm/codec_test-bind.h"

namespace audio {
// Codec with good behavior.
class Test : public SimpleCodecServer {
 public:
  static zx_status_t Create(zx_device_t* parent);

  explicit Test(zx_device_t* device) : SimpleCodecServer(device) {}

  // Implementation for SimpleCodecServer.
  zx_status_t Shutdown() override { return ZX_OK; }

 protected:
  // Implementation for SimpleCodecServer.
  zx::result<DriverIds> Initialize() override {
    return zx::ok(DriverIds{
        .vendor_id = 1,
        .device_id = 2,
    });
  }
  zx_status_t Reset() override { return ZX_OK; }
  Info GetInfo() override {
    return {.unique_id = "123", .manufacturer = "456", .product_name = "789"};
  }
  zx_status_t Stop() override { return ZX_OK; }
  zx_status_t Start() override { return ZX_OK; }
  bool IsBridgeable() override { return false; }
  void SetBridgedMode(bool enable_bridged_mode) override {}
  DaiSupportedFormats GetDaiFormats() override {
    return {
        .number_of_channels = {2, 4, 6, 8},
        .sample_formats = {SampleFormat::PCM_SIGNED},
        .frame_formats = {FrameFormat::I2S, FrameFormat::TDM1},
        .frame_rates = {24'000, 48'000, 96'000},
        .bits_per_slot = {16, 32},
        .bits_per_sample = {16, 24, 32},
    };
  }
  zx::result<CodecFormatInfo> SetDaiFormat(const DaiFormat& format) override {
    if (format.channels_to_use_bitmask != 1)  // First Codec gets the first TDM slot.
      return zx::error(ZX_ERR_NOT_SUPPORTED);
    return zx::ok(CodecFormatInfo{});
  }
  GainFormat GetGainFormat() override { return {}; }
  GainState GetGainState() override { return {}; }
  void SetGainState(GainState state) override {}
};

zx_status_t test_bind(void* ctx, zx_device_t* parent) {
  return SimpleCodecServer::CreateAndAddToDdk<Test>(parent);
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = test_bind;
  return ops;
}();

}  // namespace audio

ZIRCON_DRIVER(codec_test, audio::driver_ops, "zircon", "0.1");
