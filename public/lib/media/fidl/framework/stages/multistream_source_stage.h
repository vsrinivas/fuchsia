// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_MEDIA_FRAMEWORK_STAGES_MULTISTREAM_SOURCE_STAGE_H_
#define SERVICES_MEDIA_FRAMEWORK_STAGES_MULTISTREAM_SOURCE_STAGE_H_

#include <vector>

#include "services/media/framework/models/multistream_source.h"
#include "services/media/framework/stages/stage.h"

namespace mojo {
namespace media {

// A stage that hosts a MultistreamSource.
// TODO(dalesat): May need to grow the list of outputs dynamically.
class MultistreamSourceStage : public Stage {
 public:
  MultistreamSourceStage(std::shared_ptr<MultistreamSource> source);

  ~MultistreamSourceStage() override;

  // Stage implementation.
  size_t input_count() const override;

  Input& input(size_t index) override;

  size_t output_count() const override;

  Output& output(size_t index) override;

  PayloadAllocator* PrepareInput(size_t index) override;

  void PrepareOutput(size_t index,
                     PayloadAllocator* allocator,
                     const UpstreamCallback& callback) override;

  void UnprepareOutput(size_t index, const UpstreamCallback& callback) override;

  void Update(Engine* engine) override;

  void FlushInput(size_t index, const DownstreamCallback& callback) override;

  void FlushOutput(size_t index) override;

 private:
  std::vector<Output> outputs_;
  std::shared_ptr<MultistreamSource> source_;
  PacketPtr cached_packet_;
  size_t cached_packet_output_index_;
  size_t ended_streams_;
};

}  // namespace media
}  // namespace mojo

#endif  // SERVICES_MEDIA_FRAMEWORK_STAGES_MULTISTREAM_SOURCE_STAGE_H_
