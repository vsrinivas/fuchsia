// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/simple-codec/simple-codec-client.h>
#include <lib/simple-codec/simple-codec-server.h>
#include <lib/sync/completion.h>

#include <zxtest/zxtest.h>

namespace {
static const char* kTestId = "test id";
static const char* kTestManufacturer = "test man";
static const char* kTestProduct = "test prod";
}  // namespace
namespace audio {

// Server tests.
struct TestCodec : public SimpleCodecServer {
  explicit TestCodec() : SimpleCodecServer(nullptr), proto_({&codec_protocol_ops_, this}) {}
  codec_protocol_t GetProto() { return {&this->codec_protocol_ops_, this}; }

  zx_status_t Shutdown() override { return ZX_OK; }
  zx::status<DriverIds> Initialize() override {
    return zx::ok(DriverIds{.vendor_id = 0, .device_id = 0});
  }
  zx_status_t Reset() override { return ZX_ERR_NOT_SUPPORTED; }
  Info GetInfo() override {
    return {.unique_id = kTestId, .manufacturer = kTestManufacturer, .product_name = kTestProduct};
  }
  zx_status_t Stop() override { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t Start() override { return ZX_ERR_NOT_SUPPORTED; }
  bool IsBridgeable() override { return false; }
  void SetBridgedMode(bool enable_bridged_mode) override {}
  std::vector<DaiSupportedFormats> GetDaiFormats() override { return {}; }
  zx_status_t SetDaiFormat(const DaiFormat& format) override { return ZX_ERR_NOT_SUPPORTED; }
  GainFormat GetGainFormat() override { return {}; }
  GainState GetGainState() override { return {}; }
  void SetGainState(GainState state) override {}
  PlugState GetPlugState() override { return {}; }
  codec_protocol_t proto_ = {};
};

TEST(SimpleCodecTest, ChannelConnection) {
  auto codec = SimpleCodecServer::Create<TestCodec>();
  ASSERT_NOT_NULL(codec);
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  auto info = client.GetInfo();
  ASSERT_TRUE(info.is_ok());
  ASSERT_EQ(info->unique_id.compare(kTestId), 0);
  ASSERT_EQ(info->manufacturer.compare(kTestManufacturer), 0);
  ASSERT_EQ(info->product_name.compare(kTestProduct), 0);
}

}  // namespace audio
