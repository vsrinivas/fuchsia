// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_SERVICES_FRAMEWORK_STAGES_ACTIVE_SOURCE_STAGE_H_
#define APPS_MEDIA_SERVICES_FRAMEWORK_STAGES_ACTIVE_SOURCE_STAGE_H_

#include <deque>

#include "apps/media/services/framework/models/active_source.h"
#include "apps/media/services/framework/stages/stage.h"

namespace mojo {
namespace media {

// A stage that hosts an ActiveSource.
class ActiveSourceStage : public Stage {
 public:
  ActiveSourceStage(std::shared_ptr<ActiveSource> source);

  ~ActiveSourceStage() override;

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
  Output output_;
  std::shared_ptr<ActiveSource> source_;
  bool prepared_;
  ActiveSource::SupplyCallback supply_function_;
  std::deque<PacketPtr> packets_;
};

}  // namespace media
}  // namespace mojo

#endif  // APPS_MEDIA_SERVICES_FRAMEWORK_STAGES_ACTIVE_SOURCE_STAGE_H_
