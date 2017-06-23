// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/handlers/media_source.h"

#include <iostream>

#include "apps/media/services/logs/media_source_channel.fidl.h"
#include "apps/media/tools/flog_viewer/flog_viewer.h"
#include "apps/media/tools/flog_viewer/handlers/media_formatting.h"

namespace flog {
namespace handlers {

std::ostream& operator<<(std::ostream& os,
                         const MediaSourceAccumulator::Stream& value) {
  if (!value) {
    return os << begl << "NULL STREAM";
  }

  os << indent << "\n";
  os << begl << "output_type: " << value.output_type_ << "\n";
  os << begl << "converters: " << value.converters_;
  return os << outdent;
}

MediaSource::MediaSource(const std::string& format)
    : ChannelHandler(format),
      accumulator_(std::make_shared<MediaSourceAccumulator>()) {
  stub_.set_sink(this);
}

MediaSource::~MediaSource() {}

void MediaSource::HandleMessage(fidl::Message* message) {
  stub_.Accept(message);
}

std::shared_ptr<Accumulator> MediaSource::GetAccumulator() {
  return accumulator_;
}

void MediaSource::BoundAs(uint64_t koid) {
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaSource.BoundAs\n";
  terse_out() << indent;
  terse_out() << begl << "koid: " << AsKoid(koid) << "\n";
  terse_out() << outdent;

  BindAs(koid);
}

void MediaSource::CreatedDemux(uint64_t related_koid) {
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaSource.CreatedDemux\n";
  terse_out() << indent;
  terse_out() << begl << "related_koid: " << AsKoid(related_koid) << "\n";
  terse_out() << outdent;

  SetBindingKoid(&accumulator_->demux_, related_koid);
}

void MediaSource::NewStream(uint32_t index,
                            media::MediaTypePtr output_type,
                            fidl::Array<uint64_t> converter_koids) {
  FTL_DCHECK(output_type);
  FTL_DCHECK(converter_koids);

  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaSource.NewStream\n";
  terse_out() << indent;
  terse_out() << begl << "index: " << index << "\n";
  terse_out() << begl << "output_type: " << output_type << "\n";
  terse_out() << begl << "converter_koids: " << converter_koids << "\n";
  terse_out() << outdent;

  while (accumulator_->streams_.size() <= index) {
    accumulator_->streams_.emplace_back();
  }

  if (accumulator_->streams_[index]) {
    ReportProblem() << "NewStream index " << index << " already in use";
  }

  accumulator_->streams_[index].output_type_ = std::move(output_type);
  accumulator_->streams_[index].converters_.resize(converter_koids.size());

  for (size_t i = 0; i < converter_koids.size(); i++) {
    SetBindingKoid(&accumulator_->streams_[index].converters_[i],
                   converter_koids[i]);
  }
}

MediaSourceAccumulator::MediaSourceAccumulator() {}

MediaSourceAccumulator::~MediaSourceAccumulator() {}

void MediaSourceAccumulator::Print(std::ostream& os) {
  os << "MediaSource\n";
  os << indent;
  os << begl << "demux: " << demux_ << "\n";
  os << begl << "streams: " << streams_;

  Accumulator::Print(os);
  os << outdent;
}

MediaSourceAccumulator::Stream::Stream() {}

MediaSourceAccumulator::Stream::~Stream() {}

}  // namespace handlers
}  // namespace flog
