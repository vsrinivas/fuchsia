// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/media/codec_impl/codec_metrics.h"

#include <inttypes.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/syslog/global.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <mutex>

#include "lib/async/cpp/task.h"
#include "src/lib/cobalt/cpp/cobalt_event_builder.h"

namespace {

const char* kLogTag = "CodecMetrics";

}

CodecMetrics::CodecMetrics() {
  std::lock_guard<std::mutex> lock(lock_);
  ZX_DEBUG_ASSERT(!loop_ && !cobalt_logger_);
}

CodecMetrics::CodecMetrics(std::shared_ptr<sys::ServiceDirectory> service_directory) {
  SetServiceDirectory(service_directory);
}

CodecMetrics::~CodecMetrics() {
  // We need the async::PostTask() if cobalt_logger_.
  SetServiceDirectory(nullptr);
}

void CodecMetrics::SetServiceDirectory(std::shared_ptr<sys::ServiceDirectory> service_directory) {
  FX_LOGF(INFO, kLogTag, "CodecMetrics::SetServiceDirectory()");
  std::unique_ptr<cobalt::CobaltLogger> logger_to_delete_outside_lock;
  std::unique_ptr<async::Loop> loop_to_stop_outside_lock;
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    ZX_DEBUG_ASSERT(!!loop_ == !!cobalt_logger_);
    if (cobalt_logger_) {
      FX_LOGF(INFO, kLogTag, "CodecMetrics::SetServiceDirectory() removing old logger.");
      ZX_DEBUG_ASSERT(loop_);
      // Clean these up after we've released lock_, to avoid potential deadlock waiting on a thread
      // that may be trying to get lock_.
      loop_to_stop_outside_lock = std::move(loop_);
      logger_to_delete_outside_lock = std::move(cobalt_logger_);
    }
    ZX_DEBUG_ASSERT(!loop_ && !cobalt_logger_);
    if (service_directory) {
      FX_LOGF(INFO, kLogTag, "CodecMetrics::SetServiceDirectory() creating new logger.");
      std::unique_ptr<cobalt::CobaltLogger> new_logger;
      auto loop = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
      zx_status_t status = loop->StartThread("CodecMetrics");
      if (status != ZX_OK) {
        FX_LOGF(WARNING, kLogTag, "CodecMetrics::SetServiceDirectory() thread creation failed.");
        // ~loop
        // ~service_directory
        return;
      }
      zx::event cobalt_logger_creation_done;
      status = zx::event::create(/*options=*/0, &cobalt_logger_creation_done);
      if (status != ZX_OK) {
        FX_LOGF(WARNING, kLogTag, "zx::event::create() failed - status: %d", status);
        // ~loop
        // ~service_directory
        return;
      }
      // Must create cobalt::CobaltLogger on same dispatcher that it'll use.
      async::PostTask(loop->dispatcher(),
                      [&loop, &service_directory, &new_logger, &cobalt_logger_creation_done] {
                        new_logger = cobalt::NewCobaltLoggerFromProjectId(
                            loop->dispatcher(), service_directory, media_metrics::kProjectId);
                        cobalt_logger_creation_done.signal(0, ZX_EVENT_SIGNALED);
                      });
      zx_signals_t observed = 0;
      status =
          cobalt_logger_creation_done.wait_one(ZX_EVENT_SIGNALED, zx::time::infinite(), &observed);
      if (status != ZX_OK) {
        // ~loop
        // ~new_logger
        return;
      }
      ZX_DEBUG_ASSERT((observed & ZX_EVENT_SIGNALED) != 0);
      loop_ = std::move(loop);
      cobalt_logger_ = std::move(new_logger);
      ZX_DEBUG_ASSERT(!!loop_ && !!cobalt_logger_);
      if (!pending_counts_.empty()) {
        FX_LOGF(INFO, kLogTag, "CodecMetrics::SetServiceDirectory() flushing counts soon.");
        TryPostFlushCountsLocked();
      }
    }
    ZX_DEBUG_ASSERT(!!loop_ == !!cobalt_logger_);
  }  // ~lock
  ZX_DEBUG_ASSERT(!!loop_to_stop_outside_lock == !!logger_to_delete_outside_lock);
  if (loop_to_stop_outside_lock) {
    // Stop the loop first, to avoid any async tasks queued by the CobaltLogger outlasting the
    // CobaltLogger.
    loop_to_stop_outside_lock->Quit();
    loop_to_stop_outside_lock->JoinThreads();
    loop_to_stop_outside_lock->Shutdown();
    // Delete here for clarity.
    loop_to_stop_outside_lock = nullptr;
    // Now it's safe to delete the CobaltLogger, which we do here manually for clarity.
    logger_to_delete_outside_lock = nullptr;
  }
}

