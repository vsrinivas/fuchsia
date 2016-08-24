// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/services/framework/stages/transform_stage.h"

namespace mojo {
namespace media {

TransformStage::TransformStage(std::shared_ptr<Transform> transform)
    : transform_(transform), allocator_(nullptr), input_packet_is_new_(true) {
  DCHECK(transform_);
}

TransformStage::~TransformStage() {}

size_t TransformStage::input_count() const {
  return 1;
};

Input& TransformStage::input(size_t index) {
  DCHECK_EQ(index, 0u);
  return input_;
}

size_t TransformStage::output_count() const {
  return 1;
}

Output& TransformStage::output(size_t index) {
  DCHECK_EQ(index, 0u);
  return output_;
}

PayloadAllocator* TransformStage::PrepareInput(size_t index) {
  DCHECK_EQ(index, 0u);
  return nullptr;
}

void TransformStage::PrepareOutput(size_t index,
                                   PayloadAllocator* allocator,
                                   const UpstreamCallback& callback) {
  DCHECK_EQ(index, 0u);

  allocator_ =
      allocator == nullptr ? PayloadAllocator::GetDefault() : allocator;

  callback(0);
}

void TransformStage::UnprepareOutput(size_t index,
                                     const UpstreamCallback& callback) {
  allocator_ = nullptr;
  callback(0);
}

void TransformStage::Update(Engine* engine) {
  DCHECK(engine);
  DCHECK(allocator_);

  if (input_.packet_from_upstream() && output_.demand() != Demand::kNegative) {
    PacketPtr output_packet;
    if (transform_->TransformPacket(input_.packet_from_upstream(),
                                    input_packet_is_new_, allocator_,
                                    &output_packet)) {
      input_.packet_from_upstream().reset();
      input_packet_is_new_ = true;
    } else {
      input_packet_is_new_ = false;
    }

    if (output_packet) {
      output_.SupplyPacket(std::move(output_packet), engine);
    }
  }

  input_.SetDemand(output_.demand(), engine);
}

void TransformStage::FlushInput(size_t index,
                                const DownstreamCallback& callback) {
  DCHECK_EQ(index, 0u);
  input_.Flush();
  callback(0);
}

void TransformStage::FlushOutput(size_t index) {
  DCHECK_EQ(index, 0u);
  DCHECK(transform_);
  output_.Flush();
  transform_->Flush();
  input_packet_is_new_ = true;
}

}  // namespace media
}  // namespace mojo
