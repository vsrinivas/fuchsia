// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_STAGES_SINK_STAGE_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_STAGES_SINK_STAGE_H_

#include <atomic>

#include "garnet/bin/media/media_player/framework/models/sink.h"
#include "garnet/bin/media/media_player/framework/stages/stage_impl.h"
#include "lib/fxl/synchronization/thread_annotations.h"

namespace media_player {

// A stage that hosts an Sink.
class SinkStageImpl : public StageImpl, public SinkStage {
 public:
  SinkStageImpl(std::shared_ptr<Sink> sink);

  ~SinkStageImpl() override;

  // StageImpl implementation.
  size_t input_count() const override;

  Input& input(size_t index) override;

  size_t output_count() const override;

  Output& output(size_t index) override;

  std::shared_ptr<PayloadAllocator> PrepareInput(size_t index) override;

  void PrepareOutput(size_t index, std::shared_ptr<PayloadAllocator> allocator,
                     UpstreamCallback callback) override;

  void FlushInput(size_t index, bool hold_frame,
                  fxl::Closure callback) override;

  void FlushOutput(size_t index, fxl::Closure callback) override;

 protected:
  // StageImpl implementation.
  GenericNode* GetGenericNode() override;

  void Update() override;

 private:
  // SinkStage implementation.
  void PostTask(const fxl::Closure& task) override;

  void SetDemand(Demand demand) override;

  Input input_;
  std::shared_ptr<Sink> sink_;

  // |sink_demand_| reflects the current demand of the sink. It's atomic,
  // because it may be accessed by both the main graph thread and by an
  // arbitrary thread via |SetDemand|. |SetDemand| can only increase demand
  // (from |kNegative| to either |kPositive| or |kNeutral|) and will ensure that
  // |Update| runs after that transition.
  std::atomic<Demand> sink_demand_;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_STAGES_SINK_STAGE_H_
