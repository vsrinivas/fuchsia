// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/pipeline_builder.h"

#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/util/hasher.h"
#include "src/ui/lib/escher/util/trace_macros.h"

namespace escher {

static Hash GenerateHash(const void* bytes, size_t num_bytes) {
  TRACE_DURATION("gfx", "PipelineBuilder::GenerateHash");
  Hasher h;
  if (num_bytes % 4 == 0) {
    h.data(reinterpret_cast<const uint32_t*>(bytes), num_bytes / 4);
  } else {
    h.data(reinterpret_cast<const uint8_t*>(bytes), num_bytes);
  }
  return h.value();
}

PipelineBuilder::PipelineBuilder(vk::Device device) : device_(device), weak_factory_(this) {
  FX_DCHECK(device);

  // There is no StorePipelineCacheDataCallback, so hash will never be computed again.
  hash_.val = 0;
}

PipelineBuilder::PipelineBuilder(vk::Device device, const void* initial_cache_data,
                                 size_t initial_cache_data_size,
                                 StorePipelineCacheDataCallback store_data_callback)
    : device_(device), store_data_callback_(std::move(store_data_callback)), weak_factory_(this) {
  FX_DCHECK(device);
  FX_DCHECK(store_data_callback_);
  FX_DCHECK(initial_cache_data || initial_cache_data_size == 0);
  TRACE_DURATION("gfx", "PipelineBuilder[constructor]");

  // Use the initial data to populate a VkPipelineCache.
  vk::PipelineCacheCreateInfo info;
  info.initialDataSize = initial_cache_data_size;
  info.pInitialData = initial_cache_data;
  cache_ = ESCHER_CHECKED_VK_RESULT(device_.createPipelineCache(info));

  // Initialize the hash for comparison in |MaybeStorePipelineCacheData()|; the callback will only
  // be invoked when the cache data changes.
  hash_ = GenerateHash(initial_cache_data, initial_cache_data_size);
}

PipelineBuilder::~PipelineBuilder() {
  MaybeStorePipelineCacheData();
  device_.destroyPipelineCache(cache_);
}

vk::Pipeline PipelineBuilder::BuildGraphicsPipeline(const vk::GraphicsPipelineCreateInfo& info,
                                                    bool do_logging) {
  TRACE_DURATION("gfx", "PipelineBuilder::BuildGraphicsPipeline");
  if (do_logging && log_creation_callback_) {
    TRACE_DURATION("gfx", "PipelineBuilder::BuildGraphicsPipeline[logging]");
    log_creation_callback_(&info, nullptr);
  }
  created_pipeline_since_last_store_ = true;
  auto pipeline = ESCHER_CHECKED_VK_RESULT(device_.createGraphicsPipeline(cache_, info));
  return pipeline;
}

vk::Pipeline PipelineBuilder::BuildComputePipeline(const vk::ComputePipelineCreateInfo& info,
                                                   bool do_logging) {
  TRACE_DURATION("gfx", "PipelineBuilder::BuildComputePipeline");
  if (do_logging && log_creation_callback_) {
    TRACE_DURATION("gfx", "PipelineBuilder::BuildComputePipeline[logging]");
    log_creation_callback_(nullptr, &info);
  }
  created_pipeline_since_last_store_ = true;
  return ESCHER_CHECKED_VK_RESULT(device_.createComputePipeline(cache_, info));
}

void PipelineBuilder::MaybeStorePipelineCacheData() {
  if (created_pipeline_since_last_store_ && store_data_callback_) {
    created_pipeline_since_last_store_ = false;

    std::vector<uint8_t> bytes;
    {
      TRACE_DURATION("gfx", "PipelineBuilder::MaybeStorePipelineCacheData[vulkan]");
      bytes = ESCHER_CHECKED_VK_RESULT(device_.getPipelineCacheData(cache_));
    }

    // We'll only invoke the callback when the cache data changes.
    auto new_hash = GenerateHash(bytes.data(), bytes.size());
    if (hash_ != new_hash) {
      TRACE_DURATION("gfx", "PipelineBuilder::MaybeStorePipelineCacheData[callback]", "hash",
                     TA_UINT64(new_hash.val), "num_bytes", TA_UINT64(bytes.size()));
      hash_ = new_hash;
      store_data_callback_(std::move(bytes));
    }
  }
}

}  // namespace escher
