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
  void Reset(ResetRequestView Request, ResetCompleter::Sync& completer) override {
    completer.Reply();
  }
  void GetProperties(GetPropertiesRequestView request,
                     GetPropertiesCompleter::Sync& completer) override {
    fidl::Arena arena;
    auto builder = fuchsia_hardware_audio::wire::DaiProperties::Builder(arena);
    builder.is_input(false);
    builder.manufacturer("test");
    builder.product_name("test");
    completer.Reply(builder.Build());
  }
  void GetHealthState(GetHealthStateRequestView request,
                      GetHealthStateCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }
  void SignalProcessingConnect(SignalProcessingConnectRequestView request,
                               SignalProcessingConnectCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }
  void GetRingBufferFormats(GetRingBufferFormatsRequestView request,
                            GetRingBufferFormatsCompleter::Sync& completer) override {
    // We support 0 ring buffer formats in this testing driver.
    fidl::Arena arena;
    fidl::VectorView<fuchsia_hardware_audio::wire::SupportedFormats> formats(arena, 0);
    fuchsia_hardware_audio::wire::DaiGetRingBufferFormatsResponse response;
    response.ring_buffer_formats = formats;
    auto result =
        fuchsia_hardware_audio::wire::DaiGetRingBufferFormatsResult::WithResponse(arena, response);
    completer.Reply(std::move(result));
  }
  void GetDaiFormats(GetDaiFormatsRequestView request,
                     GetDaiFormatsCompleter::Sync& completer) override {
    // We support 0 DAI formats in this testing driver.
    fidl::Arena arena;
    fidl::VectorView<fuchsia_hardware_audio::wire::DaiSupportedFormats> formats(arena, 0);
    fuchsia_hardware_audio::wire::DaiGetDaiFormatsResponse response;
    response.dai_formats = formats;
    auto result = fuchsia_hardware_audio::wire::DaiGetDaiFormatsResult::WithResponse(
        arena, std::move(response));
    completer.Reply(std::move(result));
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
