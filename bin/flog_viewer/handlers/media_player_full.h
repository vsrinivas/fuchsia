// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_TOOLS_FLOG_VIEWER_HANDLERS_MEDIA_PLAYER_FULL_H_
#define APPS_MEDIA_TOOLS_FLOG_VIEWER_HANDLERS_MEDIA_PLAYER_FULL_H_

#include "apps/media/interfaces/logs/media_player_channel.mojom.h"
#include "apps/media/tools/flog_viewer/channel_handler.h"

namespace mojo {
namespace flog {
namespace handlers {

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

  void EndOfStream() override;

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

}  // namespace handlers
}  // namespace flog
}  // namespace mojo

#endif  // APPS_MEDIA_TOOLS_FLOG_VIEWER_HANDLERS_MEDIA_PLAYER_FULL_H_
