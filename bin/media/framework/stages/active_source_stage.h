// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <deque>

#include "apps/media/src/framework/models/active_source.h"
#include "apps/media/src/framework/stages/stage_impl.h"
#include "lib/ftl/synchronization/mutex.h"
#include "lib/ftl/synchronization/thread_annotations.h"

namespace media {

// A stage that hosts an ActiveSource.
class ActiveSourceStageImpl : public StageImpl, public ActiveSourceStage {
 public:
  ActiveSourceStageImpl(std::shared_ptr<ActiveSource> source);

  ~ActiveSourceStageImpl() override;

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
  // ActiveSourceStage implementation.
  void SetTaskRunner(ftl::RefPtr<ftl::TaskRunner> task_runner) override;

  void PostTask(const ftl::Closure& task) override;

  void SupplyPacket(PacketPtr packet) override;

  Output output_;
  std::shared_ptr<ActiveSource> source_;
  bool prepared_;

  mutable ftl::Mutex mutex_;
  std::deque<PacketPtr> packets_ FTL_GUARDED_BY(mutex_);
};

}  // namespace media
