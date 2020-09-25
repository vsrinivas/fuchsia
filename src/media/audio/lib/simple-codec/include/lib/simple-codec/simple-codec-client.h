// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_CLIENT_H_
#define SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_CLIENT_H_

#include <lib/simple-codec/simple-codec-types.h>
#include <lib/sync/completion.h>
#include <lib/zx/status.h>
#include <lib/zx/time.h>

#include <string>
#include <vector>

#include <ddktl/protocol/codec.h>

namespace audio {

// This class provides simple audio DAI controller drivers a way to communicate with codecs using
// the audio codec protocol. The methods in the protocol have been converted to always return a
// status in case there is not reply (after kDefaultTimeoutNsecs or the timeout specified via the
// SetTimeout() method).
class SimpleCodecClient {
 public:
  // Convenience methods not part of the audio codec protocol.
  // Initialize the client using the DDK codec protocol object.
  zx_status_t SetProtocol(ddk::CodecProtocolClient proto_client);
  // Configures the timeout the the codec protocol equivalent methods below.
  void SetTimeout(int64_t nsecs);

  // Sync C++ methods to communicate with codecs, for descriptions see
  // //docs/concepts/drivers/driver_interfaces/audio_codec.md and
  // //sdk/banjo/ddk.protocol.codec/codec.banjo.
  zx_status_t Reset();
  zx::status<Info> GetInfo();
  zx_status_t Stop();
  zx_status_t Start();
  zx::status<bool> IsBridgeable();
  zx_status_t SetBridgedMode(bool bridged);
  zx::status<std::vector<DaiSupportedFormats>> GetDaiFormats();
  zx_status_t SetDaiFormat(DaiFormat format);
  zx::status<GainFormat> GetGainFormat();
  zx::status<GainState> GetGainState();
  void SetGainState(GainState state);
  zx::status<PlugState> GetPlugState();

 protected:
  ddk::CodecProtocolClient proto_client_;

 private:
  static constexpr int64_t kDefaultTimeoutNsecs = 1'000'000'000;

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

  int64_t timeout_nsecs_ = kDefaultTimeoutNsecs;
};

}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_CLIENT_H_
