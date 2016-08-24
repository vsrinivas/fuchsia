// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/services/framework/stages/active_multistream_sink_stage.h"

namespace mojo {
namespace media {

ActiveMultistreamSinkStage::ActiveMultistreamSinkStage(
    std::shared_ptr<ActiveMultistreamSink> sink)
    : sink_(sink) {
  DCHECK(sink_);
  sink_->SetHost(this);
  // Add one unallocated input so this stage isn't misidentified as a source.
  ReleaseInput(AllocateInput());
}

ActiveMultistreamSinkStage::~ActiveMultistreamSinkStage() {
  base::AutoLock lock(lock_);
}

size_t ActiveMultistreamSinkStage::input_count() const {
  base::AutoLock lock(lock_);
  return inputs_.size();
};

Input& ActiveMultistreamSinkStage::input(size_t index) {
  base::AutoLock lock(lock_);
  DCHECK_LT(index, inputs_.size());
  return inputs_[index]->input_;
}

size_t ActiveMultistreamSinkStage::output_count() const {
  return 0;
}

Output& ActiveMultistreamSinkStage::output(size_t index) {
  CHECK(false) << "output requested from sink";
  return *(static_cast<Output*>(nullptr));
}

PayloadAllocator* ActiveMultistreamSinkStage::PrepareInput(size_t index) {
  return nullptr;
}

void ActiveMultistreamSinkStage::PrepareOutput(
    size_t index,
    PayloadAllocator* allocator,
    const UpstreamCallback& callback) {
  CHECK(false) << "PrepareOutput called on sink";
}

void ActiveMultistreamSinkStage::Update(Engine* engine) {
  DCHECK(engine);
  DCHECK(sink_);

  base::AutoLock lock(lock_);

  for (auto iter = pending_inputs_.begin(); iter != pending_inputs_.end();) {
    DCHECK(*iter < inputs_.size());
    StageInput* input = inputs_[*iter].get();
    if (input->input_.packet_from_upstream()) {
      input->demand_ = sink_->SupplyPacket(
          input->index_, std::move(input->input_.packet_from_upstream()));

      if (input->demand_ == Demand::kNegative) {
        auto remove_iter = iter;
        ++iter;
        pending_inputs_.erase(remove_iter);
      }
    } else {
      ++iter;
    }

    input->input_.SetDemand(input->demand_, engine);
  }
}

void ActiveMultistreamSinkStage::FlushInput(
    size_t index,
    const DownstreamCallback& callback) {
  DCHECK(sink_);

  sink_->Flush();

  base::AutoLock lock(lock_);
  inputs_[index]->demand_ = Demand::kNegative;
  inputs_[index]->input_.Flush();

  pending_inputs_.remove(index);
}

void ActiveMultistreamSinkStage::FlushOutput(size_t index) {
  CHECK(false) << "FlushOutput called on sink";
}

size_t ActiveMultistreamSinkStage::AllocateInput() {
  base::AutoLock lock(lock_);

  StageInput* input;
  if (unallocated_inputs_.empty()) {
    input = new StageInput(inputs_.size());
    inputs_.emplace_back(std::unique_ptr<StageInput>(input));
  } else {
    // Allocate lowest indices first.
    auto iter = unallocated_inputs_.lower_bound(0);
    input = inputs_[*iter].get();
    DCHECK(!input->allocated_);
    unallocated_inputs_.erase(iter);
  }

  input->allocated_ = true;

  return input->index_;
}

size_t ActiveMultistreamSinkStage::ReleaseInput(size_t index) {
  base::AutoLock lock(lock_);
  DCHECK(index < inputs_.size());

  StageInput* input = inputs_[index].get();
  DCHECK(input);
  DCHECK(input->allocated_);
  DCHECK(!input->input_.connected());

  input->allocated_ = false;

  // Pop input if it's at the end of inputs_. Otherwise, add it to
  // unallocated_inputs_. We never pop the last input so the stage can't be
  // misidentified as a source.
  if (index != 0 && index == inputs_.size() - 1) {
    while (inputs_.size() > 1 && !inputs_.back()->allocated_) {
      unallocated_inputs_.erase(inputs_.size() - 1);
      inputs_.pop_back();
    }
  } else {
    unallocated_inputs_.insert(input->index_);
  }

  return inputs_.size();
}

void ActiveMultistreamSinkStage::UpdateDemand(size_t input_index,
                                              Demand demand) {
  lock_.Acquire();
  DCHECK(input_index < inputs_.size());
  DCHECK(demand != Demand::kNegative);

  StageInput* input = inputs_[input_index].get();
  DCHECK(input);
  input->demand_ = demand;
  pending_inputs_.push_back(input_index);
  lock_.Release();
  RequestUpdate();
}

}  // namespace media
}  // namespace mojo
