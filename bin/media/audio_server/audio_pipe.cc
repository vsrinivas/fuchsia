// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/audio_server/audio_pipe.h"

#include <limits>
#include <vector>

#include "apps/media/src/audio_server/audio_renderer_format_info.h"
#include "apps/media/src/audio_server/audio_renderer_impl.h"
#include "apps/media/src/audio_server/audio_server_impl.h"

namespace media {
namespace audio {

AudioPipe::AudioPacketRef::AudioPacketRef(SuppliedPacketPtr supplied_packet,
                                          AudioServerImpl* server,
                                          uint32_t frac_frame_len,
                                          int64_t start_pts,
                                          int64_t end_pts,
                                          uint32_t frame_count)
    : supplied_packet_(std::move(supplied_packet)),
      server_(server),
      frac_frame_len_(frac_frame_len),
      start_pts_(start_pts),
      end_pts_(end_pts),
      frame_count_(frame_count) {
  FTL_DCHECK(supplied_packet_);
  FTL_DCHECK(server_);
}

AudioPipe::AudioPacketRef::~AudioPacketRef() {
  FTL_DCHECK(server_);
  server_->SchedulePacketCleanup(std::move(supplied_packet_));
}

AudioPipe::AudioPipe(AudioRendererImpl* owner, AudioServerImpl* server)
    : owner_(owner), server_(server) {
  FTL_DCHECK(owner_);
  FTL_DCHECK(server_);
}

AudioPipe::~AudioPipe() {}

void AudioPipe::ProgramRangeSet(uint64_t program,
                                int64_t min_pts,
                                int64_t max_pts) {
  FTL_DCHECK(program == 0) << "Non-zero program not implemented";
  min_pts_ = min_pts * (owner_->format_info()->frame_to_media_ratio() *
                        owner_->format_info()->frames_per_ns());
}

void AudioPipe::PrimeRequested(
    const MediaTimelineControlPoint::PrimeCallback& cbk) {
  if (prime_callback_) {
    // Prime was already requested. Complete the old one and warn.
    FTL_LOG(WARNING) << "multiple prime requests received";
    prime_callback_();
  }

  if (!is_bound()) {
    // This renderer isn't connected. No need to prime.
    cbk();
    return;
  }

  prime_callback_ = cbk;
  SetDemand(kDemandMinPacketsOutstanding);
  // TODO(dalesat): Implement better demand strategy.
}

void AudioPipe::OnPacketSupplied(SuppliedPacketPtr supplied_packet) {
  FTL_DCHECK(supplied_packet);
  FTL_DCHECK(owner_);

  if (!owner_->format_info_valid()) {
    FTL_LOG(ERROR) << "Packet supplied, but format has not set.";
    Reset();
    return;
  }

  FTL_DCHECK(supplied_packet->packet()->pts_rate_ticks ==
             owner_->format_info()->format()->frames_per_second);
  FTL_DCHECK(supplied_packet->packet()->pts_rate_seconds == 1);

  // Start by making sure that the region we are receiving is made from an
  // integral number of audio frames.  Count the total number of frames in the
  // process.
  //
  // TODO(johngro): Someday, automatically enforce this using
  // alignment/allocation restrictions at the MediaPipe level of things.
  uint32_t frame_size = owner_->format_info()->bytes_per_frame();

  if ((frame_size > 1) && (supplied_packet->payload_size() % frame_size)) {
    FTL_LOG(ERROR) << "Region length (" << supplied_packet->payload_size()
                   << ") is not divisible by by audio frame size ("
                   << frame_size << ")";
    Reset();
    return;
  }

  static constexpr uint32_t kMaxFrames = std::numeric_limits<uint32_t>::max() >>
                                         AudioRendererImpl::PTS_FRACTIONAL_BITS;
  uint32_t frame_count = (supplied_packet->payload_size() / frame_size);
  if (frame_count > kMaxFrames) {
    FTL_LOG(ERROR) << "Audio frame count (" << frame_count
                   << ") exceeds maximum allowed (" << kMaxFrames << ")";
    Reset();
    return;
  }

  // Figure out the starting PTS.
  int64_t start_pts;
  if (supplied_packet->packet()->pts != MediaPacket::kNoTimestamp) {
    // The user provided an explicit PTS for this audio.  Transform it into
    // units of fractional frames.
    start_pts = supplied_packet->packet()->pts *
                owner_->format_info()->frame_to_media_ratio();
  } else {
    // No PTS was provided.  Use the end time of the last audio packet, if
    // known.  Otherwise, just assume a media time of 0.
    start_pts = next_pts_known_ ? next_pts_ : 0;
  }

  // The end pts is the value we will use for the next packet's start PTS, if
  // the user does not provide an explicit PTS.
  int64_t pts_delta = (static_cast<int64_t>(frame_count)
                       << AudioRendererImpl::PTS_FRACTIONAL_BITS);
  next_pts_ = start_pts + pts_delta;
  next_pts_known_ = true;

  bool end_of_stream = supplied_packet->packet()->end_of_stream;

  // Send the packet along unless it falls outside the program range.
  if (next_pts_ >= min_pts_) {
    owner_->OnPacketReceived(AudioPacketRefPtr(new AudioPacketRef(
        std::move(supplied_packet), server_,
        frame_count << AudioRendererImpl::PTS_FRACTIONAL_BITS, start_pts,
        next_pts_, frame_count)));
  }

  if (prime_callback_ && (end_of_stream || supplied_packets_outstanding() >=
                                               kDemandMinPacketsOutstanding)) {
    // Prime was requested, and we've hit end of stream or demand is met. Call
    // the callback to indicate priming is complete.
    prime_callback_();
    prime_callback_ = nullptr;
  }
}

void AudioPipe::OnFlushRequested(bool hold_frame, const FlushCallback& cbk) {
  FTL_DCHECK(owner_);
  next_pts_known_ = false;
  owner_->OnFlushRequested(cbk);
}

}  // namespace audio
}  // namespace media
