// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_JOURNAL_INTERNAL_METRICS_H_
#define FS_JOURNAL_INTERNAL_METRICS_H_
#include <lib/inspect/cpp/inspect.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>

#include <cobalt-client/cpp/collector.h>
#include <fs/metrics/composite_latency_event.h>
#include <fs/metrics/events.h>

namespace fs {

class MetricsTrait {
 public:
  virtual ~MetricsTrait() = default;
  virtual inspect::Node* GetInspectRoot() = 0;
  virtual cobalt_client::Collector* GetCollector() = 0;
  virtual fs_metrics::CompositeLatencyEvent NewLatencyEvent(fs_metrics::Event event) = 0;
};

class JournalMetrics {
 public:
  // A wrapper class around CompositeLatencyEvent to make easier to set block
  // count and success values.
  class LatencyEvent {
   public:
    explicit LatencyEvent(std::optional<fs_metrics::CompositeLatencyEvent> event_or)
        : event_or_(std::move(event_or)) {}

    // Sets block count for the current operation.
    void set_block_count(uint64_t block_count) {
      if (!event_or_.has_value()) {
        return;
      }
      event_or_.value().mutable_latency_event()->mutable_options()->block_count = block_count;
    }

    // If true, the operation is cosidered to be successful.
    void set_success(bool success) {
      if (!event_or_.has_value()) {
        return;
      }
      event_or_.value().mutable_latency_event()->mutable_options()->success = success;
    }

   private:
    // Internal tracker for the latency event. Set to nullopt if the metrics are
    // disabled.
    std::optional<fs_metrics::CompositeLatencyEvent> event_or_;
  };

  // Creates new journal metrics for a journal that has |capacity| number of
  // blocks and starts the journal at |start_block|.
  JournalMetrics(std::shared_ptr<MetricsTrait> root, uint64_t capacity, uint64_t start_block)
      : root_(root) {
    if (IsInspectEnabled()) {
      capacity_ = GetInspectRoot()->CreateUint("capacity", capacity);
      start_block_ = GetInspectRoot()->CreateUint("start_block", start_block);
    }
  }

  LatencyEvent NewLatencyEvent(fs_metrics::Event event) {
    if (!Enabled()) {
      return LatencyEvent(std::nullopt);
    }
    return LatencyEvent(root_->NewLatencyEvent(event));
  }

 private:
  inspect::Node* GetInspectRoot() const {
    return root_ == nullptr ? nullptr : root_->GetInspectRoot();
  }

  cobalt_client::Collector* GetCollector() const {
    if (root_ == nullptr)
      return nullptr;
    return root_ == nullptr ? nullptr : root_->GetCollector();
  }

  // Returns true if both(cobalt and inspect) metrics are enabled.
  bool Enabled() const { return IsCobaltEnabled() && IsInspectEnabled(); }

  // Returns true if inspect metrics are enabled.
  bool IsInspectEnabled() const { return GetInspectRoot() != nullptr; }

  // Returns true if cobalt metrics are enabled.
  bool IsCobaltEnabled() const { return GetCollector() != nullptr; }

  // Filesystem's metrics.
  std::shared_ptr<MetricsTrait> root_ = nullptr;

  // Size of the journal in bytes.
  inspect::UintProperty capacity_;

  // Journal start block.
  inspect::UintProperty start_block_;
};

}  // namespace fs

#endif  // FS_JOURNAL_INTERNAL_METRICS_H_
