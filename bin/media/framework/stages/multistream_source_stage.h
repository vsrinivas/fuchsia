// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "apps/media/src/framework/models/multistream_source.h"
#include "apps/media/src/framework/stages/stage_impl.h"

namespace media {

// A stage that hosts a MultistreamSource.
// TODO(dalesat): May need to grow the list of outputs dynamically.
class MultistreamSourceStageImpl : public StageImpl,
                                   public MultistreamSourceStage {
 public:
  MultistreamSourceStageImpl(Engine* engine,
                         std::shared_ptr<MultistreamSource> source);

  ~MultistreamSourceStageImpl() override;

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
  void Update() override;

 private:
  std::vector<Output> outputs_;
  std::shared_ptr<MultistreamSource> source_;
  PacketPtr cached_packet_;
  size_t cached_packet_output_index_;
  size_t ended_streams_;
};

}  // namespace media
