// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/audio_pipe.h"

#include <limits>
#include <vector>

#include "garnet/bin/media/audio_server/audio_renderer1_impl.h"
#include "garnet/bin/media/audio_server/audio_renderer_format_info.h"
#include "garnet/bin/media/audio_server/audio_server_impl.h"
#include "garnet/bin/media/audio_server/constants.h"

namespace media {
namespace audio {

AudioPipe::AudioPacketRefV1::AudioPacketRefV1(
    std::unique_ptr<MediaPacketConsumerBase::SuppliedPacket> supplied_packet,
    AudioServerImpl* server, uint32_t frac_frame_len, int64_t start_pts)
    : AudioPacketRef(server, frac_frame_len, start_pts),
      supplied_packet_(std::move(supplied_packet)) {
  FXL_DCHECK(supplied_packet_);
}

void AudioPipe::AudioPacketRefV1::Cleanup() {
  FXL_DCHECK(supplied_packet_ != nullptr);
  supplied_packet_.reset();
}

void* AudioPipe::AudioPacketRefV1::payload() {
  return supplied_packet_->payload();
}

uint32_t AudioPipe::AudioPacketRefV1::flags() {
  return supplied_packet_->packet().flags;
}

AudioPipe::AudioPipe(AudioRenderer1Impl* owner, AudioServerImpl* server)
    : owner_(owner), server_(server) {
  FXL_DCHECK(owner_);
  FXL_DCHECK(server_);
}

AudioPipe::~AudioPipe() {}

void AudioPipe::ProgramRangeSet(uint64_t program, int64_t min_pts,
                                int64_t max_pts) {
  FXL_DCHECK(program == 0) << "Non-zero program not implemented";
  UpdateMinPts(min_pts);
}

void AudioPipe::UpdateMinPts(int64_t min_pts) {
  if (owner_->format_info_valid()) {
    min_pts_ = min_pts * (owner_->format_info()->frame_to_media_ratio() *
                          owner_->format_info()->frames_per_ns());
    min_pts_dirty_ = false;
  } else {
    min_pts_ = min_pts;
    min_pts_dirty_ = true;
  }
}

void AudioPipe::PrimeRequested(MediaTimelineControlPoint::PrimeCallback cbk) {
  if (prime_callback_) {
    // Prime was already requested. Complete the old one and warn.
    FXL_LOG(WARNING) << "multiple prime requests received";
    prime_callback_();
  }

  if (!is_bound()) {
    // This renderer isn't connected. No need to prime.
    cbk();
    return;
  }

  if (supplied_packets_outstanding() >= kDemandMinPacketsOutstanding) {
    // Demand has already been met.
    SetDemand(kDemandMinPacketsOutstanding);
    cbk();
    return;
  }

  prime_callback_ = cbk;
  SetDemand(kDemandMinPacketsOutstanding);
  // TODO(dalesat): Implement better demand strategy.
}

void AudioPipe::OnPacketSupplied(
    std::unique_ptr<MediaPacketConsumerBase::SuppliedPacket> supplied_packet) {
  FXL_DCHECK(supplied_packet);
  FXL_DCHECK(owner_);

  if (!owner_->format_info_valid()) {
    FXL_LOG(ERROR) << "Packet supplied, but format has not set.";
    Reset();
    return;
  }

  if (min_pts_dirty_) {
    UpdateMinPts(min_pts_);
    FXL_DCHECK(!min_pts_dirty_);
  }

  FXL_DCHECK(supplied_packet->packet().pts_rate_ticks ==
             owner_->format_info()->format().frames_per_second);
  FXL_DCHECK(supplied_packet->packet().pts_rate_seconds == 1);

  // Start by making sure that the region we are receiving is made from an
  // integral number of audio frames.  Count the total number of frames in the
  // process.
  //
  // TODO(johngro): Someday, automatically enforce this using
  // alignment/allocation restrictions at the MediaPipe level of things.
  uint32_t frame_size = owner_->format_info()->bytes_per_frame();

  if ((frame_size > 1) && (supplied_packet->payload_size() % frame_size)) {
    FXL_LOG(ERROR) << "Region length (" << supplied_packet->payload_size()
                   << ") is not divisible by by audio frame size ("
                   << frame_size << ")";
    Reset();
    return;
  }

  static constexpr uint32_t kMaxFrames =
      std::numeric_limits<uint32_t>::max() >> kPtsFractionalBits;
  uint32_t frame_count = (supplied_packet->payload_size() / frame_size);
  if (frame_count > kMaxFrames) {
    FXL_LOG(ERROR) << "Audio frame count (" << frame_count
                   << ") exceeds maximum allowed (" << kMaxFrames << ")";
    Reset();
    return;
  }

  // Figure out the starting PTS.
  int64_t start_pts;
  if (supplied_packet->packet().pts != kNoTimestamp) {
    // The user provided an explicit PTS for this audio.  Transform it into
    // units of fractional frames.
    start_pts = supplied_packet->packet().pts *
                owner_->format_info()->frame_to_media_ratio();
  } else {
    // No PTS was provided.  Use the end time of the last audio packet, if
    // known.  Otherwise, just assume a media time of 0.
    start_pts = next_pts_known_ ? next_pts_ : 0;
  }

  // The end pts is the value we will use for the next packet's start PTS, if
  // the user does not provide an explicit PTS.
  int64_t pts_delta = (static_cast<int64_t>(frame_count) << kPtsFractionalBits);
  next_pts_ = start_pts + pts_delta;
  next_pts_known_ = true;

  bool end_of_stream = supplied_packet->packet().flags & kFlagEos;

  // Send the packet along unless it falls outside the program range.
  if (next_pts_ >= min_pts_) {
    auto packet = fbl::AdoptRef<AudioPacketRef>(
        new AudioPacketRefV1(std::move(supplied_packet), server_,
                             frame_count << kPtsFractionalBits, start_pts));
    owner_->OnPacketReceived(std::move(packet));
  }

  if (prime_callback_ && (end_of_stream || supplied_packets_outstanding() >=
                                               kDemandMinPacketsOutstanding)) {
    // Prime was requested, and we've hit end of stream or demand is met. Call
    // the callback to indicate priming is complete.
    prime_callback_();
    prime_callback_ = nullptr;
  }
}

void AudioPipe::OnFlushRequested(bool hold_frame, FlushCallback cbk) {
  FXL_DCHECK(owner_);
  next_pts_known_ = false;
  owner_->OnFlushRequested(cbk);
}

}  // namespace audio
}  // namespace media
