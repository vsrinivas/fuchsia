// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_FRAMEWORK_STAGES_MULTISTREAM_SOURCE_STAGE_H_
#define GARNET_BIN_MEDIA_FRAMEWORK_STAGES_MULTISTREAM_SOURCE_STAGE_H_

#include <vector>

#include "garnet/bin/media/framework/models/multistream_source.h"
#include "garnet/bin/media/framework/stages/stage_impl.h"

namespace media_player {

// A stage that hosts a MultistreamSource.
// TODO(dalesat): May need to grow the list of outputs dynamically.
class MultistreamSourceStageImpl : public StageImpl,
                                   public MultistreamSourceStage {
 public:
  MultistreamSourceStageImpl(std::shared_ptr<MultistreamSource> source);

  ~MultistreamSourceStageImpl() override;

  // StageImpl implementation.
  size_t input_count() const override;

  Input& input(size_t index) override;

  size_t output_count() const override;

  Output& output(size_t index) override;

  std::shared_ptr<media::PayloadAllocator> PrepareInput(size_t index) override;

  void PrepareOutput(size_t index,
                     std::shared_ptr<media::PayloadAllocator> allocator,
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
  // MultistreamSourceStage implementation.
  void PostTask(const fxl::Closure& task) override;

  std::vector<Output> outputs_;
  std::shared_ptr<MultistreamSource> source_;
  media::PacketPtr cached_packet_;
  size_t cached_packet_output_index_;
  size_t ended_streams_;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_FRAMEWORK_STAGES_MULTISTREAM_SOURCE_STAGE_H_
