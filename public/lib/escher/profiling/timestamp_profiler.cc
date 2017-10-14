// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/profiling/timestamp_profiler.h"

#include "lib/escher/impl/command_buffer.h"
#include "lib/escher/impl/vulkan_utils.h"

namespace escher {

static constexpr uint32_t kPoolSize = 100;

TimestampProfiler::TimestampProfiler(vk::Device device, float timestamp_period)
    : device_(device), timestamp_period_(timestamp_period) {}

TimestampProfiler::~TimestampProfiler() {
  FXL_DCHECK(ranges_.empty() && pools_.empty() && query_count_ == 0 &&
             current_pool_index_ == 0);
}

void TimestampProfiler::AddTimestamp(impl::CommandBuffer* cmd_buf,
                                     vk::PipelineStageFlagBits flags,
                                     std::string name) {
  QueryRange* range = ObtainRange(cmd_buf);
  cmd_buf->get().writeTimestamp(flags, range->pool, current_pool_index_);
  results_.push_back(Result{0, 0, std::move(name)});
  ++range->count;
  ++current_pool_index_;
  ++query_count_;
}

std::vector<TimestampProfiler::Result> TimestampProfiler::GetQueryResults() {
  uint32_t result_index = 0;
  for (auto& range : ranges_) {
    // We don't wait for results.  Crash if results aren't immediately
    // available.
    vk::Result status = device_.getQueryPoolResults(
        range.pool, range.start_index, range.count,
        vk::ArrayProxy<Result>(range.count, &results_[result_index]),
        sizeof(Result), vk::QueryResultFlagBits::e64);
    FXL_DCHECK(status == vk::Result::eSuccess);

    result_index += range.count;
  }
  FXL_CHECK(result_index == query_count_);

  for (auto& pool : pools_) {
    device_.destroyQueryPool(pool);
  }
  ranges_.clear();
  pools_.clear();
  query_count_ = 0;
  current_pool_index_ = 0;

  const uint64_t first_time = results_[0].time;
  const float microsecond_multiplier = timestamp_period_ * 0.001f;
  for (size_t i = 0; i < results_.size(); ++i) {
    results_[i].time = static_cast<uint64_t>((results_[i].time - first_time) *
                                             microsecond_multiplier);
  }
  results_[0].elapsed = 0;
  for (size_t i = 1; i < results_.size(); ++i) {
    results_[i].elapsed = results_[i].time - results_[i - 1].time;
  }

  return std::move(results_);
}

TimestampProfiler::QueryRange* TimestampProfiler::ObtainRange(
    impl::CommandBuffer* cmd_buf) {
  if (ranges_.empty() || current_pool_index_ == kPoolSize) {
    return CreateRangeAndPool(cmd_buf);
  } else if (ranges_.back().command_buffer != cmd_buf->get()) {
    return CreateRange(cmd_buf);
  } else {
    auto range = &ranges_.back();
    FXL_DCHECK(current_pool_index_ < kPoolSize);
    if (current_pool_index_ != range->start_index + range->count) {
      FXL_LOG(INFO) << current_pool_index_ << "  " << range->start_index << "  "
                    << range->count;
    }
    FXL_DCHECK(current_pool_index_ == range->start_index + range->count);
    return range;
  }
}

TimestampProfiler::QueryRange* TimestampProfiler::CreateRangeAndPool(
    impl::CommandBuffer* cmd_buf) {
  vk::QueryPoolCreateInfo info;
  info.flags = vk::QueryPoolCreateFlags();  // no flags currently exist
  info.queryType = vk::QueryType::eTimestamp;
  info.queryCount = kPoolSize;
  vk::QueryPool pool = ESCHER_CHECKED_VK_RESULT(device_.createQueryPool(info));

  QueryRange range;
  range.pool = pool;
  range.command_buffer = cmd_buf->get();
  range.start_index = 0;
  range.count = 0;

  current_pool_index_ = 0;
  pools_.push_back(pool);
  ranges_.push_back(range);

  return &ranges_.back();
}

TimestampProfiler::QueryRange* TimestampProfiler::CreateRange(
    impl::CommandBuffer* cmd_buf) {
  QueryRange& prev = ranges_.back();
  FXL_DCHECK(!ranges_.empty() && current_pool_index_ < kPoolSize);
  FXL_DCHECK(current_pool_index_ == prev.start_index + prev.count);

  QueryRange range;
  range.pool = prev.pool;
  range.command_buffer = cmd_buf->get();
  range.start_index = prev.start_index + prev.count;
  range.count = 0;
  FXL_DCHECK(range.start_index == current_pool_index_);

  ranges_.push_back(range);
  return &ranges_.back();
}

}  // namespace escher
