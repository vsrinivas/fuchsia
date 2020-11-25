// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "dai-test.h"

#include <lib/async-loop/default.h>
#include <lib/device-protocol/pdev.h>
#include <lib/zx/clock.h>
#include <string.h>
#include <zircon/device/audio.h>

#include <memory>
#include <utility>

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddktl/protocol/composite.h>

#include "src/media/audio/drivers/dai-test/dai_test_bind.h"

namespace audio::daitest {

DaiTest::DaiTest(zx_device_t* parent, bool is_input)
    : DaiTestDeviceType(parent),
      loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
      is_input_(is_input) {
  loop_.StartThread(is_input_ ? "dai-test-in" : "dai-test-out");
  ddk_proto_id_ = is_input_ ? ZX_PROTOCOL_AUDIO_INPUT_2 : ZX_PROTOCOL_AUDIO_OUTPUT_2;
  plug_time_ = zx::clock::get_monotonic().get();
}

zx_status_t DaiTest::InitPDev() {
  ddk::CompositeProtocolClient composite(parent());
  if (!composite.is_valid()) {
    zxlogf(ERROR, "%s Could not get composite protocol", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  proto_client_ = ddk::DaiProtocolClient(composite, is_input ? "dai-in" : "dai-out");
  if (!proto_client_.is_valid()) {
    zxlogf(ERROR, "%s could not get DAI fragment", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }
  zx::channel channel_remote, channel_local;
  zx_status_t status = zx::channel::create(0, &channel_local, &channel_remote);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not create channel", __FILE__);
    return status;
  }
  status = proto_client_.Connect(std::move(channel_remote));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not coonect to DAI protocol", __FILE__);
    return status;
  }
  dai_.Bind(std::move(channel_local));
  return ZX_OK;
}

void DaiTest::GetProperties(GetPropertiesCallback callback) {
  ::fuchsia::hardware::audio::StreamProperties prop;
  audio_stream_unique_id_t id = AUDIO_STREAM_UNIQUE_ID_BUILTIN_BT;
  std::array<uint8_t, 16> id2;
  for (size_t i = 0; i < 16; ++i) {
    id2[i] = id.data[i];
  }
  prop.set_unique_id(std::move(id2));
  prop.set_is_input(is_input_);
  prop.set_can_mute(false);
  prop.set_can_agc(false);
  prop.set_min_gain_db(0.f);
  prop.set_max_gain_db(0.f);
  prop.set_gain_step_db(0.f);
  prop.set_product(is_input_ ? "DAI-test-in" : "DAI-test-out");
  prop.set_manufacturer("None");
  prop.set_clock_domain(0);
  prop.set_plug_detect_capabilities(::fuchsia::hardware::audio::PlugDetectCapabilities::HARDWIRED);
  callback(std::move(prop));
}

void DaiTest::GetChannel(GetChannelCompleter::Sync& completer) {
  zx::channel channel_remote, channel_local;
  auto status = zx::channel::create(0, &channel_local, &channel_remote);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not create channel", __FILE__);
    return;
  }

  ::fidl::InterfaceRequest<::fuchsia::hardware::audio::StreamConfig> stream_config;
  stream_config.set_channel(std::move(channel_local));
  stream_config_binding_.emplace(this, std::move(stream_config), loop_.dispatcher());
  completer.Reply(std::move(channel_remote));
}

void DaiTest::GetSupportedFormats(GetSupportedFormatsCallback callback) {
  // Pass through formats supported be the DAI.
  ::fuchsia::hardware::audio::Dai_GetRingBufferFormats_Result out_result;
  ZX_ASSERT(dai_->GetRingBufferFormats(&out_result) == ZX_OK);
  callback(std::move(out_result.response().ring_buffer_formats));
}

void DaiTest::CreateRingBuffer(
    ::fuchsia::hardware::audio::Format ring_buffer_format,
    ::fidl::InterfaceRequest<::fuchsia::hardware::audio::RingBuffer> ring_buffer_intf) {
  // We pick the first DAI supported format and the requested ring buffer format.
  // A non-test driver would pick a DAI format based on compatibility with the ring buffer formats
  // and any other requirements.
  ::fuchsia::hardware::audio::Dai_GetDaiFormats_Result out_result;
  ZX_ASSERT(dai_->GetDaiFormats(&out_result) == ZX_OK);
  ::fuchsia::hardware::audio::DaiFormat dai_format = {};
  dai_format.number_of_channels = out_result.response().dai_formats[0].number_of_channels[0];
  dai_format.channels_to_use_bitmask = (1 << dai_format.number_of_channels) - 1;  // Use all.
  dai_format.sample_format = out_result.response().dai_formats[0].sample_formats[0];
  ZX_ASSERT(out_result.response().dai_formats[0].frame_formats[0].is_frame_format_standard());
  dai_format.frame_format.set_frame_format_standard(  // Only standrd frame formats allowed.
      out_result.response().dai_formats[0].frame_formats[0].frame_format_standard());
  dai_format.frame_rate = out_result.response().dai_formats[0].frame_rates[0];
  dai_format.bits_per_slot = out_result.response().dai_formats[0].bits_per_slot[0];
  dai_format.bits_per_sample = out_result.response().dai_formats[0].bits_per_sample[0];
  dai_->CreateRingBuffer(std::move(dai_format), std::move(ring_buffer_format),
                         std::move(ring_buffer_intf));
}

void DaiTest::WatchGainState(WatchGainStateCallback callback) {
  ::fuchsia::hardware::audio::GainState gain_state;
  // Only reply the first time, then don't reply anymore since we don't change (hanging-get).
  static bool first_time = true;
  if (first_time) {
    gain_state.set_muted(false);
    gain_state.set_agc_enabled(false);
    gain_state.set_gain_db(0.f);
    callback(std::move(gain_state));
    first_time = false;
  }
}

void DaiTest::SetGain(::fuchsia::hardware::audio::GainState target_state) {
  // Ignored, no support for gain changing.
}

void DaiTest::WatchPlugState(WatchPlugStateCallback callback) {
  ::fuchsia::hardware::audio::PlugState plug_state;
  // Only reply the first time, then don't reply anymore since we don't change (hanging-get).
  static bool first_time = true;
  if (first_time) {
    plug_state.set_plugged(true);
    plug_state.set_plug_state_time(plug_time_);
    callback(std::move(plug_state));
    first_time = false;
  }
}

static zx_status_t daitest_bind(void* ctx, zx_device_t* device) {
  size_t actual = 0;
  bool is_input = false;
  auto status =
      device_get_metadata(device, DEVICE_METADATA_PRIVATE, &is_input, sizeof(is_input), &actual);
  if (status != ZX_OK || sizeof(is_input) != actual) {
    zxlogf(ERROR, "%s device_get_metadata failed %d", __FILE__, status);
    return status;
  }
  auto dai = std::make_unique<audio::daitest::DaiTest>(device, is_input);
  if (dai == nullptr) {
    zxlogf(ERROR, "%s Could not create DAI driver", __FILE__);
    return ZX_ERR_NO_MEMORY;
  }

  status = dai->InitPDev();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Could not init device", __FILE__);
    return status;
  }
  status = dai->DdkAdd(is_input ? "dai-test-in" : "dai-test-out");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Could not add DAI driver to the DDK", __FILE__);
    return status;
  }
  __UNUSED auto dummy = dai.release();
  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = daitest_bind;
  return ops;
}();

}  // namespace audio::daitest

ZIRCON_DRIVER(dai_test, audio::daitest::driver_ops, "dai-test", "0.1")
