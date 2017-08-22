// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <list>
#include <set>
#include <vector>

#include "apps/media/src/framework/models/active_multistream_sink.h"
#include "apps/media/src/framework/stages/stage_impl.h"
#include "lib/ftl/synchronization/mutex.h"
#include "lib/ftl/synchronization/thread_annotations.h"

namespace media {

// A stage that hosts an ActiveMultistreamSink.
class ActiveMultistreamSinkStageImpl : public StageImpl,
                                       public ActiveMultistreamSinkStage {
 public:
  ActiveMultistreamSinkStageImpl(std::shared_ptr<ActiveMultistreamSink> sink);

  ~ActiveMultistreamSinkStageImpl() override;

  // StageImpl implementation.
  size_t input_count() const override;

  Input& input(size_t index) override;

  size_t output_count() const override;

  Output& output(size_t index) override;

  PayloadAllocator* PrepareInput(size_t index) override;

  void PrepareOutput(size_t index,
                     PayloadAllocator* allocator,
                     const UpstreamCallback& callback) override;

  void FlushInput(size_t index,
                  bool hold_frame,
                  const DownstreamCallback& callback) override;

  void FlushOutput(size_t index) override;

 protected:
  // StageImpl implementation.
  ftl::RefPtr<ftl::TaskRunner> GetNodeTaskRunner() override;

  void Update() override;

 private:
  // ActiveMultistreamSinkStage implementation.
  void SetTaskRunner(ftl::RefPtr<ftl::TaskRunner> task_runner) override;

  void PostTask(const ftl::Closure& task) override;

  size_t AllocateInput() override;

  size_t ReleaseInput(size_t index) override;

  void UpdateDemand(size_t input_index, Demand demand) override;

  struct StageInput {
    StageInput(StageImpl* stage, size_t index)
        : input_(stage, index), allocated_(false), demand_(Demand::kNegative) {}
    Input input_;
    bool allocated_;
    Demand demand_;
  };

  std::shared_ptr<ActiveMultistreamSink> sink_;

  mutable ftl::Mutex mutex_;
  std::vector<std::unique_ptr<StageInput>> inputs_ FTL_GUARDED_BY(mutex_);
  std::set<size_t> unallocated_inputs_ FTL_GUARDED_BY(mutex_);
  std::list<size_t> pending_inputs_ FTL_GUARDED_BY(mutex_);
};

}  // namespace media
