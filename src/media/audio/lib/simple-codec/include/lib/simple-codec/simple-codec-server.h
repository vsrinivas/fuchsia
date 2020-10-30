// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_SERVER_H_
#define SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_SERVER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/simple-codec/simple-codec-types.h>
#include <lib/zx/status.h>
#include <lib/zx/time.h>
#include <string.h>

#include <memory>
#include <optional>
#include <vector>

#include <ddk/debug.h>
#include <ddktl/device.h>
#include <ddktl/protocol/audio.h>
#include <sdk/lib/fidl/cpp/binding.h>

namespace audio {

class SimpleCodecServer;
using SimpleCodecServerDeviceType = ddk::Device<SimpleCodecServer>;

// This class provides an implementation of the audio codec protocol to be subclassed by codec
// drivers. The subclass must implement all the virtual methods and use Create() for construction.
class SimpleCodecServer : public SimpleCodecServerDeviceType,
                          public ddk::CodecProtocol<SimpleCodecServer, ddk::base_protocol>,
                          public ::fuchsia::hardware::audio::codec::Codec {
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
  static std::unique_ptr<T> Create(ConstructorSignature&&... args) {
    static_assert(std::is_base_of<SimpleCodecServer, T>::value,
                  "Class must derive from SimpleCodecServer!");

    auto ret = std::make_unique<T>(std::forward<ConstructorSignature>(args)...);

    if (ret->CreateInternal() != ZX_OK) {
      ret->Shutdown();
      return nullptr;
    }

    return ret;
  }
  virtual ~SimpleCodecServer() = default;
  void DdkRelease() {
    Shutdown();
    loop_.Shutdown();
    delete this;
  }

  // Hooks for driver implementation.

  // Called during Create(), it must return DriverIds or an error. Any resources allocated here must
  // be released before destruction, for instance in the Shutdown hook or the class destructor.
  virtual zx::status<DriverIds> Initialize() = 0;
  // Called right before deallocation of the driver in DdkRelease() and also if there is an error
  // during creation in Create().
  virtual zx_status_t Shutdown() = 0;
  // Protocol methods to be implemented by the driver, for descriptions see
  // //docs/concepts/drivers/driver_interfaces/audio_codec.md
  virtual zx_status_t Reset() = 0;
  virtual Info GetInfo() = 0;
  virtual zx_status_t Stop() = 0;
  virtual zx_status_t Start() = 0;
  virtual bool IsBridgeable() = 0;
  void SetBridgedMode(bool enable_bridged_mode) override = 0;
  virtual std::vector<DaiSupportedFormats> GetDaiFormats() = 0;
  virtual zx_status_t SetDaiFormat(const DaiFormat& format) = 0;
  virtual GainFormat GetGainFormat() = 0;
  virtual GainState GetGainState() = 0;
  void SetGainState(GainState state) override = 0;
  virtual PlugState GetPlugState() = 0;

  zx_status_t CodecConnect(zx::channel(channel));

 protected:
  explicit SimpleCodecServer(zx_device_t* parent)
      : SimpleCodecServerDeviceType(parent), loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  void Reset(ResetCallback callback) override { callback(Reset()); }
  void Stop(StopCallback callback) override { callback(Stop()); }
  void Start(StartCallback callback) override { callback(Start()); }
  void GetInfo(GetInfoCallback callback) override { callback(GetInfo()); }
  void IsBridgeable(IsBridgeableCallback callback) override { callback(IsBridgeable()); }
  void GetDaiFormats(GetDaiFormatsCallback callback) override { callback(ZX_OK, GetDaiFormats()); }
  void SetDaiFormat(::fuchsia::hardware::audio::codec::DaiFormat format,
                    SetDaiFormatCallback callback) override {
    callback(SetDaiFormat(std::move(format)));
  }
  void GetGainFormat(GetGainFormatCallback callback) override { callback(GetGainFormat()); }
  void GetGainState(GetGainStateCallback callback) override { callback(GetGainState()); }
  void GetPlugState(GetPlugStateCallback callback) override { callback(GetPlugState()); }

 private:
  friend class std::default_delete<SimpleCodecServer>;

  zx_status_t CreateInternal();

  DriverIds driver_ids_;
  std::optional<fidl::Binding<::fuchsia::hardware::audio::codec::Codec>> binding_;
  async::Loop loop_;
};

}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_SERVER_H_