void CodecMetrics::LogEvent(
    media_metrics::StreamProcessorEventsMetricDimensionImplementation implementation,
    media_metrics::StreamProcessorEventsMetricDimensionEvent event) {
  std::lock_guard<std::mutex> lock(lock_);
  ZX_DEBUG_ASSERT(!!loop_ == !!cobalt_logger_);
  bool was_empty = pending_counts_.empty();
  PendingCountsKey key(implementation, event);
  ++pending_counts_[key];
  if (was_empty) {
    // We don't try to process locally, because if we're logging infrequently then the optimization
    // wouldn't matter, and if we're logging frequently then we need to post in order to delay
    // anyway.  So we opt to keep the code simpler and always post even if the deadline is in the
    // past.
    TryPostFlushCountsLocked();
  }
}

void CodecMetrics::FlushPendingEventCounts() {
  // This method is never called on the nop instance.
  FX_LOGF(INFO, kLogTag, "CodecMetrics::FlushPendingEventCounts()");
  ZX_DEBUG_ASSERT(loop_);
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    last_flushed_ = zx::clock::get_monotonic();
    PendingCounts snapped_pending_event_counts;
    snapped_pending_event_counts.swap(pending_counts_);
    auto iter = snapped_pending_event_counts.begin();
    constexpr uint32_t kMaxBatchSize = 64;
    std::vector<fuchsia::cobalt::CobaltEvent> batch;
    while (iter != snapped_pending_event_counts.end()) {
      auto [key, count] = *iter;
      iter++;
      batch.emplace_back(cobalt::CobaltEventBuilder(media_metrics::kStreamProcessorEventsMetricId)
                             .with_event_codes({static_cast<uint32_t>(key.implementation()),
                                                static_cast<uint32_t>(key.event())})
                             .as_count_event(/*period_duration_micros=*/0, count));
      ZX_DEBUG_ASSERT(batch.size() <= kMaxBatchSize);
      if (batch.size() == kMaxBatchSize || iter == snapped_pending_event_counts.end()) {
        FX_LOGF(INFO, kLogTag, "CodecMetrics::FlushPendingEventCounts() batch.size: %" PRId64,
                batch.size());
        cobalt_logger_->LogCobaltEvents(std::move(batch));
        ZX_DEBUG_ASSERT(batch.empty());
      }
    }
  }
}

void CodecMetrics::TryPostFlushCountsLocked() {
  ZX_DEBUG_ASSERT(!!loop_ == !!cobalt_logger_);
  if (cobalt_logger_) {
    ZX_DEBUG_ASSERT(loop_);
    async::PostTaskForTime(
        loop_->dispatcher(), [this] { FlushPendingEventCounts(); },
        last_flushed_ + kMinLoggingPeriod);
  }
}

CodecMetrics::PendingCountsKey::PendingCountsKey(
    media_metrics::StreamProcessorEventsMetricDimensionImplementation implementation,
    media_metrics::StreamProcessorEventsMetricDimensionEvent event)
    : implementation_(implementation), event_(event) {}

media_metrics::StreamProcessorEventsMetricDimensionImplementation
CodecMetrics::PendingCountsKey::implementation() const {
  return implementation_;
}

media_metrics::StreamProcessorEventsMetricDimensionEvent CodecMetrics::PendingCountsKey::event()
    const {
  return event_;
}

size_t CodecMetrics::PendingCountsKeyHash::operator()(const PendingCountsKey& key) const noexcept {
  // Rely on size_t being unsigned so it'll wrap without being undefined behavior.
  return std::hash<media_metrics::StreamProcessorEventsMetricDimensionImplementation>{}(
             key.implementation()) +
         std::hash<media_metrics::StreamProcessorEventsMetricDimensionEvent>{}(key.event());
}

bool CodecMetrics::PendingCountsKeyEqual::operator()(const PendingCountsKey& lhs,
                                                     const PendingCountsKey& rhs) const noexcept {
  return std::equal_to<media_metrics::StreamProcessorEventsMetricDimensionImplementation>{}(
             lhs.implementation(), rhs.implementation()) &&
         std::equal_to<media_metrics::StreamProcessorEventsMetricDimensionEvent>{}(lhs.event(),
                                                                                   rhs.event());
}
