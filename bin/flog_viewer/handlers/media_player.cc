// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/handlers/media_player.h"

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

MediaPlayer::MediaPlayer(const std::string& format)
    : ChannelHandler(format),
      accumulator_(std::make_shared<MediaPlayerAccumulator>()) {
  stub_.set_sink(this);
}

MediaPlayer::~MediaPlayer() {}

void MediaPlayer::HandleMessage(fidl::Message* message) {
  stub_.Accept(message);
}

std::shared_ptr<Accumulator> MediaPlayer::GetAccumulator() {
  return accumulator_;
}

void MediaPlayer::BoundAs(uint64_t koid) {
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaPlayer.BoundAs\n";
  terse_out() << indent;
  terse_out() << begl << "koid: " << AsKoid(koid) << "\n";
  terse_out() << outdent;

  BindAs(koid);
}

void MediaPlayer::CreatedSource(uint64_t related_koid) {
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaPlayer.CreatedSource\n";
  terse_out() << indent;
  terse_out() << begl << "related_koid: " << AsKoid(related_koid) << "\n";
  terse_out() << outdent;

  SetBindingKoid(&accumulator_->source_, related_koid);
}

void MediaPlayer::ReceivedSourceDescription(
    fidl::Array<media::MediaTypePtr> stream_types) {
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaPlayer.ReceivedSourceDescription"
              << "\n";
  terse_out() << indent;
  terse_out() << begl << "stream_types: " << stream_types << "\n";
  terse_out() << outdent;

  if (accumulator_->state_ != MediaPlayerAccumulator::State::kInitial) {
    ReportProblem() << "ReceivedSourceDescription out of sequence";
  } else {
    accumulator_->state_ = MediaPlayerAccumulator::State::kDescriptionReceived;
  }

  if (accumulator_->stream_types_) {
    ReportProblem() << "Duplicate ReceivedSourceDescription";
  }

  accumulator_->stream_types_ = std::move(stream_types);
  accumulator_->sinks_.resize(accumulator_->stream_types_.size());
}

void MediaPlayer::CreatedSink(uint64_t stream_index, uint64_t related_koid) {
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaPlayer.CreatedSink\n";
  terse_out() << indent;
  terse_out() << begl << "stream_index: " << stream_index << "\n";
  terse_out() << begl << "related_koid: " << AsKoid(related_koid) << "\n";
  terse_out() << outdent;

  if (accumulator_->sinks_.size() <= stream_index) {
    ReportProblem() << "Stream index (" << stream_index
                    << ") out of range, stream count "
                    << accumulator_->sinks_.size();
    return;
  }

  SetBindingKoid(&accumulator_->sinks_[stream_index], related_koid);
}

void MediaPlayer::StreamsPrepared() {
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaPlayer.StreamsPrepared\n";

  if (accumulator_->state_ !=
      MediaPlayerAccumulator::State::kDescriptionReceived) {
    ReportProblem() << "StreamsPrepared out of sequence";
  } else {
    accumulator_->state_ = MediaPlayerAccumulator::State::kStreamsPrepared;
  }
}

void MediaPlayer::Flushed() {
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaPlayer.Flushed\n";

  if (accumulator_->state_ != MediaPlayerAccumulator::State::kFlushed &&
      accumulator_->state_ != MediaPlayerAccumulator::State::kStreamsPrepared &&
      accumulator_->state_ != MediaPlayerAccumulator::State::kFlushing) {
    ReportProblem() << "Flushed out of sequence";
  }

  accumulator_->state_ = MediaPlayerAccumulator::State::kFlushed;
}

void MediaPlayer::Primed() {
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaPlayer.Primed\n";

  if (accumulator_->state_ != MediaPlayerAccumulator::State::kPrimed &&
      accumulator_->state_ != MediaPlayerAccumulator::State::kPriming &&
      accumulator_->state_ != MediaPlayerAccumulator::State::kPlaying) {
    ReportProblem() << "Primed out of sequence";
  }

  accumulator_->state_ = MediaPlayerAccumulator::State::kPrimed;
}

