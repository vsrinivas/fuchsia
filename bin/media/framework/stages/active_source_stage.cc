// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/framework/stages/active_source_stage.h"

namespace media {

ActiveSourceStage::ActiveSourceStage(Engine* engine,
                                     std::shared_ptr<ActiveSource> source)
    : Stage(engine), output_(this, 0), source_(source), prepared_(false) {
  FTL_DCHECK(source_);

  supply_function_ = [this](PacketPtr packet) {
    bool needs_update = false;

    {
      ftl::MutexLocker locker(&mutex_);
      bool packets_was_empty_ = packets_.empty();
      packets_.push_back(std::move(packet));
      if (packets_was_empty_ && prepared_) {
        needs_update = true;
      }
    }

    if (needs_update) {
      NeedsUpdate();
    }
  };

  source_->SetSupplyCallback(supply_function_);
}

ActiveSourceStage::~ActiveSourceStage() {}

size_t ActiveSourceStage::input_count() const {
  return 0;
};

Input& ActiveSourceStage::input(size_t index) {
  FTL_CHECK(false) << "input requested from source";
  abort();
}

size_t ActiveSourceStage::output_count() const {
  return 1;
}

Output& ActiveSourceStage::output(size_t index) {
  FTL_DCHECK(index == 0u);
  return output_;
}

PayloadAllocator* ActiveSourceStage::PrepareInput(size_t index) {
  FTL_CHECK(false) << "PrepareInput called on source";
  return nullptr;
}

void ActiveSourceStage::PrepareOutput(size_t index,
                                      PayloadAllocator* allocator,
                                      const UpstreamCallback& callback) {
  FTL_DCHECK(index == 0u);
  FTL_DCHECK(source_);

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
  FTL_DCHECK(index == 0u);
  FTL_DCHECK(source_);

  source_->set_allocator(nullptr);
  output_.SetCopyAllocator(nullptr);
}

void ActiveSourceStage::Update() {
  Demand demand = output_.demand();

  {
    ftl::MutexLocker locker(&mutex_);
    if (demand != Demand::kNegative && !packets_.empty()) {
      output_.SupplyPacket(std::move(packets_.front()));
      packets_.pop_front();
      demand = Demand::kNegative;
    }
  }

  source_->SetDownstreamDemand(demand);
}

void ActiveSourceStage::FlushInput(size_t index,
                                   bool hold_frame,
                                   const DownstreamCallback& callback) {
  FTL_CHECK(false) << "FlushInput called on source";
}

void ActiveSourceStage::FlushOutput(size_t index) {
  FTL_DCHECK(source_);
  source_->Flush();
  ftl::MutexLocker locker(&mutex_);
  packets_.clear();
}

}  // namespace media
