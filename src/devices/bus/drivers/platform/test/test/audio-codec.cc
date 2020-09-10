// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/codec.h>

#define DRIVER_NAME "test-codec"

namespace codec {

class TestAudioCodecDevice;
using DeviceType = ddk::Device<TestAudioCodecDevice, ddk::Unbindable>;

class TestAudioCodecDevice : public DeviceType,
                             public ddk::CodecProtocol<TestAudioCodecDevice, ddk::base_protocol> {
 public:
  static zx_status_t Create(zx_device_t* parent);

  explicit TestAudioCodecDevice(zx_device_t* parent) : DeviceType(parent) {}

  zx_status_t Create(std::unique_ptr<TestAudioCodecDevice>* out);

  void CodecReset(codec_reset_callback callback, void* cookie) { callback(cookie, ZX_OK); }
  void CodecStop(codec_reset_callback callback, void* cookie) { callback(cookie, ZX_OK); }
  void CodecStart(codec_reset_callback callback, void* cookie) { callback(cookie, ZX_OK); }
  void CodecGetInfo(codec_get_info_callback callback, void* cookie) {
    info_t info = {};
    info.unique_id = "test_id";
    info.manufacturer = "test_man";
    info.product_name = "test_product";
    callback(cookie, &info);
  }
  void CodecIsBridgeable(codec_is_bridgeable_callback callback, void* cookie) {
    callback(cookie, true);
  }
  void CodecSetBridgedMode(bool enable_bridged_mode, codec_set_bridged_mode_callback callback,
                           void* cookie) {
    callback(cookie);
  }
  void CodecGetDaiFormats(codec_get_dai_formats_callback callback, void* cookie) {
    dai_supported_formats_t formats[3] = {};
    uint8_t bits_per_sample[3] = {1, 99, 253};
    formats[0].bits_per_sample_list = bits_per_sample;
    formats[0].bits_per_sample_count = 3;
    uint32_t number_of_channels[3] = {0, 1, 200};
    formats[1].number_of_channels_list = number_of_channels;
    formats[1].number_of_channels_count = 3;
    uint32_t frame_rates[1] = {48000};
    formats[2].frame_rates_list = frame_rates;
    formats[2].frame_rates_count = 1;
    callback(cookie, ZX_OK, formats, 3);
  }
  void CodecSetDaiFormat(const dai_format_t* format, codec_set_dai_format_callback callback,
                         void* cookie) {
    callback(cookie, ZX_OK);
  }
  void CodecGetGainFormat(codec_get_gain_format_callback callback, void* cookie) {
    gain_format_t format = {};
    format.can_agc = true;
    format.min_gain = -99.99f;
    callback(cookie, &format);
  }
  void CodecGetGainState(codec_get_gain_state_callback callback, void* cookie) {
    gain_state_t gain_state = {};
    gain_state.gain = 123.456f;
    gain_state.muted = true;
    gain_state.agc_enable = false;
    callback(cookie, &gain_state);
  }
  void CodecSetGainState(const gain_state_t* gain_state, codec_set_gain_state_callback callback,
                         void* cookie) {
    callback(cookie);
  }
  void CodecGetPlugState(codec_get_plug_state_callback callback, void* cookie) {
    plug_state_t plug_state = {};
    plug_state.hardwired = false;
    plug_state.plugged = true;
    callback(cookie, &plug_state);
  }

  // Methods required by the ddk mixins
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();
};

zx_status_t TestAudioCodecDevice::Create(zx_device_t* parent) {
  auto dev = std::make_unique<TestAudioCodecDevice>(parent);

  zxlogf(INFO, "TestAudioCodecDevice::Create: %s ", DRIVER_NAME);

  auto status = dev->DdkAdd("test-codec");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed: %d", __func__, status);
    return status;
  }
  // devmgr is now in charge of dev.
  __UNUSED auto ptr = dev.release();

  return ZX_OK;
}

void TestAudioCodecDevice::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void TestAudioCodecDevice::DdkRelease() { delete this; }

zx_status_t test_codec_bind(void* ctx, zx_device_t* parent) {
  return TestAudioCodecDevice::Create(parent);
}

constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t driver_ops = {};
  driver_ops.version = DRIVER_OPS_VERSION;
  driver_ops.bind = test_codec_bind;
  return driver_ops;
}();

}  // namespace codec

ZIRCON_DRIVER_BEGIN(test_codec, codec::driver_ops, "zircon", "0.1", 4)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TEST),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_PBUS_TEST),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TEST_AUDIO_CODEC), ZIRCON_DRIVER_END(test_codec)
