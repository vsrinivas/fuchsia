// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/simple-codec/simple-codec-server.h>

#include "src/media/audio/drivers/tests/realm/codec_test2-bind.h"

namespace audio {
// Codec with bad behavior.
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
    return {.unique_id = "abc", .manufacturer = "def", .product_name = "ghi"};
  }
  zx_status_t Stop() override { return ZX_OK; }
  zx_status_t Start() override { return ZX_OK; }
  bool IsBridgeable() override { return false; }
  void SetBridgedMode(bool enable_bridged_mode) override {}
  DaiSupportedFormats GetDaiFormats() override {
    return {};  // No valid DAI formats returned to test drivers/configurator handling.
  }
  zx::result<CodecFormatInfo> SetDaiFormat(const DaiFormat& format) override {
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

ZIRCON_DRIVER(codec_test2, audio::driver_ops, "zircon", "0.1");
