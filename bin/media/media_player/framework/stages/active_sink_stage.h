// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_STAGES_ACTIVE_SINK_STAGE_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_STAGES_ACTIVE_SINK_STAGE_H_

#include <deque>
#include <mutex>

#include "garnet/bin/media/media_player/framework/models/active_sink.h"
#include "garnet/bin/media/media_player/framework/stages/stage_impl.h"
#include "lib/fxl/synchronization/thread_annotations.h"

namespace media_player {

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

  std::shared_ptr<PayloadAllocator> PrepareInput(size_t index) override;

  void PrepareOutput(size_t index,
                     std::shared_ptr<PayloadAllocator> allocator,
                     UpstreamCallback callback) override;

  void FlushInput(size_t index,
                  bool hold_frame,
                  DownstreamCallback callback) override;

  void FlushOutput(size_t index) override;

 protected:
  // StageImpl implementation.
  GenericNode* GetGenericNode() override;

  void Update() override;

 private:
  // ActiveSinkStage implementation.
  void PostTask(const fxl::Closure& task) override;

  void SetDemand(Demand demand) override;

  Input input_;
  std::shared_ptr<ActiveSink> sink_;

  mutable std::mutex mutex_;
  Demand sink_demand_ FXL_GUARDED_BY(mutex_) = Demand::kNegative;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_STAGES_ACTIVE_SINK_STAGE_H_
