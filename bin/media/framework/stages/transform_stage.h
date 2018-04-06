// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/media/framework/models/transform.h"
#include "garnet/bin/media/framework/stages/stage_impl.h"

namespace media {

// A stage that hosts a Transform.
class TransformStageImpl : public TransformStage, public StageImpl {
 public:
  TransformStageImpl(std::shared_ptr<Transform> transform);

  ~TransformStageImpl() override;

  // StageImpl implementation.
  void OnShutDown() override;

  size_t input_count() const override;

  Input& input(size_t index) override;

  size_t output_count() const override;

  Output& output(size_t index) override;

  std::shared_ptr<PayloadAllocator> PrepareInput(size_t index) override;

  void PrepareOutput(size_t index,
                     std::shared_ptr<PayloadAllocator> allocator,
                     UpstreamCallback callback) override;

  void UnprepareOutput(size_t index, UpstreamCallback callback) override;

  void FlushInput(size_t index,
                  bool hold_frame,
                  DownstreamCallback callback) override;

  void FlushOutput(size_t index) override;

 protected:
  // StageImpl implementation.
  GenericNode* GetGenericNode() override;

  void Update() override;

 private:
  // TransformStage implementation.
  void PostTask(const fxl::Closure& task) override;

  Input input_;
  Output output_;
  std::shared_ptr<Transform> transform_;
  std::shared_ptr<PayloadAllocator> allocator_;
  bool input_packet_is_new_;
};

}  // namespace media
