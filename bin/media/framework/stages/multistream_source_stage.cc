// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/framework/stages/multistream_source_stage.h"

#include "apps/media/src/framework/stages/util.h"

namespace media {

MultistreamSourceStageImpl::MultistreamSourceStageImpl(
    Engine* engine,
    std::shared_ptr<MultistreamSource> source)
    : StageImpl(engine), source_(source), ended_streams_(0) {
  FTL_DCHECK(source);

  for (size_t index = 0; index < source->stream_count(); ++index) {
    outputs_.emplace_back(this, index);
  }
}

MultistreamSourceStageImpl::~MultistreamSourceStageImpl() {}

size_t MultistreamSourceStageImpl::input_count() const {
  return 0;
};

Input& MultistreamSourceStageImpl::input(size_t index) {
  FTL_CHECK(false) << "input requested from source";
  abort();
}

size_t MultistreamSourceStageImpl::output_count() const {
  return outputs_.size();
}

Output& MultistreamSourceStageImpl::output(size_t index) {
  FTL_DCHECK(index < outputs_.size());
  return outputs_[index];
}

PayloadAllocator* MultistreamSourceStageImpl::PrepareInput(size_t index) {
  FTL_CHECK(false) << "PrepareInput called on source";
  return nullptr;
}

void MultistreamSourceStageImpl::PrepareOutput(
    size_t index,
    PayloadAllocator* allocator,
    const UpstreamCallback& callback) {
  FTL_DCHECK(index < outputs_.size());

  if (allocator != nullptr) {
    // Currently, we don't support a source that uses provided allocators. If
    // we're provided an allocator, the output must have it so supplied packets
    // can be copied.
    outputs_[index].SetCopyAllocator(allocator);
  }
}

void MultistreamSourceStageImpl::UnprepareOutput(
    size_t index,
    const UpstreamCallback& callback) {
  FTL_DCHECK(index < outputs_.size());
  outputs_[index].SetCopyAllocator(nullptr);
}

void MultistreamSourceStageImpl::Update() {
  while (true) {
    if (cached_packet_ && HasPositiveDemand(outputs_)) {
      FTL_DCHECK(cached_packet_output_index_ < outputs_.size());
      Output& output = outputs_[cached_packet_output_index_];

      if (output.demand() != Demand::kNegative) {
        // cached_packet_ is intended for an output which will accept packets.
        output.SupplyPacket(std::move(cached_packet_));
      }
    }

    if (cached_packet_) {
      // There's still a cached packet. We're done for now.
      return;
    }

    if (ended_streams_ == outputs_.size()) {
      // We've seen end-of-stream for all streams. All done.
      return;
    }

    // Pull a packet from the source.
    cached_packet_ = source_->PullPacket(&cached_packet_output_index_);
    FTL_DCHECK(cached_packet_);
    FTL_DCHECK(cached_packet_output_index_ < outputs_.size());

    if (cached_packet_->end_of_stream()) {
      ended_streams_++;
    }
  }
}

void MultistreamSourceStageImpl::FlushInput(
    size_t index,
    bool hold_frame,
    const DownstreamCallback& callback) {
  FTL_CHECK(false) << "FlushInput called on source";
}

void MultistreamSourceStageImpl::FlushOutput(size_t index) {
  FTL_DCHECK(index < outputs_.size());
  FTL_DCHECK(source_);
  source_->Flush();
  cached_packet_.reset();
  cached_packet_output_index_ = 0;
  ended_streams_ = 0;
}

}  // namespace media
