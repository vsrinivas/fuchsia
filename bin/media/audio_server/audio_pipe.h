// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include <media/cpp/fidl.h>
#include "garnet/bin/media/audio_server/audio_packet_ref.h"
#include "garnet/bin/media/audio_server/fwd_decls.h"
#include "lib/media/transport/media_packet_consumer_base.h"

namespace media {
namespace audio {

class AudioRenderer1Impl;

class AudioPipe : public MediaPacketConsumerBase {
 public:
  class AudioPacketRefV1 : public ::media::audio::AudioPacketRef {
   public:
    void Cleanup() final;
    void* payload() final;
    uint32_t flags() final;

   private:
    friend class AudioPipe;
    AudioPacketRefV1(
        std::unique_ptr<MediaPacketConsumerBase::SuppliedPacket> packet,
        AudioServerImpl* server, uint32_t frac_frame_len, int64_t start_pts);

    std::unique_ptr<MediaPacketConsumerBase::SuppliedPacket> supplied_packet_;
  };

  AudioPipe(AudioRenderer1Impl* owner, AudioServerImpl* server);
  ~AudioPipe() override;

  // Indicates a program range was set.
  void ProgramRangeSet(uint64_t program, int64_t min_pts, int64_t max_pts);

  // Indicates the priming was requested. The pipe is responsible for calling
  // the callback when priming is complete.
  void PrimeRequested(MediaTimelineControlPoint::PrimeCallback callback);

 protected:
  void OnPacketSupplied(
      std::unique_ptr<MediaPacketConsumerBase::SuppliedPacket> packet) override;
  void OnFlushRequested(bool hold_frame, FlushCallback cbk) override;

 private:
  static constexpr uint32_t kDemandMinPacketsOutstanding = 4;

  void UpdateMinPts(int64_t min_pts);

  AudioRenderer1Impl* owner_;
  AudioServerImpl* server_;

  MediaTimelineControlPoint::PrimeCallback prime_callback_;
  int64_t min_pts_ = kMinTime;
  bool min_pts_dirty_ = false;

  // State used for timestamp interpolation
  bool next_pts_known_ = 0;
  int64_t next_pts_;
};

}  // namespace audio
}  // namespace media
