// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "lib/escher/forward_declarations.h"
#include "lib/escher/vk/vulkan_context.h"
#include "lib/fxl/memory/ref_counted.h"

namespace escher {

class TimestampProfiler : public fxl::RefCountedThreadSafe<TimestampProfiler> {
 public:
  TimestampProfiler(vk::Device device, float timestamp_period);
  ~TimestampProfiler();

  // Add a Vulkan timestamp-query that will mark the time that all previous
  // commands in |cmd_buf| have finished the pipeline stage specified by
  // |flags|.  For example, use eVertexShader to mark the time that vertex
  // shaders have been applied to all vertices from previous draw-calls.
  //
  // Note: you should understand the caveats in the Vulkan spec regarding the
  // accuracy of these timestamps.
  void AddTimestamp(impl::CommandBuffer* cmd_buf,
                    vk::PipelineStageFlagBits flags,
                    std::string name);

  struct Result {
    uint64_t time;     // microseconds elapsed since the first timestamp
    uint64_t elapsed;  // microseconds elapsed since the previous timestamp
    std::string name;
  };

  std::vector<Result> GetQueryResults();

 private:
  // Each QueryRange keeps track of current usage of a separate vk::QueryPool.
  // A new pool (and hence a new QueryRange) is used whenever:
  //   - the capacity of the previous pool is reached.
  //   - a different CommandBuffer is passed to AddTimestamp().
  struct QueryRange {
    vk::QueryPool pool;
    vk::CommandBuffer command_buffer;
    uint32_t start_index;  // within the pool
    uint32_t count;
  };

  QueryRange* ObtainRange(impl::CommandBuffer* cmd_buf);
  QueryRange* CreateRange(impl::CommandBuffer* cmd_buf);
  QueryRange* CreateRangeAndPool(impl::CommandBuffer* cmd_buf);

  std::vector<QueryRange> ranges_;
  std::vector<vk::QueryPool> pools_;

  // Remembers timestamp names, and will eventually be filled in with timestamp
  // values before returning results.
  std::vector<Result> results_;

  uint32_t query_count_ = 0;
  uint32_t current_pool_index_ = 0;

  const vk::Device device_;
  const float timestamp_period_;
};

}  // namespace escher
