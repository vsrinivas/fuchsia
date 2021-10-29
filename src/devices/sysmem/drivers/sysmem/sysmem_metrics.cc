// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sysmem_metrics.h"

#include "src/devices/sysmem/metrics/metrics.cb.h"

SysmemMetrics::SysmemMetrics()
    : metrics_buffer_(cobalt::MetricsBuffer::Create(sysmem_metrics::kProjectId)),
      unused_page_check_(
          metrics_buffer_->CreateMetricBuffer(sysmem_metrics::kUnusedPageCheckOldMetricId)) {}

cobalt::MetricsBuffer& SysmemMetrics::metrics_buffer() { return *metrics_buffer_; }

void SysmemMetrics::LogUnusedPageCheck(sysmem_metrics::UnusedPageCheckMetricDimensionEvent event) {
  unused_page_check_.LogEvent({event});
}

void SysmemMetrics::LogUnusedPageCheckCounts(uint32_t succeeded_count, uint32_t failed_count) {
  if (succeeded_count) {
    unused_page_check_pending_success_count_ += succeeded_count;
  }
  if (failed_count) {
    unused_page_check_.LogEventCount(
        {sysmem_metrics::UnusedPageCheckOldMetricDimensionEvent_PatternCheckFailed}, failed_count);
  }

  zx::time now = zx::clock::get_monotonic();
  if ((now >= unused_page_check_last_flush_time_ + kUnusedPageCheckFlushSuccessPeriod) &&
      unused_page_check_pending_success_count_) {
    unused_page_check_.LogEventCount(
        {sysmem_metrics::UnusedPageCheckOldMetricDimensionEvent_PatternCheckOk},
        unused_page_check_pending_success_count_);
    unused_page_check_pending_success_count_ = 0;
    unused_page_check_last_flush_time_ = now;
  }
}
