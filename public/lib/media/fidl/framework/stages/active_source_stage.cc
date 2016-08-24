// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/services/framework/stages/active_source_stage.h"

namespace mojo {
namespace media {

ActiveSourceStage::ActiveSourceStage(std::shared_ptr<ActiveSource> source)
    : source_(source), prepared_(false) {
  DCHECK(source_);

  supply_function_ = [this](PacketPtr packet) {
    bool packets_was_empty_ = packets_.empty();
    packets_.push_back(std::move(packet));
    if (packets_was_empty_ && prepared_) {
      RequestUpdate();
    }
  };

  source_->SetSupplyCallback(supply_function_);
}

ActiveSourceStage::~ActiveSourceStage() {}

size_t ActiveSourceStage::input_count() const {
  return 0;
};

Input& ActiveSourceStage::input(size_t index) {
  CHECK(false) << "input requested from source";
  abort();
}

size_t ActiveSourceStage::output_count() const {
  return 1;
}

Output& ActiveSourceStage::output(size_t index) {
  DCHECK_EQ(index, 0u);
  return output_;
}

PayloadAllocator* ActiveSourceStage::PrepareInput(size_t index) {
  CHECK(false) << "PrepareInput called on source";
  return nullptr;
}

void ActiveSourceStage::PrepareOutput(size_t index,
                                      PayloadAllocator* allocator,
                                      const UpstreamCallback& callback) {
  DCHECK_EQ(index, 0u);
  DCHECK(source_);

  if (source_->can_accept_allocator()) {
    // Give the source the provided allocator or the default if non was
    // provided.
    source_->set_allocator(allocator == nullptr ? PayloadAllocator::GetDefault()
                                                : allocator);
  } else if (allocator) {
    // The source can't use the provided allocator, so the output must copy
    // packets.
    output_.SetCopyAllocator(allocator);
  }

  prepared_ = true;
}

void ActiveSourceStage::UnprepareOutput(size_t index,
                                        const UpstreamCallback& callback) {
  DCHECK_EQ(index, 0u);
  DCHECK(source_);

  source_->set_allocator(nullptr);
  output_.SetCopyAllocator(nullptr);
}

void ActiveSourceStage::Update(Engine* engine) {
  DCHECK(engine);

  Demand demand = output_.demand();

  if (demand != Demand::kNegative && !packets_.empty()) {
    output_.SupplyPacket(std::move(packets_.front()), engine);
    packets_.pop_front();
    source_->SetDownstreamDemand(Demand::kNegative);
  } else {
    source_->SetDownstreamDemand(demand);
  }
}

void ActiveSourceStage::FlushInput(size_t index,
                                   const DownstreamCallback& callback) {
  CHECK(false) << "FlushInput called on source";
}

void ActiveSourceStage::FlushOutput(size_t index) {
  DCHECK(source_);
  output_.Flush();
  source_->Flush();
  packets_.clear();
}

}  // namespace media
}  // namespace mojo
