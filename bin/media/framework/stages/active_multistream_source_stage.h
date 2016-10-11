// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <deque>
#include <vector>

#include "apps/media/src/framework/models/active_multistream_source.h"
#include "apps/media/src/framework/stages/stage.h"
#include "lib/ftl/synchronization/mutex.h"
#include "lib/ftl/synchronization/thread_annotations.h"

namespace mojo {
namespace media {

// A stage that hosts an ActiveMultistreamSource.
class ActiveMultistreamSourceStage : public Stage {
 public:
  ActiveMultistreamSourceStage(std::shared_ptr<ActiveMultistreamSource> source);

  ~ActiveMultistreamSourceStage() override;

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
  std::vector<std::deque<PacketPtr>> packets_per_output_;
  std::shared_ptr<ActiveMultistreamSource> source_;
  ActiveMultistreamSource::SupplyCallback supply_function_;

  mutable ftl::Mutex mutex_;
  size_t ended_streams_ FTL_GUARDED_BY(mutex_) = 0;
  bool packet_request_outstanding_ FTL_GUARDED_BY(mutex_) = false;
};

}  // namespace media
}  // namespace mojo
