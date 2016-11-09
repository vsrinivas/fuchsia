// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/handlers/media_player_full.h"

#include <iostream>

#include "apps/media/interfaces/logs/media_player_channel.fidl.h"
#include "apps/media/tools/flog_viewer/flog_viewer.h"
#include "apps/media/tools/flog_viewer/handlers/media_formatting.h"

namespace flog {
namespace handlers {

MediaPlayerFull::MediaPlayerFull(const std::string& format)
    : terse_(format == FlogViewer::kFormatTerse) {
  stub_.set_sink(this);
}

MediaPlayerFull::~MediaPlayerFull() {}

void MediaPlayerFull::HandleMessage(fidl::Message* message) {
  stub_.Accept(message);
}

void MediaPlayerFull::ReceivedDemuxDescription(
    fidl::Array<media::MediaTypePtr> stream_types) {
  std::cout << entry() << "MediaPlayer.ReceivedDemuxDescription" << std::endl;
  std::cout << indent;
  std::cout << begl << "stream_types: " << stream_types;
  std::cout << outdent;
}

void MediaPlayerFull::StreamsPrepared() {
  std::cout << entry() << "MediaPlayer.StreamsPrepared" << std::endl;
}

void MediaPlayerFull::Flushed() {
  std::cout << entry() << "MediaPlayer.Flushed" << std::endl;
}

void MediaPlayerFull::Primed() {
  std::cout << entry() << "MediaPlayer.Primed" << std::endl;
}

void MediaPlayerFull::Playing() {
  std::cout << entry() << "MediaPlayer.Playing" << std::endl;
}

void MediaPlayerFull::EndOfStream() {
  std::cout << entry() << "MediaPlayer.EndOfStream" << std::endl;
}

void MediaPlayerFull::PlayRequested() {
  std::cout << entry() << "MediaPlayer.PlayRequested" << std::endl;
}

void MediaPlayerFull::PauseRequested() {
  std::cout << entry() << "MediaPlayer.PauseRequested" << std::endl;
}

void MediaPlayerFull::SeekRequested(int64_t position) {
  std::cout << entry() << "MediaPlayer.SeekRequested" << std::endl;
  std::cout << indent;
  std::cout << begl << "position: " << position << std::endl;
  std::cout << outdent;
}

void MediaPlayerFull::Seeking(int64_t position) {
  std::cout << entry() << "MediaPlayer.Seeking" << std::endl;
  std::cout << indent;
  std::cout << begl << "position: " << position << std::endl;
  std::cout << outdent;
}

void MediaPlayerFull::Priming() {
  std::cout << entry() << "MediaPlayer.Priming" << std::endl;
}

void MediaPlayerFull::Flushing() {
  std::cout << entry() << "MediaPlayer.Flushing" << std::endl;
}

void MediaPlayerFull::SettingTimelineTransform(
    media::TimelineTransformPtr timeline_transform) {
  std::cout << entry() << "MediaPlayer.SettingTimelineTransform" << std::endl;
  std::cout << indent;
  std::cout << begl << "timeline_transform: " << timeline_transform;
  std::cout << outdent;
}

}  // namespace handlers
}  // namespace flog
