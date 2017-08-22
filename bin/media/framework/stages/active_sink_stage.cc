// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/framework/stages/active_sink_stage.h"

namespace media {

ActiveSinkStageImpl::ActiveSinkStageImpl(Engine* engine,
                                         std::shared_ptr<ActiveSink> sink)
    : StageImpl(engine), input_(this, 0), sink_(sink) {
  FTL_DCHECK(sink_);
}

ActiveSinkStageImpl::~ActiveSinkStageImpl() {}

size_t ActiveSinkStageImpl::input_count() const {
  return 1;
};

Input& ActiveSinkStageImpl::input(size_t index) {
  FTL_DCHECK(index == 0u);
  return input_;
}

size_t ActiveSinkStageImpl::output_count() const {
  return 0;
}

Output& ActiveSinkStageImpl::output(size_t index) {
  FTL_CHECK(false) << "output requested from sink";
  abort();
}

PayloadAllocator* ActiveSinkStageImpl::PrepareInput(size_t index) {
  FTL_DCHECK(index == 0u);
  return sink_->allocator();
}

void ActiveSinkStageImpl::PrepareOutput(size_t index,
                                        PayloadAllocator* allocator,
                                        const UpstreamCallback& callback) {
  FTL_CHECK(false) << "PrepareOutput called on sink";
}

void ActiveSinkStageImpl::Update() {
  FTL_DCHECK(sink_);

  Demand demand;

  {
    ftl::MutexLocker locker(&mutex_);

    if (input_.packet()) {
      sink_demand_ = sink_->SupplyPacket(input_.TakePacket(Demand::kNegative));
    }

    demand = sink_demand_;
  }

  if (demand != Demand::kNegative) {
    input_.SetDemand(demand);
  }
}

void ActiveSinkStageImpl::FlushInput(size_t index,
                                     bool hold_frame,
                                     const DownstreamCallback& callback) {
  FTL_DCHECK(index == 0u);
  FTL_DCHECK(sink_);
  input_.Flush();
  sink_->Flush(hold_frame);
  ftl::MutexLocker locker(&mutex_);
  sink_demand_ = Demand::kNegative;
}

void ActiveSinkStageImpl::FlushOutput(size_t index) {
  FTL_CHECK(false) << "FlushOutput called on sink";
}

void ActiveSinkStageImpl::SetDemand(Demand demand) {
  bool needs_update = false;

  {
    ftl::MutexLocker locker(&mutex_);
    if (sink_demand_ != demand) {
      sink_demand_ = demand;
      needs_update = true;
    }
  }

  if (needs_update) {
    // This can't be called with the mutex taken, because |Update| can be
    // called from |NeedsUpdate|.
    NeedsUpdate();
  }
}

}  // namespace media
