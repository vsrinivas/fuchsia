// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_METRICS_H_
#define SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_METRICS_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>
#include <unordered_map>

#include <src/lib/cobalt/cpp/cobalt_event_builder.h>
#include <src/lib/cobalt/cpp/cobalt_logger.h>

#include <src/media/lib/metrics/metrics.cb.h>

// Methods of this class can be called on any thread.
class CodecMetrics final {
 public:
  // A nop instance so unit tests don't need to wire up cobalt.
  CodecMetrics() __TA_EXCLUDES(lock_);

  // !service_directory is ok.  If !service_directory, the instance will be a nop instance until
  // SetServiceDirectory() is called.
  CodecMetrics(std::shared_ptr<sys::ServiceDirectory> service_directory) __TA_EXCLUDES(lock_);

  ~CodecMetrics() __TA_EXCLUDES(lock_);

  // Set the ServiceDirectory from which to get fuchsia.cobalt.LoggerFactory.  This can be nullptr.
  // This can be called again, regardless of whether there was already a previous ServiceDirectory.
  // Previously-queued events may be lost (especially recently-queued events) when switching to a
  // new ServiceDirectory.
  void SetServiceDirectory(std::shared_ptr<sys::ServiceDirectory> service_directory)
      __TA_EXCLUDES(lock_);

  // Log the event as EVENT_COUNT, with period_duration_micros 0, possibly aggregating with any
  // other calls to this method with the same component and event wihtin a short duration to limit
  // the rate of FIDL calls to cobalt, per the rate requirement/recommendation in the cobalt docs.
  //
  // No attempt is made to flush pending events before driver exit or suspend, since this driver
  // isn't expected to unbind very often, if ever, and if we're suspending already then it's
  // unlikely that the pending cobalt events would be persisted anyway.
  void LogEvent(media_metrics::StreamProcessorEventsMetricDimensionImplementation implementation,
                media_metrics::StreamProcessorEventsMetricDimensionEvent event)
      __TA_EXCLUDES(lock_);

 private:
  class PendingCountsKey {
   public:
    PendingCountsKey(
        media_metrics::StreamProcessorEventsMetricDimensionImplementation implementation,
        media_metrics::StreamProcessorEventsMetricDimensionEvent event);

    media_metrics::StreamProcessorEventsMetricDimensionImplementation implementation() const;
    media_metrics::StreamProcessorEventsMetricDimensionEvent event() const;

   private:
    media_metrics::StreamProcessorEventsMetricDimensionImplementation implementation_;
    media_metrics::StreamProcessorEventsMetricDimensionEvent event_;
  };
  struct PendingCountsKeyHash {
    size_t operator()(const PendingCountsKey& key) const noexcept;
  };
  struct PendingCountsKeyEqual {
    bool operator()(const PendingCountsKey& lhs, const PendingCountsKey& rhs) const noexcept;
  };

  void TryPostFlushCountsLocked() __TA_REQUIRES(lock_);
  void FlushPendingEventCounts() __TA_EXCLUDES(lock_);

  static constexpr zx::duration kMinLoggingPeriod = zx::sec(5);

  std::mutex lock_;

  // We have a separate async::Loop for each instance of cobalt::CobaltLogger, because CobaltLogger
  // requires that no async tasks posted by CobaltLogger out-live the CobaltLogger.  The easiest way
  // to achieve that is to give CobaltLogger its own async::Loop and Quit(), JoinThreads(),
  // Shutdown() that async::Loop before destroying the CobaltLogger.
  std::unique_ptr<async::Loop> loop_ = nullptr;

  std::unique_ptr<cobalt::CobaltLogger> cobalt_logger_ __TA_GUARDED(lock_);

  // Don't bother with std::optional<> here - instead subtract kMinLoggingPeriod so we'll log
  // immediately the first time.
  zx::time last_flushed_ __TA_GUARDED(lock_) = zx::clock::get_monotonic() - kMinLoggingPeriod;

  // From component and event to event count.
  using PendingCounts =
      std::unordered_map<PendingCountsKey, int64_t, PendingCountsKeyHash, PendingCountsKeyEqual>;
  PendingCounts pending_counts_ __TA_GUARDED(lock_);
};

#endif  // SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_METRICS_H_
