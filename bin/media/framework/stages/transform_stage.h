// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/src/framework/models/transform.h"
#include "apps/media/src/framework/stages/stage_impl.h"

namespace media {

// A stage that hosts a Transform.
class TransformStageImpl : public TransformStage, public StageImpl {
 public:
  TransformStageImpl(std::shared_ptr<Transform> transform);

  ~TransformStageImpl() override;

  // StageImpl implementation.
  size_t input_count() const override;

  Input& input(size_t index) override;

  size_t output_count() const override;

  Output& output(size_t index) override;

  PayloadAllocator* PrepareInput(size_t index) override;

  void PrepareOutput(size_t index,
                     PayloadAllocator* allocator,
                     const UpstreamCallback& callback) override;

  void UnprepareOutput(size_t index, const UpstreamCallback& callback) override;

  void FlushInput(size_t index,
                  bool hold_frame,
                  const DownstreamCallback& callback) override;

  void FlushOutput(size_t index) override;

 protected:
  // StageImpl implementation.
  ftl::RefPtr<ftl::TaskRunner> GetNodeTaskRunner() override;

  void Update() override;

 private:
  // TransformStage implementation.
  void SetTaskRunner(ftl::RefPtr<ftl::TaskRunner> task_runner) override;

  void PostTask(const ftl::Closure& task) override;

  Input input_;
  Output output_;
  std::shared_ptr<Transform> transform_;
  PayloadAllocator* allocator_;
  bool input_packet_is_new_;
};

}  // namespace media
