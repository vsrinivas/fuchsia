// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <deque>

#include "apps/media/src/framework/models/active_sink.h"
#include "apps/media/src/framework/stages/stage_impl.h"
#include "lib/ftl/synchronization/mutex.h"
#include "lib/ftl/synchronization/thread_annotations.h"

namespace media {

// A stage that hosts an ActiveSink.
class ActiveSinkStageImpl : public StageImpl, public ActiveSinkStage {
 public:
  ActiveSinkStageImpl(std::shared_ptr<ActiveSink> sink);

  ~ActiveSinkStageImpl() override;

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
  // ActiveSinkStage implementation.
  void SetTaskRunner(ftl::RefPtr<ftl::TaskRunner> task_runner) override;

  void PostTask(const ftl::Closure& task) override;

  void SetDemand(Demand demand) override;

  Input input_;
  std::shared_ptr<ActiveSink> sink_;

  mutable ftl::Mutex mutex_;
  Demand sink_demand_ FTL_GUARDED_BY(mutex_) = Demand::kNegative;
};

}  // namespace media
