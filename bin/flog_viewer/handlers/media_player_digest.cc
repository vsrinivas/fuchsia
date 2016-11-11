// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/handlers/media_player_digest.h"

#include <iostream>

#include "apps/media/services/logs/media_player_channel.fidl.h"
#include "apps/media/tools/flog_viewer/flog_viewer.h"
#include "apps/media/tools/flog_viewer/handlers/media_formatting.h"

namespace flog {
namespace handlers {

std::ostream& operator<<(std::ostream& os,
                         MediaPlayerAccumulator::State value) {
  switch (value) {
    case MediaPlayerAccumulator::State::kInitial:
      return os << "initial";
    case MediaPlayerAccumulator::State::kDescriptionReceived:
      return os << "descriptionReceived";
    case MediaPlayerAccumulator::State::kStreamsPrepared:
      return os << "streamsPrepared";
    case MediaPlayerAccumulator::State::kFlushed:
      return os << "flushed";
    case MediaPlayerAccumulator::State::kPriming:
      return os << "priming";
    case MediaPlayerAccumulator::State::kPrimed:
      return os << "primed";
    case MediaPlayerAccumulator::State::kPlaying:
      return os << "playing";
    case MediaPlayerAccumulator::State::kEndOfStream:
      return os << "endOfStream";
    case MediaPlayerAccumulator::State::kFlushing:
      return os << "flushing";
  }

  return os;
}

MediaPlayerDigest::MediaPlayerDigest(const std::string& format)
    : accumulator_(std::make_shared<MediaPlayerAccumulator>()) {
  FTL_DCHECK(format == FlogViewer::kFormatDigest);
  stub_.set_sink(this);
}

MediaPlayerDigest::~MediaPlayerDigest() {}

void MediaPlayerDigest::HandleMessage(fidl::Message* message) {
  stub_.Accept(message);
}

std::shared_ptr<Accumulator> MediaPlayerDigest::GetAccumulator() {
  return accumulator_;
}

void MediaPlayerDigest::ReceivedDemuxDescription(
    fidl::Array<media::MediaTypePtr> stream_types) {
  if (accumulator_->state_ != MediaPlayerAccumulator::State::kInitial) {
    ReportProblem() << "ReceivedDemuxDescription out of sequence";
  } else {
    accumulator_->state_ = MediaPlayerAccumulator::State::kDescriptionReceived;
  }

  if (accumulator_->stream_types_) {
    ReportProblem() << "Duplicate ReceivedDemuxDescription";
  }

  accumulator_->stream_types_ = std::move(stream_types);
}

void MediaPlayerDigest::StreamsPrepared() {
  if (accumulator_->state_ !=
      MediaPlayerAccumulator::State::kDescriptionReceived) {
    ReportProblem() << "StreamsPrepared out of sequence";
  } else {
    accumulator_->state_ = MediaPlayerAccumulator::State::kStreamsPrepared;
  }
}

void MediaPlayerDigest::Flushed() {
  if (accumulator_->state_ != MediaPlayerAccumulator::State::kFlushed &&
      accumulator_->state_ != MediaPlayerAccumulator::State::kStreamsPrepared &&
      accumulator_->state_ != MediaPlayerAccumulator::State::kFlushing) {
    ReportProblem() << "Flushed out of sequence";
  }

  accumulator_->state_ = MediaPlayerAccumulator::State::kFlushed;
}

void MediaPlayerDigest::Primed() {
  if (accumulator_->state_ != MediaPlayerAccumulator::State::kPrimed &&
      accumulator_->state_ != MediaPlayerAccumulator::State::kPriming &&
      accumulator_->state_ != MediaPlayerAccumulator::State::kPlaying) {
    ReportProblem() << "Primed out of sequence";
  }

  accumulator_->state_ = MediaPlayerAccumulator::State::kPrimed;
}

void MediaPlayerDigest::Playing() {
  if (accumulator_->state_ != MediaPlayerAccumulator::State::kPlaying &&
      accumulator_->state_ != MediaPlayerAccumulator::State::kPrimed) {
    ReportProblem() << "Playing out of sequence";
  }

  accumulator_->state_ = MediaPlayerAccumulator::State::kPlaying;
}

void MediaPlayerDigest::EndOfStream() {
  if (accumulator_->state_ != MediaPlayerAccumulator::State::kPrimed &&
      accumulator_->state_ != MediaPlayerAccumulator::State::kPriming &&
      accumulator_->state_ != MediaPlayerAccumulator::State::kPlaying) {
    ReportProblem() << "EndOfStream out of sequence";
  }

  accumulator_->state_ = MediaPlayerAccumulator::State::kEndOfStream;
  accumulator_->target_state_ = MediaPlayerAccumulator::State::kEndOfStream;
}

void MediaPlayerDigest::PlayRequested() {
  accumulator_->target_state_ = MediaPlayerAccumulator::State::kPlaying;
}

void MediaPlayerDigest::PauseRequested() {
  accumulator_->target_state_ = MediaPlayerAccumulator::State::kPrimed;
}

void MediaPlayerDigest::SeekRequested(int64_t position) {
  accumulator_->target_position_ = position;
}

void MediaPlayerDigest::Seeking(int64_t position) {
  if (accumulator_->state_ != MediaPlayerAccumulator::State::kFlushed) {
    ReportProblem() << "Seeking out of sequence";
  }

  if (accumulator_->target_position_ == media::kUnspecifiedTime) {
    ReportProblem() << "Seeking with no SeekRequested";
  }

  accumulator_->target_position_ = media::kUnspecifiedTime;
}

void MediaPlayerDigest::Priming() {
  if (accumulator_->state_ != MediaPlayerAccumulator::State::kFlushed) {
    ReportProblem() << "Priming out of sequence";
  }

  accumulator_->state_ = MediaPlayerAccumulator::State::kPriming;
}

void MediaPlayerDigest::Flushing() {
  if (accumulator_->state_ != MediaPlayerAccumulator::State::kPrimed &&
      accumulator_->state_ != MediaPlayerAccumulator::State::kEndOfStream) {
    ReportProblem() << "Flushing out of sequence";
  }

  accumulator_->state_ = MediaPlayerAccumulator::State::kFlushing;
}

void MediaPlayerDigest::SettingTimelineTransform(
    media::TimelineTransformPtr timeline_transform) {
  if (accumulator_->state_ != MediaPlayerAccumulator::State::kPrimed &&
      accumulator_->state_ != MediaPlayerAccumulator::State::kPlaying &&
      accumulator_->state_ != MediaPlayerAccumulator::State::kEndOfStream) {
    ReportProblem() << "SettingTimelineTransform out of sequence";
  }

  accumulator_->timeline_transform_ = std::move(timeline_transform);
}

MediaPlayerAccumulator::MediaPlayerAccumulator() {}

MediaPlayerAccumulator::~MediaPlayerAccumulator() {}

void MediaPlayerAccumulator::Print(std::ostream& os) {
  os << "MediaPlayer" << std::endl;
  os << indent;
  os << begl << "state: " << state_ << std::endl;
  os << begl << "target_state: " << target_state_ << std::endl;
  os << begl << "target_position: " << AsTime(target_position_) << std::endl;
  os << begl << "stream_types: " << stream_types_;
  os << begl << "timeline_transform: " << timeline_transform_;

  if (state_ != target_state_) {
    os << begl << "SUSPENSE: transitioning to state " << target_state_
       << ", currently in state " << state_ << std::endl;
  }

  if (target_position_ != media::kUnspecifiedTime) {
    os << begl << "SUSPENSE: seeking to position " << target_position_;
  }

  Accumulator::Print(os);
  os << outdent;
}

}  // namespace handlers
}  // namespace flog
