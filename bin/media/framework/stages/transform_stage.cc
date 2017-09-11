// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/framework/stages/transform_stage.h"

namespace media {

TransformStageImpl::TransformStageImpl(std::shared_ptr<Transform> transform)
    : input_(this, 0),
      output_(this, 0),
      transform_(transform),
      allocator_(nullptr),
      input_packet_is_new_(true) {
  FXL_DCHECK(transform_);
}

TransformStageImpl::~TransformStageImpl() {}

void TransformStageImpl::ShutDown() {
  StageImpl::ShutDown();
  allocator_ = nullptr;
}

size_t TransformStageImpl::input_count() const {
  return 1;
};

Input& TransformStageImpl::input(size_t index) {
  FXL_DCHECK(index == 0u);
  return input_;
}

size_t TransformStageImpl::output_count() const {
  return 1;
}

Output& TransformStageImpl::output(size_t index) {
  FXL_DCHECK(index == 0u);
  return output_;
}

std::shared_ptr<PayloadAllocator> TransformStageImpl::PrepareInput(
    size_t index) {
  FXL_DCHECK(index == 0u);
  return nullptr;
}

void TransformStageImpl::PrepareOutput(
    size_t index,
    std::shared_ptr<PayloadAllocator> allocator,
    const UpstreamCallback& callback) {
  FXL_DCHECK(index == 0u);

  allocator_ =
      allocator == nullptr ? PayloadAllocator::GetDefault() : allocator;

  callback(0);
}

void TransformStageImpl::UnprepareOutput(size_t index,
                                         const UpstreamCallback& callback) {
  allocator_ = nullptr;
  callback(0);
}

GenericNode* TransformStageImpl::GetGenericNode() {
  return transform_.get();
}

void TransformStageImpl::ReleaseNode() {
  transform_ = nullptr;
}

void TransformStageImpl::Update() {
  FXL_DCHECK(allocator_);

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
  FXL_DCHECK(index == 0u);
  input_.Flush();
  callback(0);
}

void TransformStageImpl::FlushOutput(size_t index) {
  FXL_DCHECK(index == 0u);
  FXL_DCHECK(transform_);
  PostTask([this]() { transform_->Flush(); });
  input_packet_is_new_ = true;
}

void TransformStageImpl::SetTaskRunner(
    fxl::RefPtr<fxl::TaskRunner> task_runner) {
  StageImpl::SetTaskRunner(task_runner);
}

void TransformStageImpl::PostTask(const fxl::Closure& task) {
  StageImpl::PostTask(task);
}

}  // namespace media
