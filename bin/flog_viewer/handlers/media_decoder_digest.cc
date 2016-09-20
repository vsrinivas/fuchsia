// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/handlers/media_decoder_digest.h"

#include <iostream>

#include "apps/media/interfaces/logs/media_decoder_channel.mojom.h"
#include "apps/media/tools/flog_viewer/flog_viewer.h"
#include "apps/media/tools/flog_viewer/handlers/media_formatting.h"

namespace mojo {
namespace flog {
namespace handlers {

MediaDecoderDigest::MediaDecoderDigest(const std::string& format)
    : accumulator_(std::make_shared<MediaDecoderAccumulator>()) {
  MOJO_DCHECK(format == FlogViewer::kFormatDigest);
  stub_.set_sink(this);
}

MediaDecoderDigest::~MediaDecoderDigest() {}

void MediaDecoderDigest::HandleMessage(Message* message) {
  stub_.Accept(message);
}

std::shared_ptr<Accumulator> MediaDecoderDigest::GetAccumulator() {
  return accumulator_;
}

void MediaDecoderDigest::Config(mojo::media::MediaTypePtr input_type,
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

MediaDecoderAccumulator::MediaDecoderAccumulator() {}

MediaDecoderAccumulator::~MediaDecoderAccumulator() {}

void MediaDecoderAccumulator::Print(std::ostream& os) {
  os << "MediaDecoder" << std::endl;
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

} // namespace handlers
} // namespace flog
} // namespace mojo
