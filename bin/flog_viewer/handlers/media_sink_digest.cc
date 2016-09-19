// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "examples/flog_viewer/handlers/media/media_sink_digest.h"

#include <iostream>

#include "examples/flog_viewer/flog_viewer.h"
#include "examples/flog_viewer/handlers/media/media_formatting.h"
#include "mojo/services/media/logs/interfaces/media_sink_channel.mojom.h"

namespace mojo {
namespace flog {
namespace examples {
namespace handlers {
namespace media {

MediaSinkDigest::MediaSinkDigest(const std::string& format)
    : accumulator_(std::make_shared<MediaSinkAccumulator>()) {
  MOJO_DCHECK(format == FlogViewer::kFormatDigest);
  stub_.set_sink(this);
}

MediaSinkDigest::~MediaSinkDigest() {}

void MediaSinkDigest::HandleMessage(Message* message) {
  stub_.Accept(message);
}

std::shared_ptr<Accumulator> MediaSinkDigest::GetAccumulator() {
  return accumulator_;
}

void MediaSinkDigest::Config(mojo::media::MediaTypePtr input_type,
                             mojo::media::MediaTypePtr output_type,
                             uint64_t consumer_address,
                             uint64_t producer_address) {
  MOJO_DCHECK(input_type);
  MOJO_DCHECK(output_type);

  accumulator_->input_type_ = input_type.Pass();
  accumulator_->output_type_ = output_type.Pass();
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

  os << begl << "consumer: " << *consumer_channel_ << " ";
  MOJO_DCHECK(consumer_channel_);
  MOJO_DCHECK(consumer_channel_->resolved());
  consumer_channel_->PrintAccumulator(os);

  os << begl << "producer: " << *producer_channel_ << " ";
  MOJO_DCHECK(producer_channel_);
  MOJO_DCHECK(producer_channel_->resolved());
  producer_channel_->PrintAccumulator(os);

  Accumulator::Print(os);
  os << outdent;
}

}  // namespace media
}  // namespace handlers
}  // namespace examples
}  // namespace flog
}  // namespace mojo
