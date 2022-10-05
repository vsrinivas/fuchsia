// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.audio/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>

#include <ddktl/device.h>

#include "src/media/audio/drivers/tests/realm/dai_test-bind.h"

namespace audio {

class TestDai;
using TestDaiDeviceType =
    ddk::Device<TestDai, ddk::Messageable<fuchsia_hardware_audio::DaiConnector>::Mixin>;

class TestDai : public TestDaiDeviceType,
                public ddk::internal::base_protocol,
                public fidl::WireServer<fuchsia_hardware_audio::Dai> {
 public:
  TestDai(zx_device_t* parent)
      : TestDaiDeviceType(parent), loop_(&kAsyncLoopConfigNeverAttachToThread) {
    ddk_proto_id_ = ZX_PROTOCOL_DAI;
    loop_.StartThread("test-dai-driver");
  }

  void DdkRelease() {}

 protected:
  // FIDL LLCPP method for fuchsia.hardware.audio.DaiConnector.
  void Connect(ConnectRequestView request, ConnectCompleter::Sync& completer) override {
    fidl::BindServer<fidl::WireServer<fuchsia_hardware_audio::Dai>>(
        loop_.dispatcher(), std::move(request->dai_protocol), this);
  }

  // FIDL LLCPP methods for fuchsia.hardware.audio.Dai.
  void Reset(ResetCompleter::Sync& completer) override { completer.Reply(); }
  void GetProperties(GetPropertiesCompleter::Sync& completer) override {
    fidl::Arena arena;
    auto builder = fuchsia_hardware_audio::wire::DaiProperties::Builder(arena);
    builder.is_input(false);
    builder.manufacturer("test");
    builder.product_name("test");
    completer.Reply(builder.Build());
  }
  void GetHealthState(GetHealthStateCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }
  void SignalProcessingConnect(SignalProcessingConnectRequestView request,
                               SignalProcessingConnectCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }
  void GetRingBufferFormats(GetRingBufferFormatsCompleter::Sync& completer) override {
    fidl::Arena arena;
    fidl::VectorView<fuchsia_hardware_audio::wire::ChannelSet> channel_sets(arena, 1);
    fidl::VectorView<fuchsia_hardware_audio::wire::ChannelAttributes> attributes(arena, 2);
    fuchsia_hardware_audio::wire::SampleFormat sample_formats[1] = {
        fuchsia_hardware_audio::wire::SampleFormat::kPcmSigned};
    uint32_t rates[1] = {48'000};
    uint8_t bytes_per_sample[1] = {2};
    uint8_t valid_bits_per_sample[1] = {16};
    auto formats =
        fuchsia_hardware_audio::wire::PcmSupportedFormats::Builder(arena)
            .channel_sets(std::move(channel_sets))
            .sample_formats(
                fidl::VectorView<fuchsia_hardware_audio::wire::SampleFormat>::FromExternal(
                    sample_formats, 1))
            .frame_rates(fidl::VectorView<uint32_t>::FromExternal(rates, 1))
            .bytes_per_sample(fidl::VectorView<uint8_t>::FromExternal(bytes_per_sample, 1))
            .valid_bits_per_sample(
                fidl::VectorView<uint8_t>::FromExternal(valid_bits_per_sample, 1))
            .Build();

    fidl::VectorView<fuchsia_hardware_audio::wire::SupportedFormats> all_formats(arena, 1);
    all_formats[0] = fuchsia_hardware_audio::wire::SupportedFormats::Builder(arena)
                         .pcm_supported_formats(std::move(formats))
                         .Build();

    fuchsia_hardware_audio::wire::DaiGetRingBufferFormatsResponse response;
    response.ring_buffer_formats = std::move(all_formats);
    completer.Reply(::fit::ok(&response));
  }

  void GetDaiFormats(GetDaiFormatsCompleter::Sync& completer) override {
    fidl::Arena arena;
    fidl::VectorView<fuchsia_hardware_audio::wire::DaiSupportedFormats> all_formats(arena, 1);
    fuchsia_hardware_audio::wire::DaiSupportedFormats formats;
    uint32_t number_of_channels[] = {2};
    fuchsia_hardware_audio::wire::DaiSampleFormat sample_formats[] = {
        fuchsia_hardware_audio::wire::DaiSampleFormat::kPcmSigned};
    fuchsia_hardware_audio::wire::DaiFrameFormat frame_formats[] = {
        fuchsia_hardware_audio::wire::DaiFrameFormat::WithFrameFormatStandard(
            fuchsia_hardware_audio::wire::DaiFrameFormatStandard::kI2S)};
    uint32_t frame_rates[] = {48'000};
    uint8_t bits_per_slot[] = {32};
    uint8_t bits_per_sample[] = {24};
    formats.number_of_channels =
        fidl::VectorView<uint32_t>::FromExternal(number_of_channels, std::size(number_of_channels));
    formats.sample_formats =
        fidl::VectorView<fuchsia_hardware_audio::wire::DaiSampleFormat>::FromExternal(
            sample_formats, std::size(sample_formats));
    formats.frame_formats =
        fidl::VectorView<fuchsia_hardware_audio::wire::DaiFrameFormat>::FromExternal(
            frame_formats, std::size(frame_formats));
    formats.frame_rates =
        fidl::VectorView<uint32_t>::FromExternal(frame_rates, std::size(frame_rates));
    formats.bits_per_slot =
        fidl::VectorView<uint8_t>::FromExternal(bits_per_slot, std::size(bits_per_slot));
    formats.bits_per_sample =
        fidl::VectorView<uint8_t>::FromExternal(bits_per_sample, std::size(bits_per_sample));

    all_formats[0] = formats;

    fuchsia_hardware_audio::wire::DaiGetDaiFormatsResponse response;
    response.dai_formats = std::move(all_formats);
    completer.Reply(::fit::ok(&response));
  }
  void CreateRingBuffer(CreateRingBufferRequestView request,
                        CreateRingBufferCompleter::Sync& completer) override {
    // Not testing the ring buffer interface with this driver, we drop this request.
  }

  async::Loop loop_;
};

zx_status_t test_bind(void* ctx, zx_device_t* parent) {
  auto dai = std::make_unique<audio::TestDai>(parent);
  if (dai == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }
  zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, 1},
      {BIND_PLATFORM_DEV_DID, 0, 2},
  };
  zx_status_t status = dai->DdkAdd(ddk::DeviceAddArgs("test").set_props(props));
  if (status != ZX_OK) {
    return status;
  }
  [[maybe_unused]] auto unused = dai.release();
  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = test_bind;
  return ops;
}();

}  // namespace audio

ZIRCON_DRIVER(dai_test, audio::driver_ops, "zircon", "0.1");
