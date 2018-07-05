// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_IMPL_VK_PIPELINE_CACHE_H_
#define LIB_ESCHER_IMPL_VK_PIPELINE_CACHE_H_

#include <memory>
#include <unordered_map>

#include "lib/escher/impl/vk/pipeline.h"
#include "lib/escher/impl/vk/pipeline_factory.h"

namespace escher {
namespace impl {

// A simple, thread-safe asynchronous cache for Vulkan Pipelines.
class PipelineCache {
 public:
  PipelineCache();
  ~PipelineCache();

  std::shared_future<PipelinePtr> GetPipeline(
      const PipelineSpec& spec, const PipelineFactoryPtr& factory);

 private:
  std::unordered_map<PipelineSpec, std::shared_future<PipelinePtr>,
                     PipelineSpec::HashMapHasher>
      map_;
  std::mutex mutex_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PipelineCache);
};

}  // namespace impl
}  // namespace escher

#endif  // LIB_ESCHER_IMPL_VK_PIPELINE_CACHE_H_
