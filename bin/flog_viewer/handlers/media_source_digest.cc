// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/handlers/media_source_digest.h"

#include <iostream>

#include "apps/media/services/logs/media_source_channel.fidl.h"
#include "apps/media/tools/flog_viewer/flog_viewer.h"
#include "apps/media/tools/flog_viewer/handlers/media_formatting.h"

namespace flog {
namespace handlers {

std::ostream& operator<<(std::ostream& os,
                         const MediaSourceAccumulator::Stream& value) {
  if (!value) {
    return os << begl << "NULL STREAM" << std::endl;
  } else {
    os << std::endl;
  }

  os << indent;
  os << begl << "output_type: " << value.output_type_;
  os << begl << "converters: " << value.converters_;
  return os << outdent;
}

MediaSourceDigest::MediaSourceDigest(const std::string& format)
    : accumulator_(std::make_shared<MediaSourceAccumulator>()) {
  FTL_DCHECK(format == FlogViewer::kFormatDigest);
  stub_.set_sink(this);
}

MediaSourceDigest::~MediaSourceDigest() {}

void MediaSourceDigest::HandleMessage(fidl::Message* message) {
  stub_.Accept(message);
}

std::shared_ptr<Accumulator> MediaSourceDigest::GetAccumulator() {
  return accumulator_;
}

void MediaSourceDigest::BoundAs(uint64_t koid) {
  BindAs(koid);
}

void MediaSourceDigest::CreatedDemux(uint64_t related_koid) {
  SetBindingKoid(&accumulator_->demux_, related_koid);
}

void MediaSourceDigest::NewStream(uint32_t index,
                                  media::MediaTypePtr output_type,
                                  fidl::Array<uint64_t> converter_koids) {
  FTL_DCHECK(output_type);
  FTL_DCHECK(converter_koids);

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
  os << "MediaSource" << std::endl;
  os << indent;
  os << begl << "demux: " << demux_;
  os << begl << "streams: " << streams_;

  Accumulator::Print(os);
  os << outdent;
}

MediaSourceAccumulator::Stream::Stream() {}

MediaSourceAccumulator::Stream::~Stream() {}

}  // namespace handlers
}  // namespace flog
