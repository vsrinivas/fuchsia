// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/handlers/media_sink_digest.h"

#include <iostream>

#include "apps/media/services/logs/media_sink_channel.fidl.h"
#include "apps/media/tools/flog_viewer/flog_viewer.h"
#include "apps/media/tools/flog_viewer/handlers/media_formatting.h"

namespace flog {
namespace handlers {

MediaSinkDigest::MediaSinkDigest(const std::string& format)
    : accumulator_(std::make_shared<MediaSinkAccumulator>()) {
  FTL_DCHECK(format == FlogViewer::kFormatDigest);
  stub_.set_sink(this);
}

MediaSinkDigest::~MediaSinkDigest() {}

void MediaSinkDigest::HandleMessage(fidl::Message* message) {
  stub_.Accept(message);
}

std::shared_ptr<Accumulator> MediaSinkDigest::GetAccumulator() {
  return accumulator_;
}

void MediaSinkDigest::BoundAs(uint64_t koid) {
  BindAs(koid);
}

void MediaSinkDigest::Config(media::MediaTypePtr input_type,
                             media::MediaTypePtr output_type,
                             fidl::Array<uint64_t> converter_koids,
                             uint64_t renderer_koid) {
  FTL_DCHECK(input_type);
  FTL_DCHECK(output_type);

  accumulator_->input_type_ = std::move(input_type);
  accumulator_->output_type_ = std::move(output_type);
  accumulator_->converters_.resize(converter_koids.size());

  for (size_t i = 0; i < converter_koids.size(); i++) {
    SetBindingKoid(&accumulator_->converters_[i], converter_koids[i]);
  }

  SetBindingKoid(&accumulator_->renderer_, renderer_koid);
}

MediaSinkAccumulator::MediaSinkAccumulator() {}

MediaSinkAccumulator::~MediaSinkAccumulator() {}

void MediaSinkAccumulator::Print(std::ostream& os) {
  os << "MediaSink" << std::endl;
  os << indent;
  os << begl << "input_type: " << input_type_;
  os << begl << "output_type: " << output_type_;
  os << begl << "converters: " << converters_;
  os << begl << "renderer: " << renderer_;

  Accumulator::Print(os);
  os << outdent;
}

}  // namespace handlers
}  // namespace flog
