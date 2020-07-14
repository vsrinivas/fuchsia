// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_SERVER_H_
#define SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_SERVER_H_

#include <lib/simple-codec/simple-codec-types.h>
#include <lib/zx/status.h>
#include <lib/zx/time.h>
#include <string.h>

#include <memory>
#include <vector>

#include <ddk/debug.h>
#include <ddktl/device.h>

namespace audio {

class SimpleCodecServer;
using SimpleCodecServerDeviceType = ddk::Device<SimpleCodecServer>;

// This class provides an implementation of the audio codec protocol to be subclassed by codec
// drivers. The subclass must implement all the virtual methods and use Create() for construction.
class SimpleCodecServer : public SimpleCodecServerDeviceType,
                          public ddk::CodecProtocol<SimpleCodecServer, ddk::base_protocol> {
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
  // //docs/concepts/drivers/driver_interfaces/audio_codec.md and
  // //sdk/banjo/ddk.protocol.codec/codec.banjo.
  virtual zx_status_t Reset() = 0;
  virtual Info GetInfo() = 0;
  virtual zx_status_t Stop() = 0;
  virtual zx_status_t Start() = 0;
  virtual bool IsBridgeable() = 0;
  virtual void SetBridgedMode(bool enable_bridged_mode) = 0;
  virtual std::vector<DaiSupportedFormats> GetDaiFormats() = 0;
  virtual zx_status_t SetDaiFormat(const DaiFormat& format) = 0;
  virtual GainFormat GetGainFormat() = 0;
  virtual GainState GetGainState() = 0;
  virtual void SetGainState(GainState state) = 0;
  virtual PlugState GetPlugState() = 0;

  // Banjo codec protocol, do not use directly even though it is public.
  void CodecReset(codec_reset_callback callback, void* cookie);
  void CodecStop(codec_stop_callback callback, void* cookie);
  void CodecStart(codec_start_callback callback, void* cookie);
  void CodecGetInfo(codec_get_info_callback callback, void* cookie);
  void CodecIsBridgeable(codec_is_bridgeable_callback callback, void* cookie);
  void CodecSetBridgedMode(bool enable_bridged_mode, codec_set_bridged_mode_callback callback,
                           void* cookie);
  void CodecGetDaiFormats(codec_get_dai_formats_callback callback, void* cookie);
  void CodecSetDaiFormat(const dai_format_t* format, codec_set_dai_format_callback callback,
                         void* cookie);
  void CodecGetGainFormat(codec_get_gain_format_callback callback, void* cookie);
  void CodecGetGainState(codec_get_gain_state_callback callback, void* cookie);
  void CodecSetGainState(const gain_state_t* gain_state, codec_set_gain_state_callback callback,
                         void* cookie);
  void CodecGetPlugState(codec_get_plug_state_callback callback, void* cookie);

 protected:
  explicit SimpleCodecServer(zx_device_t* parent) : SimpleCodecServerDeviceType(parent) {}

 private:
  friend class std::default_delete<SimpleCodecServer>;

  zx_status_t CreateInternal();

  DriverIds driver_ids_;
};

}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_SERVER_H_
