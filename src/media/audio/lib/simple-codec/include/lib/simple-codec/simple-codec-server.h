// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_SERVER_H_
#define SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_SERVER_H_

#include <fidl/fuchsia.hardware.audio/cpp/wire.h>
#include <fuchsia/hardware/audio/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/simple-codec/simple-codec-server-internal.h>
#include <lib/zx/result.h>
#include <lib/zx/time.h>
#include <string.h>

#include <memory>
#include <optional>
#include <vector>

#include <ddktl/device.h>

namespace audio {

class SimpleCodecServer;
using SimpleCodecServerDeviceType =
    ddk::Device<SimpleCodecServer, ddk::Messageable<fuchsia_hardware_audio::CodecConnector>::Mixin>;

// This class provides an implementation of the audio codec protocol to be subclassed by codec
// drivers. The subclass must implement all the virtual methods and use Create() for construction.
class SimpleCodecServer : public SimpleCodecServerDeviceType,
                          public ddk::CodecProtocol<SimpleCodecServer, ddk::base_protocol>,
                          public SimpleCodecServerInternal<SimpleCodecServer> {
 public:
  // Create
  //
  // A general method which handles the construction/initialization of
  // SimpleCodecServer implementation. Given an implementation called
  // 'MyCodec', invocation should look something like..
  //
  // auto codec = SimpleCodecServer::Create<MyCodec>(arg1, arg2, ...);
  //
  template <typename T, typename... ConstructorSignature>
  static zx_status_t CreateAndAddToDdk(ConstructorSignature&&... args) {
    static_assert(std::is_base_of<SimpleCodecServer, T>::value,
                  "Class must derive from SimpleCodecServer!");

    auto dev = std::make_unique<T>(std::forward<ConstructorSignature>(args)...);

    zx_status_t status = dev->CreateAndAddToDdkInternal();
    if (status != ZX_OK) {
      dev->Shutdown();
      return status;
    }
    dev.release();  // Managed by the DDK.
    return ZX_OK;
  }
  virtual ~SimpleCodecServer() = default;
  void DdkRelease() {
    loop_->Shutdown();
    Shutdown();
    state_.Set("released");
    delete this;
  }

  // |fuchsia.hardware.audio.CodecConnector| implementation.
  void Connect(ConnectRequestView request, ConnectCompleter::Sync& completer) override {
    BindClient(request->codec_protocol.TakeChannel(), loop_->dispatcher());
    ZX_DEBUG_ASSERT(!completer.is_reply_needed());
  }

  // Hooks for driver implementation.

  // Called during Create(), it must return DriverIds or an error. Any resources allocated here must
  // be released before destruction, for instance in the Shutdown hook or the class destructor.
  virtual zx::result<DriverIds> Initialize() = 0;
  // Called right before deallocation of the driver in DdkRelease() and also if there is an error
  // during creation in Create().
  virtual zx_status_t Shutdown() = 0;
  // Protocol methods to be implemented by the driver, for descriptions see
  // //docs/concepts/drivers/driver_interfaces/audio_codec.md
  // Methods are simplified to use standard C++ types (see simple-codec-types.h) and also:
  // - Only allow standard frame formats (DaiFrameFormatStandard, see
  //   //sdk/fidl/fuchsia.hardware.audio/dai_format.fidl).
  // - GetDaiFormats returns one DaiSupportedFormats instead of a vector (still allows supported
  //   formats with multiple frame rates, number of channels, etc. just not overly complex ones).
  // - No need to implement WatchGainState, gain must only be changed by SetGainState.
  // - No need to implement WatchPlugState, the library always advertises "hardwired".
  virtual zx_status_t Reset() = 0;
  virtual Info GetInfo() = 0;
  virtual zx_status_t Stop() = 0;
  virtual zx_status_t Start() = 0;
  virtual DaiSupportedFormats GetDaiFormats() = 0;
  virtual zx::result<CodecFormatInfo> SetDaiFormat(const DaiFormat& format) = 0;
  virtual GainFormat GetGainFormat() = 0;
  virtual GainState GetGainState() = 0;
  virtual void SetGainState(GainState state) = 0;

  // Default to not bridgable.
  virtual bool IsBridgeable() { return false; }
  virtual void SetBridgedMode(bool enable_bridged_mode) {
    if (enable_bridged_mode) {
      zxlogf(ERROR, "bridged mode not supported");
    }
  }

  // Default only used for gain, mute and AGC support, override for custom signal processing API
  // usage.
  bool SupportsSignalProcessing() override { return false; }
  void SignalProcessingConnect(
      fidl::InterfaceRequest<fuchsia::hardware::audio::signalprocessing::SignalProcessing>
          signal_processing) override;

  zx_status_t CodecConnect(zx::channel(channel));
  // The dispatcher's loop is guaranteed to be shutdown before "this" is deleted.
  async_dispatcher_t* dispatcher() { return loop_->dispatcher(); }

 protected:
  explicit SimpleCodecServer(zx_device_t* parent) : SimpleCodecServerDeviceType(parent) {
    async_loop_config_t config = kAsyncLoopConfigNoAttachToCurrentThread;
    config.irq_support = true;
    loop_.emplace(&config);
  }
  inspect::Inspector& inspect() { return inspect_; }
  uint64_t GetTopologyId() { return SimpleCodecServerInternal::GetTopologyId(); }
  uint64_t GetGainPeId() { return SimpleCodecServerInternal::GetGainPeId(); }
  uint64_t GetMutePeId() { return SimpleCodecServerInternal::GetMutePeId(); }
  uint64_t GetAgcPeId() { return SimpleCodecServerInternal::GetAgcPeId(); }

 private:
  // Internal implementaions have the same name but different signatures.
  using SimpleCodecServerInternal::GetDaiFormats;
  using SimpleCodecServerInternal::GetInfo;
  using SimpleCodecServerInternal::IsBridgeable;
  using SimpleCodecServerInternal::Reset;
  using SimpleCodecServerInternal::SetDaiFormat;
  using SimpleCodecServerInternal::Start;
  using SimpleCodecServerInternal::Stop;

  friend class std::default_delete<SimpleCodecServer>;
  friend class SimpleCodecServerInternal;

  zx_status_t CreateAndAddToDdkInternal();

  DriverIds driver_ids_;

  inspect::Inspector inspect_;
  inspect::Node simple_codec_;
  inspect::StringProperty state_;
  inspect::IntProperty start_time_;

  inspect::UintProperty number_of_channels_;
  inspect::UintProperty channels_to_use_bitmask_;
  inspect::UintProperty frame_rate_;
  inspect::UintProperty bits_per_slot_;
  inspect::UintProperty bits_per_sample_;
  inspect::StringProperty sample_format_;
  inspect::StringProperty frame_format_;
};

}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_SERVER_H_
