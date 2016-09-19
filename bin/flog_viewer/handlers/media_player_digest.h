// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_FLOG_VIEWER_HANDLERS_MEDIA_MEDIA_PLAYER_DIGEST_H_
#define EXAMPLES_FLOG_VIEWER_HANDLERS_MEDIA_MEDIA_PLAYER_DIGEST_H_

#include "examples/flog_viewer/accumulator.h"
#include "examples/flog_viewer/channel_handler.h"
#include "mojo/services/media/logs/interfaces/media_player_channel.mojom.h"

namespace mojo {
namespace flog {
namespace examples {
namespace handlers {
namespace media {

class MediaPlayerAccumulator;

// Handler for MediaPlayerChannel messages, digest format.
class MediaPlayerDigest : public ChannelHandler,
                          public mojo::media::logs::MediaPlayerChannel {
 public:
  MediaPlayerDigest(const std::string& format);

  ~MediaPlayerDigest() override;

  std::shared_ptr<Accumulator> GetAccumulator() override;

 protected:
  // ChannelHandler overrides.
  void HandleMessage(Message* message) override;

 private:
  // MediaPlayerChannel implementation.
  void ReceivedDemuxDescription(
      Array<mojo::media::MediaTypePtr> stream_types) override;

  void StreamsPrepared() override;

  void Flushed() override;

  void Primed() override;

  void Playing() override;

  void PlayRequested() override;

  void PauseRequested() override;

  void SeekRequested(int64_t position) override;

  void Seeking(int64_t position) override;

  void Priming() override;

  void Flushing() override;

  void SettingTimelineTransform(
      TimelineTransformPtr timeline_transform) override;

 private:
  mojo::media::logs::MediaPlayerChannelStub stub_;
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
    kFlushing
  };

  MediaPlayerAccumulator();
  ~MediaPlayerAccumulator() override;

  // Accumulator overrides.
  void Print(std::ostream& os) override;

 private:
  State state_ = State::kInitial;
  State target_state_ = State::kFlushed;
  int64_t target_position_ = kUnspecifiedTime;
  Array<mojo::media::MediaTypePtr> stream_types_;
  TimelineTransformPtr timeline_transform_;

  friend class MediaPlayerDigest;
};

}  // namespace media
}  // namespace handlers
}  // namespace examples
}  // namespace flog
}  // namespace mojo

#endif  // EXAMPLES_FLOG_VIEWER_HANDLERS_MEDIA_MEDIA_PLAYER_DIGEST_H_
