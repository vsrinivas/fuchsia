// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "apps/media/services/logs/media_player_channel.fidl.h"
#include "apps/media/tools/flog_viewer/accumulator.h"
#include "apps/media/tools/flog_viewer/channel_handler.h"

namespace flog {
namespace handlers {

class MediaPlayerAccumulator;

// Handler for MediaPlayerChannel messages, digest format.
class MediaPlayerDigest : public ChannelHandler,
                          public media::logs::MediaPlayerChannel {
 public:
  MediaPlayerDigest(const std::string& format);

  ~MediaPlayerDigest() override;

  std::shared_ptr<Accumulator> GetAccumulator() override;

 protected:
  // ChannelHandler overrides.
  void HandleMessage(fidl::Message* message) override;

 private:
  // MediaPlayerChannel implementation.
  void BoundAs(uint64_t koid) override;

  void CreatedSource(uint64_t related_koid) override;

  void ReceivedSourceDescription(
      fidl::Array<media::MediaTypePtr> stream_types) override;

  void CreatedSink(uint64_t stream_index, uint64_t related_koid) override;

  void StreamsPrepared() override;

  void Flushed() override;

  void Primed() override;

  void Playing() override;

  void EndOfStream() override;

  void PlayRequested() override;

  void PauseRequested() override;

  void SeekRequested(int64_t position) override;

  void Seeking(int64_t position) override;

  void Priming() override;

  void Flushing() override;

  void SettingTimelineTransform(
      media::TimelineTransformPtr timeline_transform) override;

 private:
  media::logs::MediaPlayerChannelStub stub_;
  std::shared_ptr<MediaPlayerAccumulator> accumulator_;
};

// Status of a media player as understood by MediaPlayerDigest.
class MediaPlayerAccumulator : public Accumulator {
 public:
  enum class State {
    kInitial,
    kDescriptionReceived,
    kStreamsPrepared,
    kFlushed,
    kPriming,
    kPrimed,
    kPlaying,
    kEndOfStream,
    kFlushing
  };

  MediaPlayerAccumulator();
  ~MediaPlayerAccumulator() override;

  // Accumulator overrides.
  void Print(std::ostream& os) override;

 private:
  State state_ = State::kInitial;
  State target_state_ = State::kFlushed;
  int64_t target_position_ = media::kUnspecifiedTime;
  ChildBinding source_;
  fidl::Array<media::MediaTypePtr> stream_types_;
  std::vector<ChildBinding> sinks_;
  media::TimelineTransformPtr timeline_transform_;

  friend class MediaPlayerDigest;
};

}  // namespace handlers
}  // namespace flog
