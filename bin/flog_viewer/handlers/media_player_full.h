// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_FLOG_VIEWER_HANDLERS_MEDIA_MEDIA_PLAYER_FULL_H_
#define EXAMPLES_FLOG_VIEWER_HANDLERS_MEDIA_MEDIA_PLAYER_FULL_H_

#include "examples/flog_viewer/channel_handler.h"
#include "mojo/services/media/logs/interfaces/media_player_channel.mojom.h"

namespace mojo {
namespace flog {
namespace examples {
namespace handlers {
namespace media {

// Handler for MediaPlayerChannel messages.
class MediaPlayerFull : public ChannelHandler,
                        public mojo::media::logs::MediaPlayerChannel {
 public:
  MediaPlayerFull(const std::string& format);

  ~MediaPlayerFull() override;

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

  mojo::media::logs::MediaPlayerChannelStub stub_;
  bool terse_;
};

}  // namespace media
}  // namespace handlers
}  // namespace examples
}  // namespace flog
}  // namespace mojo

#endif  // EXAMPLES_FLOG_VIEWER_HANDLERS_MEDIA_MEDIA_PLAYER_FULL_H_
