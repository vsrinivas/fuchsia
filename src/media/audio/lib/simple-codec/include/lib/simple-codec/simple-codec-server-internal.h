// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_SERVER_INTERNAL_H_
#define SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_SERVER_INTERNAL_H_

#include <lib/simple-codec/simple-codec-types.h>

#include <sdk/lib/fidl/cpp/binding.h>

namespace audio {

class SimpleCodecServer;

template <class T>
class SimpleCodecServerInternal : public ::fuchsia::hardware::audio::Codec {
 public:
  explicit SimpleCodecServerInternal();

 private:
  friend class SimpleCodecServer;
  void Reset(ResetCallback callback) override;
  void Stop(StopCallback callback) override;
  void Start(StartCallback callback) override;
  void GetInfo(GetInfoCallback callback) override;
  void IsBridgeable(IsBridgeableCallback callback) override;
  void GetDaiFormats(GetDaiFormatsCallback callback) override;
  void SetDaiFormat(::fuchsia::hardware::audio::DaiFormat format,
                    SetDaiFormatCallback callback) override;
  void GetGainFormat(GetGainFormatCallback callback) override;
  void WatchGainState(WatchGainStateCallback callback) override;
  void SetGainState(::fuchsia::hardware::audio::GainState state) override;
  void GetPlugDetectCapabilities(GetPlugDetectCapabilitiesCallback callback) override;
  void WatchPlugState(WatchPlugStateCallback callback) override;

  zx_time_t plug_time_ = 0;
};

}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_SERVER_INTERNAL_H_
