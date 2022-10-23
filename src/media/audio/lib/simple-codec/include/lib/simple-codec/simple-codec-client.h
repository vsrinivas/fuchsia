// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_CLIENT_H_
#define SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_CLIENT_H_

#include <fidl/fuchsia.hardware.audio/cpp/wire.h>
#include <fuchsia/hardware/audio/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/wire/client.h>
#include <lib/simple-codec/simple-codec-types.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/result.h>
#include <lib/zx/time.h>
#include <threads.h>

#include <future>
#include <string>
#include <thread>
#include <vector>

#include <fbl/mutex.h>

namespace audio {

// This class provides simple audio DAI controller drivers a way to communicate with codecs using
// the audio codec protocol. The methods in the protocol have been converted to always return a
// status in case there is not reply (after kDefaultTimeoutNsecs or the timeout specified via the
// SetTimeout() method). This class is thread hostile.
class SimpleCodecClient {
 public:
  // If dispatcher is not specified, this object will start its own dispatcher thread for handling
  // async calls. Otherwise, dispatcher must remain valid while this object exists, and will be
  // passed to SimpleCodecClients that are move constructed from this one.
  explicit SimpleCodecClient(async_dispatcher_t* dispatcher = nullptr)
      : loop_(&kAsyncLoopConfigNeverAttachToThread),
        created_with_dispatcher_(dispatcher != nullptr),
        dispatcher_(created_with_dispatcher_ ? dispatcher : loop_.dispatcher()) {}
  ~SimpleCodecClient();

  SimpleCodecClient(SimpleCodecClient&& other) noexcept;

  // Convenience methods not part of the audio codec protocol.
  // Initialize the client using the DDK codec protocol object. Other methods must not be called
  // until after SetProtocol() has been called and returned ZX_OK.
  zx_status_t SetProtocol(ddk::CodecProtocolClient proto_client);

  // Sync C++ methods to communicate with codecs, for descriptions see
  // //docs/concepts/drivers/driver_interfaces/audio_codec.md.
  // Methods are simplified to use standard C++ types (see simple-codec-types.h) and also:
  // - Only allow standard frame formats (DaiFrameFormatStandard, see
  //   //sdk/fidl/fuchsia.hardware.audio/dai_format.fidl).
  // - GetDaiFormats returns one DaiSupportedFormats instead of a vector (still allows supported
  //   formats with multiple frame rates, number of channels, etc. just not overly complex ones).
  // - No direct calls to WatchPlugState, the library only expects "hardwired" codecs.
  zx_status_t Reset();
  zx::result<Info> GetInfo();
  zx_status_t Stop();
  zx_status_t Start();
  zx::result<bool> IsBridgeable();
  zx_status_t SetBridgedMode(bool bridged);
  zx::result<DaiSupportedFormats> GetDaiFormats();
  zx::result<CodecFormatInfo> SetDaiFormat(DaiFormat format);
  zx::result<GainFormat> GetGainFormat();
  zx::result<GainState> GetGainState();
  void SetGainState(GainState state);

 protected:
  ddk::CodecProtocolClient proto_client_;

 private:
  template <class T>
  struct AsyncOutData {
    sync_completion_t completion;
    zx_status_t status;
    T data;
  };

  struct AsyncOut {
    sync_completion_t completion;
    zx_status_t status;
  };

  void UpdateGainAndStartHangingGet(float state);
  void UpdateMuteAndStartHangingGet(bool mute);
  void UpdateAgcAndStartHangingGet(bool agc);

  void Unbind();

  async::Loop loop_;
  const bool created_with_dispatcher_;
  async_dispatcher_t* const dispatcher_;
  bool thread_started_ = false;

  fidl::WireSharedClient<fuchsia_hardware_audio::Codec> codec_;
  fidl::WireSharedClient<fuchsia_hardware_audio_signalprocessing::SignalProcessing>
      signal_processing_;
  std::future<void> codec_torn_down_;

  fbl::Mutex gain_state_lock_;
  zx::result<GainState> gain_state_ TA_GUARDED(gain_state_lock_) = zx::error(ZX_ERR_SHOULD_WAIT);
  zx::result<GainFormat> gain_format_ = zx::error(ZX_ERR_SHOULD_WAIT);
  std::optional<uint64_t> gain_pe_id_;
  std::optional<uint64_t> mute_pe_id_;
  std::optional<uint64_t> agc_pe_id_;
};

}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_CLIENT_H_
