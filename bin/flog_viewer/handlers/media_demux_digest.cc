// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/handlers/media_demux_digest.h"

#include <iostream>

#include "apps/media/interfaces/logs/media_demux_channel.mojom.h"
#include "apps/media/tools/flog_viewer/flog_viewer.h"
#include "apps/media/tools/flog_viewer/handlers/media_formatting.h"

namespace mojo {
namespace flog {
namespace handlers {

std::ostream& operator<<(std::ostream& os,
                         const MediaDemuxAccumulator::Stream& value) {
  if (!value) {
    return os << begl << "NULL STREAM";
  } else {
    os << std::endl;
  }

  os << indent;
  os << begl << "type: " << value.type_;
  if (value.producer_channel_) {
    os << begl << "producer: " << *value.producer_channel_ << " ";
    value.producer_channel_->PrintAccumulator(os);
  } else {
    os << begl << "producer: <none>" << std::endl;
  }

  return os << outdent;
}

MediaDemuxDigest::MediaDemuxDigest(const std::string& format)
    : accumulator_(std::make_shared<MediaDemuxAccumulator>()) {
  FTL_DCHECK(format == FlogViewer::kFormatDigest);
  stub_.set_sink(this);
}

MediaDemuxDigest::~MediaDemuxDigest() {}

void MediaDemuxDigest::HandleMessage(Message* message) {
  stub_.Accept(message);
}

std::shared_ptr<Accumulator> MediaDemuxDigest::GetAccumulator() {
  return accumulator_;
}

void MediaDemuxDigest::NewStream(uint32_t index,
                                 mojo::media::MediaTypePtr type,
                                 uint64_t producer_address) {
  FTL_DCHECK(type);

  while (accumulator_->streams_.size() <= index) {
    accumulator_->streams_.emplace_back();
  }

  if (accumulator_->streams_[index]) {
    ReportProblem() << "NewStream index " << index << " already in use";
  }

  accumulator_->streams_[index].type_ = type.Pass();
  accumulator_->streams_[index].producer_channel_ = AsChannel(producer_address);
  accumulator_->streams_[index].producer_channel_->SetHasParent();
}

MediaDemuxAccumulator::MediaDemuxAccumulator() {}

MediaDemuxAccumulator::~MediaDemuxAccumulator() {}

void MediaDemuxAccumulator::Print(std::ostream& os) {
  os << "MediaDemux" << std::endl;
  os << indent;
  os << begl << "streams: " << streams_;
  Accumulator::Print(os);
  os << outdent;
}

MediaDemuxAccumulator::Stream::Stream() {}

MediaDemuxAccumulator::Stream::~Stream() {}

}  // namespace handlers
}  // namespace flog
}  // namespace mojo
