// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/handlers/media_renderer_digest.h"

#include <iostream>

#include "apps/media/services/logs/media_renderer_channel.fidl.h"
#include "apps/media/tools/flog_viewer/flog_viewer.h"
#include "apps/media/tools/flog_viewer/handlers/media_formatting.h"

namespace flog {
namespace handlers {

MediaRendererDigest::MediaRendererDigest(const std::string& format)
    : accumulator_(std::make_shared<MediaRendererAccumulator>()) {
  FTL_DCHECK(format == FlogViewer::kFormatDigest);
  stub_.set_sink(this);
}

MediaRendererDigest::~MediaRendererDigest() {}

void MediaRendererDigest::HandleMessage(fidl::Message* message) {
  stub_.Accept(message);
}

std::shared_ptr<Accumulator> MediaRendererDigest::GetAccumulator() {
  return accumulator_;
}

void MediaRendererDigest::BoundAs(uint64_t koid) {
  BindAs(koid);
}

void MediaRendererDigest::Config(
    fidl::Array<media::MediaTypeSetPtr> supported_types,
    uint64_t consumer_address) {
  FTL_DCHECK(supported_types);
  FTL_DCHECK(consumer_address);

  accumulator_->supported_types_ = std::move(supported_types);
  accumulator_->consumer_channel_ = AsChannel(consumer_address);
  accumulator_->consumer_channel_->SetHasParent();
}

void MediaRendererDigest::SetMediaType(media::MediaTypePtr type) {
  FTL_DCHECK(type);
  accumulator_->type_ = std::move(type);
}

MediaRendererAccumulator::MediaRendererAccumulator() {}

MediaRendererAccumulator::~MediaRendererAccumulator() {}

void MediaRendererAccumulator::Print(std::ostream& os) {
  os << "MediaRenderer" << std::endl;
  os << indent;
  os << begl << "supported_types: " << supported_types_;

  if (consumer_channel_) {
    os << begl << "consumer: " << *consumer_channel_ << " ";
    FTL_DCHECK(consumer_channel_->resolved());
    consumer_channel_->PrintAccumulator(os);
  } else {
    os << begl << "consumer: <none>" << std::endl;
  }

  os << begl << "type: " << type_;

  Accumulator::Print(os);
  os << outdent;
}

}  // namespace handlers
}  // namespace flog
