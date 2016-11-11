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

void MediaSinkDigest::Config(media::MediaTypePtr input_type,
                             media::MediaTypePtr output_type,
                             uint64_t consumer_address,
                             uint64_t producer_address) {
  FTL_DCHECK(input_type);
  FTL_DCHECK(output_type);

  accumulator_->input_type_ = std::move(input_type);
  accumulator_->output_type_ = std::move(output_type);
  accumulator_->consumer_channel_ = AsChannel(consumer_address);
  accumulator_->consumer_channel_->SetHasParent();
  accumulator_->producer_channel_ = AsChannel(producer_address);
  accumulator_->producer_channel_->SetHasParent();
}

MediaSinkAccumulator::MediaSinkAccumulator() {}

MediaSinkAccumulator::~MediaSinkAccumulator() {}

void MediaSinkAccumulator::Print(std::ostream& os) {
  os << "MediaSink" << std::endl;
  os << indent;
  os << begl << "input_type: " << input_type_;
  os << begl << "output_type: " << output_type_;

  if (consumer_channel_) {
    os << begl << "consumer: " << *consumer_channel_ << " ";
    FTL_DCHECK(consumer_channel_->resolved());
    consumer_channel_->PrintAccumulator(os);
  } else {
    os << begl << "consumer: <none>" << std::endl;
  }

  if (producer_channel_) {
    os << begl << "producer: " << *producer_channel_ << " ";
    FTL_DCHECK(producer_channel_->resolved());
    producer_channel_->PrintAccumulator(os);
  } else {
    os << begl << "producer: <none>" << std::endl;
  }

  Accumulator::Print(os);
  os << outdent;
}

}  // namespace handlers
}  // namespace flog
