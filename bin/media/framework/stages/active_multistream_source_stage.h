// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <deque>
#include <vector>

#include "garnet/bin/media/framework/models/active_multistream_source.h"
#include "garnet/bin/media/framework/stages/stage_impl.h"
#include "lib/fxl/synchronization/mutex.h"
#include "lib/fxl/synchronization/thread_annotations.h"

namespace media {

// A stage that hosts an ActiveMultistreamSource.
class ActiveMultistreamSourceStageImpl : public StageImpl,
                                         public ActiveMultistreamSourceStage {
 public:
  ActiveMultistreamSourceStageImpl(
      std::shared_ptr<ActiveMultistreamSource> source);

  ~ActiveMultistreamSourceStageImpl() override;

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
  fxl::RefPtr<fxl::TaskRunner> GetNodeTaskRunner() override;

  void Update() override;

 private:
  // ActiveMultistreamSourceStage implementation.
  void SetTaskRunner(fxl::RefPtr<fxl::TaskRunner> task_runner) override;

  void PostTask(const fxl::Closure& task) override;

  void SupplyPacket(size_t output_index, PacketPtr packet) override;

  std::vector<Output> outputs_;
  std::vector<std::deque<PacketPtr>> packets_per_output_;
  std::shared_ptr<ActiveMultistreamSource> source_;

  mutable fxl::Mutex mutex_;
  size_t ended_streams_ FXL_GUARDED_BY(mutex_) = 0;
  bool packet_request_outstanding_ FXL_GUARDED_BY(mutex_) = false;
};

}  // namespace media
