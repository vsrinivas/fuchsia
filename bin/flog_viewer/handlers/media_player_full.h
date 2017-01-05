// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/services/logs/media_player_channel.fidl.h"
#include "apps/media/tools/flog_viewer/channel_handler.h"

namespace flog {
namespace handlers {

// Handler for MediaPlayerChannel messages.
class MediaPlayerFull : public ChannelHandler,
                        public media::logs::MediaPlayerChannel {
 public:
  MediaPlayerFull(const std::string& format);

  ~MediaPlayerFull() override;

 protected:
  // ChannelHandler overrides.
  void HandleMessage(fidl::Message* message) override;

 private:
  // MediaPlayerChannel implementation.
  void ReceivedSourceDescription(
      fidl::Array<media::MediaTypePtr> stream_types) override;

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

  media::logs::MediaPlayerChannelStub stub_;
  bool terse_;
};

}  // namespace handlers
}  // namespace flog
