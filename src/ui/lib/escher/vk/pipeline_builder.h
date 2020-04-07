// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_VK_PIPELINE_BUILDER_H_
#define SRC_UI_LIB_ESCHER_VK_PIPELINE_BUILDER_H_

#include <lib/fit/function.h>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/lib/escher/util/hash.h"

#include <vulkan/vulkan.hpp>

namespace escher {

// PipelineBuilder is responsible for building Vulkan pipelines and *managing* persistence of
// VkPipelineCache data.  This class doesn't know how to write data to e.g. disk; the client is
// responsible for:
//   - providing a callback to actually persist the data
//   - calling |MaybeStorePipelineCacheData()| to trigger the callback
class PipelineBuilder {
 public:
  using StorePipelineCacheDataCallback = fit::function<void(std::vector<uint8_t>)>;
  using LogPipelineCreationCallback =
      fit::function<void(const vk::GraphicsPipelineCreateInfo* graphics_info,
                         const vk::ComputePipelineCreateInfo* compute_info)>;

  // Create a pipeline builder which doesn't use a VkPipelineCache.
  PipelineBuilder(vk::Device device);
  // Create a pipeline builder which creates a VkPipelineCache, which is used to accelerate pipeline
  // building. |store_data_callback| will be invoked during |MaybeStorePipelineCacheData()|, but
  // only if there is new data to store.  If |initial_cache_data| is nullptr, then
  // |initial_cache_data_size| must be zero.
  PipelineBuilder(vk::Device device, const void* initial_cache_data, size_t initial_cache_data_size,
                  StorePipelineCacheDataCallback store_data_callback);

  ~PipelineBuilder();

  // Return a newly created pipeline, using the pipeline cache to accelerate creation if possible.
  // If |do_logging| is true, the callback set by set_log_pipeline_creation_callback() will be
  // invoked.
  vk::Pipeline BuildGraphicsPipeline(const vk::GraphicsPipelineCreateInfo& info, bool do_logging);
  vk::Pipeline BuildComputePipeline(const vk::ComputePipelineCreateInfo& info, bool do_logging);

  // Invoke the |StorePipelineCacheDataCallback| that was passed to the constructor, but only if
  // there is updated cache data which needs to be persistently stored.   The implementation takes
  // steps to efficiently exit early when there is no updated data to store, so it is OK to call
  // this fairly often.
  void MaybeStorePipelineCacheData();

  fxl::WeakPtr<PipelineBuilder> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  // Set a callback that is invoked whenever a pipeline is built lazily.  This is used by clients
  // who generate pipelines at startup, to be notified if they find themselves accidentally
  // generating pipelines in the middle of a frame.  When the callback is invoked, one of the two
  // args will be nullptr and the other won't, depending on whether a graphics or compute pipeline
  // is being built.
  void set_log_pipeline_creation_callback(LogPipelineCreationCallback callback) {
    log_creation_callback_ = std::move(callback);
  }

 private:
  vk::Device device_;
  vk::PipelineCache cache_;

  // Invoked whenever there is updated cache data to be persisted.
  StorePipelineCacheDataCallback store_data_callback_;

  // Invoked when a pipeline is built with |do_logging| set to true.
  LogPipelineCreationCallback log_creation_callback_;

  // Used by |MaybeStorePipelineCacheData()| to accelerate the case where no new pipelines have been
  // created since the last call to |MaybeStorePipelineCacheData()|.  This is common because
  // pipelines are reused from frame-to-frame; most frames use the same pipelines as the previous
  // frames.
  bool created_pipeline_since_last_store_ = false;

  // Used by |MaybeStorePipelineCacheData()| to accelerate the case where a pipeline was created,
  // and the cache already contained the data to accelerate the creation of that pipeline.  In such
  // cases, there is no need to invoke |store_data_callback_|.
  Hash hash_ = {0};

  fxl::WeakPtrFactory<PipelineBuilder> weak_factory_;  // must be last
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_VK_PIPELINE_BUILDER_H_
