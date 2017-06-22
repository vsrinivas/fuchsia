// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "apps/media/lib/transport/media_packet_consumer_base.h"
#include "apps/media/services/timeline_controller.fidl.h"
#include "apps/media/src/audio_server/fwd_decls.h"

namespace media {
namespace audio {

class AudioPipe : public MediaPacketConsumerBase {
 public:
  class AudioPacketRef;
  // TODO(dalesat): Preferred style eschews these aliases - remove them.
  using AudioPacketRefPtr = std::shared_ptr<AudioPacketRef>;
  using SuppliedPacketPtr =
      std::unique_ptr<MediaPacketConsumerBase::SuppliedPacket>;

  class AudioPacketRef {
   public:
    ~AudioPacketRef();

    // Accessors for starting and ending presentation time stamps expressed in
    // units of audio frames (note, not media time), as signed 51.12 fixed point
    // integers (see AudioRendererImpl:PTS_FRACTIONAL_BITS).  At 192KHz, this
    // allows for ~372.7 years of usable range when starting from a media time
    // of 0.
    //
    // AudioPackets consumed by the AudioServer are all expected to have
    // explicit presentation time stamps.  If packets sent by the user are
    // missing timestamps, appropriate timestamps will be synthesized at this
    // point in the pipeline.
    //
    // Note, the start pts is the time at which the first frame of audio in the
    // packet should be presented.  The end_pts is the time at which the frame
    // after the final frame in the packet would be presented.
    //
    // TODO(johngro): Reconsider this.  It may be best to keep things expressed
    // simply in media time instead of converting to fractional units of
    // renderer frames.  If/when outputs move away from a single fixed step size
    // for output sampling, it will probably be best to just convert this back
    // to media time.
    const int64_t& start_pts() const { return start_pts_; }
    const int64_t& end_pts() const { return end_pts_; }

    uint32_t frac_frame_len() const { return frac_frame_len_; }
    uint32_t frame_count() const { return frame_count_; }
    const SuppliedPacketPtr& supplied_packet() const {
      return supplied_packet_;
    }

   private:
    friend class AudioPipe;
    AudioPacketRef(SuppliedPacketPtr supplied_packet,
                   AudioServerImpl* server,
                   uint32_t frac_frame_len,
                   int64_t start_pts,
                   int64_t end_pts,
                   uint32_t frame_count);

    SuppliedPacketPtr supplied_packet_;
    AudioServerImpl* server_;

    uint32_t frac_frame_len_;
    int64_t start_pts_;
    int64_t end_pts_;
    uint32_t frame_count_;
  };

  AudioPipe(AudioRendererImpl* owner, AudioServerImpl* server);
  ~AudioPipe() override;

  // Indicates the priming was requested. The pipe is responsible for calling
  // the callback when priming is complete.
  void PrimeRequested(int64_t pts,
                      const MediaTimelineControlPoint::PrimeCallback& callback);

 protected:
  void OnPacketSupplied(SuppliedPacketPtr supplied_packet) override;
  void OnFlushRequested(const FlushCallback& cbk) override;

 private:
  static constexpr uint32_t kDemandMinPacketsOutstanding = 4;

  AudioRendererImpl* owner_;
  AudioServerImpl* server_;

  MediaTimelineControlPoint::PrimeCallback prime_callback_;
  int64_t prime_pts_;

  // State used for timestamp interpolation
  bool next_pts_known_ = 0;
  int64_t next_pts_;
};

}  // namespace audio
}  // namespace media
