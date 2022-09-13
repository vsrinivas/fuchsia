// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_METRICS_BUFFER_METRICS_BUFFER_H_
#define SRC_LIB_METRICS_BUFFER_METRICS_BUFFER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>
#include <unordered_map>

#include <src/lib/cobalt/cpp/cobalt_event_builder.h>
#include <src/lib/cobalt/cpp/cobalt_logger.h>

namespace cobalt {

class MetricsBuffer;

// This class is a convenience interface which remembers a metric_id.
class MetricBuffer {
 public:
  MetricBuffer(const MetricBuffer& to_copy) = delete;
  MetricBuffer& operator=(const MetricBuffer& to_copy) = delete;
  MetricBuffer(MetricBuffer&& to_move) = default;
  MetricBuffer& operator=(MetricBuffer&& to_move) = default;
  ~MetricBuffer() = default;

  void LogEvent(std::vector<uint32_t> dimension_values);
  void LogEventCount(std::vector<uint32_t> dimension_values, uint32_t count);

 private:
  friend class MetricsBuffer;
  MetricBuffer(std::shared_ptr<MetricsBuffer> parent, uint32_t metric_id);
  std::shared_ptr<MetricsBuffer> parent_;
  uint32_t metric_id_ = 0;
};

// The purpose of this class is to ensure the rate of messages to Cobalt stays reasonable, per
// Cobalt's rate requirement/recommendation in the Cobalt docs.
//
// Typically it'll make sense to only have one of these per process, but that's not enforced.
//
// Methods of this class can be called on any thread.
class MetricsBuffer final : public std::enable_shared_from_this<MetricsBuffer> {
 public:
  // Initially a nop instance, so unit tests don't need to wire up cobalt.  Call
  // SetServiceDirectory() to enable and start logging.
  static std::shared_ptr<MetricsBuffer> Create(uint32_t project_id);

  // !service_directory is ok.  If !service_directory, the instance will be a nop instance until
  // SetServiceDirectory() is called.
  static std::shared_ptr<MetricsBuffer> Create(
      uint32_t project_id, std::shared_ptr<sys::ServiceDirectory> service_directory);

  ~MetricsBuffer() __TA_EXCLUDES(lock_);

  // Set the ServiceDirectory from which to get fuchsia.cobalt.LoggerFactory.  This can be nullptr.
  // This can be called again, regardless of whether there was already a previous ServiceDirectory.
  // Previously-queued events may be lost (especially recently-queued events) when switching to a
  // new ServiceDirectory.
  void SetServiceDirectory(std::shared_ptr<sys::ServiceDirectory> service_directory)
      __TA_EXCLUDES(lock_);

  // This specifies the minimum amount of time between logging batches to cobalt.  If enough
  // different metrics have accumulated to force more than one message to cobalt, then more than
  // one message is possible, but typically a single message will be sent to cobalt no more often
  // than this.  In unit tests we use this to turn the min_logging_period way down so that tests can
  // finish faster.
  void SetMinLoggingPeriod(zx::duration min_logging_period);

  // Log the event as EVENT_COUNT, with period_duration_micros 0, possibly aggregating with any
  // other calls to this method with the same component and event wihtin a short duration to limit
  // the rate of FIDL calls to Cobalt, per the rate requirement/recommendation in the Cobalt docs.
  void LogEvent(uint32_t metric_id, std::vector<uint32_t> dimension_values) __TA_EXCLUDES(lock_);

  void LogEventCount(uint32_t metric_id, std::vector<uint32_t> dimension_values, uint32_t count);

  // Use sparingly, only when it's appropriate to force the counts to flush to Cobalt, which will
  // typically only be before orderly exit or in situations like driver suspend.  Over-use of this
  // method will break the purpose of using this class, which is to ensure the rate of messages to
  // Cobalt stays reasonable.
  void ForceFlush() __TA_EXCLUDES(lock_);

  MetricBuffer CreateMetricBuffer(uint32_t metric_id);

 private:
  explicit MetricsBuffer(uint32_t project_id) __TA_EXCLUDES(lock_);

  MetricsBuffer(uint32_t project_id, std::shared_ptr<sys::ServiceDirectory> service_directory)
      __TA_EXCLUDES(lock_);

  class PendingCountsKey {
   public:
    PendingCountsKey(uint32_t metric_id, std::vector<uint32_t> dimension_values);

    uint32_t metric_id() const;
    const std::vector<uint32_t>& dimension_values() const;

   private:
    uint32_t metric_id_;
    std::vector<uint32_t> dimension_values_;
  };
  struct PendingCountsKeyHash {
    size_t operator()(const PendingCountsKey& key) const noexcept;

   private:
    std::hash<uint32_t> hash_uint32_;
  };
  struct PendingCountsKeyEqual {
    bool operator()(const PendingCountsKey& lhs, const PendingCountsKey& rhs) const noexcept;
  };

  void TryPostFlushCountsLocked() __TA_REQUIRES(lock_);
  void FlushPendingEventCounts() __TA_EXCLUDES(lock_);

  static constexpr zx::duration kDefaultMinLoggingPeriod = zx::sec(5);

  std::mutex lock_;

  const uint32_t project_id_{};

  // We have a separate async::Loop for each instance of cobalt::CobaltLogger, because CobaltLogger
  // requires that no async tasks posted by CobaltLogger out-live the CobaltLogger.  The easiest way
  // to achieve that is to give CobaltLogger its own async::Loop and Quit(), JoinThreads(),
  // Shutdown() that async::Loop before destroying the CobaltLogger.
  std::unique_ptr<async::Loop> loop_;

  std::unique_ptr<cobalt::CobaltLogger> cobalt_logger_ __TA_GUARDED(lock_);

  zx::time last_flushed_ __TA_GUARDED(lock_) = zx::time::infinite_past();

  // From component and event to event count.
  using PendingCounts =
      std::unordered_map<PendingCountsKey, int64_t, PendingCountsKeyHash, PendingCountsKeyEqual>;
  PendingCounts pending_counts_ __TA_GUARDED(lock_);

  zx::duration min_logging_period_ = kDefaultMinLoggingPeriod;
};

}  // namespace cobalt

#endif  // SRC_LIB_METRICS_BUFFER_METRICS_BUFFER_H_
