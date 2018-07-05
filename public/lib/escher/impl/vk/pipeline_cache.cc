// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/vk/pipeline_cache.h"

namespace escher {
namespace impl {

PipelineCache::PipelineCache() {}

PipelineCache::~PipelineCache() {}

std::shared_future<PipelinePtr> PipelineCache::GetPipeline(
    const PipelineSpec& spec, const PipelineFactoryPtr& factory) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = map_.find(spec);
  if (it != map_.end()) {
    // The pipeline already exists, or someone else is in the process of
    // creating it.  Either way, return the stashed future.
    return it->second;
  }

  // The pipeline has not been requested; create a new one.

  // Create a promise that we will asynchronously resolve.  We must store it in
  // a shared_ptr because we cannot move it into the lambda, since functions are
  // copyable and promise is move-only.
  auto promise = std::make_shared<std::promise<PipelinePtr>>();

  // Obtain the shared-future that we will return from this function, and stash
  // a copy in the map.
  auto result = promise->get_future().share();
  map_[spec] = result;

  // Wait for the factory on another thread.
  std::thread([spec, factory, promise{move(promise)}]() {
    auto pipeline = factory->NewPipeline(std::move(spec)).get();
    // If this fails, then subsequent requests for the same spec are
    // guaranteed to fail forever.
    FXL_DCHECK(pipeline);
    promise->set_value(std::move(pipeline));
  })
      .detach();

  return result;
}

}  // namespace impl
}  // namespace escher