void MediaPlayer::Playing() {
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaPlayer.Playing\n";

  if (accumulator_->state_ != MediaPlayerAccumulator::State::kPlaying &&
      accumulator_->state_ != MediaPlayerAccumulator::State::kPrimed) {
    ReportProblem() << "Playing out of sequence";
  }

  accumulator_->state_ = MediaPlayerAccumulator::State::kPlaying;
}

void MediaPlayer::EndOfStream() {
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaPlayer.EndOfStream\n";

  if (accumulator_->state_ != MediaPlayerAccumulator::State::kPrimed &&
      accumulator_->state_ != MediaPlayerAccumulator::State::kPriming &&
      accumulator_->state_ != MediaPlayerAccumulator::State::kPlaying) {
    ReportProblem() << "EndOfStream out of sequence";
  }

  accumulator_->state_ = MediaPlayerAccumulator::State::kEndOfStream;
  accumulator_->target_state_ = MediaPlayerAccumulator::State::kEndOfStream;
}

void MediaPlayer::PlayRequested() {
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaPlayer.PlayRequested\n";

  accumulator_->target_state_ = MediaPlayerAccumulator::State::kPlaying;
}

void MediaPlayer::PauseRequested() {
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaPlayer.PauseRequested\n";

  accumulator_->target_state_ = MediaPlayerAccumulator::State::kPrimed;
}

void MediaPlayer::SeekRequested(int64_t position) {
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaPlayer.SeekRequested\n";
  terse_out() << indent;
  terse_out() << begl << "position: " << position << "\n";
  terse_out() << outdent;

  accumulator_->target_position_ = position;
}

void MediaPlayer::Seeking(int64_t position) {
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaPlayer.Seeking\n";
  terse_out() << indent;
  terse_out() << begl << "position: " << position << "\n";
  terse_out() << outdent;

  if (accumulator_->state_ != MediaPlayerAccumulator::State::kFlushed) {
    ReportProblem() << "Seeking out of sequence";
  }

  if (accumulator_->target_position_ == media::kUnspecifiedTime) {
    ReportProblem() << "Seeking with no SeekRequested";
  }

  accumulator_->target_position_ = media::kUnspecifiedTime;
}

void MediaPlayer::Priming() {
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaPlayer.Priming\n";

  if (accumulator_->state_ != MediaPlayerAccumulator::State::kFlushed) {
    ReportProblem() << "Priming out of sequence";
  }

  accumulator_->state_ = MediaPlayerAccumulator::State::kPriming;
}

void MediaPlayer::Flushing() {
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaPlayer.Flushing\n";

  if (accumulator_->state_ != MediaPlayerAccumulator::State::kPrimed &&
      accumulator_->state_ != MediaPlayerAccumulator::State::kEndOfStream) {
    ReportProblem() << "Flushing out of sequence";
  }

  accumulator_->state_ = MediaPlayerAccumulator::State::kFlushing;
}

void MediaPlayer::SettingTimelineTransform(
    media::TimelineTransformPtr timeline_transform) {
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaPlayer.SettingTimelineTransform\n";
  terse_out() << indent;
  terse_out() << begl << "timeline_transform: " << timeline_transform << "\n";
  terse_out() << outdent;

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
  os << "MediaPlayer\n";
  os << indent;
  os << begl << "state: " << state_ << "\n";
  os << begl << "target_state: " << target_state_ << "\n";
  os << begl << "target_position: " << AsNsTime(target_position_) << "\n";
  os << begl << "source: " << source_ << "\n";
  os << begl << "stream_types: " << stream_types_ << "\n";
  os << begl << "sinks: " << sinks_ << "\n";
  os << begl << "timeline_transform: " << timeline_transform_;

  if (state_ != target_state_) {
    os << "\n"
       << begl << "SUSPENSE: transitioning to state " << target_state_
       << ", currently in state " << state_;
  }

  if (target_position_ != media::kUnspecifiedTime) {
    os << "\n" << begl << "SUSPENSE: seeking to position " << target_position_;
  }

  Accumulator::Print(os);
  os << outdent;
}

}  // namespace handlers
}  // namespace flog
