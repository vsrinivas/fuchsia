// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#ifndef ZIRCON_SYSTEM_ULIB_COBALT_CLIENT_TEST_FAKE_LOGGER_H_
#define ZIRCON_SYSTEM_ULIB_COBALT_CLIENT_TEST_FAKE_LOGGER_H_

#include <cobalt-client/cpp/types-internal.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/vector.h>

namespace cobalt_client {
namespace internal {

class FakeLogger : public Logger {
 public:
  struct HistEntry {
    MetricOptions metric_info;
    fbl::Vector<HistogramBucket> buckets;
  };

  struct CountEntry {
    MetricOptions metric_info;
    int64_t count;
  };

  FakeLogger() = default;

  bool Log(const MetricOptions& metric_info, const HistogramBucket* bucket, size_t num_buckets) {
    if (!should_fail_) {
      size_t index = logged_histograms_.size();
      logged_histograms_.push_back({metric_info, {}});
      for (size_t i = 0; i < num_buckets; i++) {
        logged_histograms_[index].buckets.push_back(bucket[i]);
      }
    }
    return !should_fail_;
  }

  bool Log(const MetricOptions& metric_info, int64_t count) {
    if (!should_fail_) {
      logged_counts_.push_back({metric_info, count});
    }
    return !should_fail_;
  }

  const fbl::Vector<HistEntry>& logged_histograms() const { return logged_histograms_; }

  const fbl::Vector<CountEntry>& logged_counts() const { return logged_counts_; }

  void set_should_fail(bool should_fail) { should_fail_ = should_fail; }

  const fbl::Vector<HistogramBucket> GetHistogram(const MetricOptions& info) const {
    fbl::Vector<HistogramBucket> histogram;
    for (auto& hist_entry : logged_histograms_) {
      if (info == hist_entry.metric_info) {
        if (histogram.is_empty()) {
          histogram.reserve(hist_entry.buckets.size());
          for (size_t i = 0; i < hist_entry.buckets.size(); ++i) {
            histogram.push_back({.index = static_cast<uint32_t>(i), .count = 0});
          }
        }
        for (auto& bucket : hist_entry.buckets) {
          histogram[bucket.index].count += bucket.count;
        }
      }
    }
    return histogram;
  }

  RemoteCounter::Type GetCounter(const MetricOptions& info) const {
    RemoteCounter::Type count = 0;
    for (auto& count_entry : logged_counts_) {
      if (info == count_entry.metric_info) {
        count += count_entry.count;
      }
    }
    return count;
  }

 private:
  bool should_fail_ = false;

  fbl::Vector<HistEntry> logged_histograms_;
  fbl::Vector<CountEntry> logged_counts_;
};

}  // namespace internal
}  // namespace cobalt_client

#endif  // ZIRCON_SYSTEM_ULIB_COBALT_CLIENT_TEST_FAKE_LOGGER_H_
