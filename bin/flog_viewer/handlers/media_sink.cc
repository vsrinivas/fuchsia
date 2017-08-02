// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/handlers/media_sink.h"

#include <iostream>

#include "apps/media/services/logs/media_sink_channel.fidl.h"
#include "apps/media/tools/flog_viewer/flog_viewer.h"
#include "apps/media/tools/flog_viewer/handlers/media_formatting.h"

namespace flog {
namespace handlers {

MediaSink::MediaSink(const std::string& format)
    : ChannelHandler(format),
      accumulator_(std::make_shared<MediaSinkAccumulator>()) {
  stub_.set_sink(this);
}

MediaSink::~MediaSink() {}

void MediaSink::HandleMessage(fidl::Message* message) {
  stub_.Accept(message);
}

std::shared_ptr<Accumulator> MediaSink::GetAccumulator() {
  return accumulator_;
}

void MediaSink::BoundAs(uint64_t koid) {
  terse_out() << EntryHeader(entry(), entry_index())
              << "MediaSink.BoundAs\n";
  terse_out() << indent;
  terse_out() << begl << "koid: " << AsKoid(koid) << "\n";
  terse_out() << outdent;

  BindAs(koid);
}

void MediaSink::Config(media::MediaTypePtr input_type,
                       media::MediaTypePtr output_type,
                       fidl::Array<uint64_t> converter_koids,
                       uint64_t renderer_koid) {
  FTL_DCHECK(input_type);
  FTL_DCHECK(output_type);

  terse_out() << EntryHeader(entry(), entry_index())
              << "MediaSink.Config\n";
  terse_out() << indent;
  terse_out() << begl << "input_type: " << input_type << "\n";
  terse_out() << begl << "output_type: " << output_type << "\n";
  terse_out() << begl << "converter_koids: " << converter_koids << "\n";
  terse_out() << begl << "renderer_koid: " << renderer_koid << "\n";
  terse_out() << outdent;

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
  os << "MediaSink\n";
  os << indent;
  os << begl << "input_type: " << input_type_ << "\n";
  os << begl << "output_type: " << output_type_ << "\n";
  os << begl << "converters: " << converters_ << "\n";
  os << begl << "renderer: " << renderer_;

  Accumulator::Print(os);
  os << outdent;
}

}  // namespace handlers
}  // namespace flog
