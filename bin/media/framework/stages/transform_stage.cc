// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/framework/stages/transform_stage.h"

namespace media {

TransformStageImpl::TransformStageImpl(std::shared_ptr<Transform> transform)
    : input_(this, 0),
      output_(this, 0),
      transform_(transform),
      allocator_(nullptr),
      input_packet_is_new_(true) {
  FTL_DCHECK(transform_);
}

TransformStageImpl::~TransformStageImpl() {}

size_t TransformStageImpl::input_count() const {
  return 1;
};

Input& TransformStageImpl::input(size_t index) {
  FTL_DCHECK(index == 0u);
  return input_;
}

size_t TransformStageImpl::output_count() const {
  return 1;
}

Output& TransformStageImpl::output(size_t index) {
  FTL_DCHECK(index == 0u);
  return output_;
}

PayloadAllocator* TransformStageImpl::PrepareInput(size_t index) {
  FTL_DCHECK(index == 0u);
  return nullptr;
}

void TransformStageImpl::PrepareOutput(size_t index,
                                       PayloadAllocator* allocator,
                                       const UpstreamCallback& callback) {
  FTL_DCHECK(index == 0u);

  allocator_ =
      allocator == nullptr ? PayloadAllocator::GetDefault() : allocator;

  callback(0);
}

void TransformStageImpl::UnprepareOutput(size_t index,
                                         const UpstreamCallback& callback) {
  allocator_ = nullptr;
  callback(0);
}

ftl::RefPtr<ftl::TaskRunner> TransformStageImpl::GetNodeTaskRunner() {
  return transform_->GetTaskRunner();
}

void TransformStageImpl::Update() {
  FTL_DCHECK(allocator_);

  while (input_.packet() && output_.demand() != Demand::kNegative) {
    PacketPtr output_packet;
    if (transform_->TransformPacket(input_.packet(), input_packet_is_new_,
                                    allocator_, &output_packet)) {
      input_.TakePacket(Demand::kNegative);
      input_packet_is_new_ = true;
    } else {
      input_packet_is_new_ = false;
    }

    if (output_packet) {
      output_.SupplyPacket(std::move(output_packet));
    }
  }

  input_.SetDemand(output_.demand());
}

void TransformStageImpl::FlushInput(size_t index,
                                    bool hold_frame,
                                    const DownstreamCallback& callback) {
  FTL_DCHECK(index == 0u);
  input_.Flush();
  callback(0);
}

void TransformStageImpl::FlushOutput(size_t index) {
  FTL_DCHECK(index == 0u);
  FTL_DCHECK(transform_);
  PostTask([this]() { transform_->Flush(); });
  input_packet_is_new_ = true;
}

void TransformStageImpl::SetTaskRunner(
    ftl::RefPtr<ftl::TaskRunner> task_runner) {
  StageImpl::SetTaskRunner(task_runner);
}

void TransformStageImpl::PostTask(const ftl::Closure& task) {
  StageImpl::PostTask(task);
}

}  // namespace media
