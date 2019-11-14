// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_CLIENT_CPP_TYPES_INTERNAL_H_
#define COBALT_CLIENT_CPP_TYPES_INTERNAL_H_

#include <fuchsia/cobalt/c/fidl.h>
#include <string.h>
#include <unistd.h>

#include <cstdint>

#include <cobalt-client/cpp/metric-options.h>

namespace cobalt_client {
namespace internal {
// Note: Everything on this namespace is internal, no external users should rely
// on the behaviour of any of these classes.
// A value pair which represents a bucket index and the count for such index.
using HistogramBucket = fuchsia_cobalt_HistogramBucket;

enum class ReleaseStage : fuchsia_cobalt_ReleaseStage {
  kGa = fuchsia_cobalt_ReleaseStage_GA,
  kDogfood = fuchsia_cobalt_ReleaseStage_DOGFOOD,
  kFishfood = fuchsia_cobalt_ReleaseStage_FISHFOOD,
  kDebug = fuchsia_cobalt_ReleaseStage_DEBUG,
};

struct MetricInfo {
  static constexpr uint64_t kMaxEventCodes = fuchsia_cobalt_MAX_EVENT_CODE_COUNT;

  // Allows using a |MetricInfo| as key in ordered containers.
  struct LessThan {
    bool operator()(const MetricInfo& lhs, const MetricInfo& rhs) const {
      if (lhs.component < rhs.component) {
        return true;
      } else if (lhs.component > rhs.component) {
        return false;
      }
      if (lhs.metric_id < rhs.metric_id) {
        return true;
      } else if (lhs.metric_id > rhs.metric_id) {
        return false;
      }
      return lhs.event_codes < rhs.event_codes;
    }
  };

  // Generates |name| from the contents of metric options.
  static MetricInfo From(const MetricOptions& options);

  // Allows comparing two |MetricInfo|, which is a shortcut for checking if
  // all fields are equal.
  bool operator==(const MetricInfo& rhs) const;
  bool operator!=(const MetricInfo& rhs) const;

  // Provides refined metric collection for remote metrics.
  // Warning: |component| is not yet supported in the backend, so it will be ignored.
  std::string component = {};

  // Used by remote metrics to match with the respective unique id for the projects defined
  // metrics in the backend.
  uint32_t metric_id = {};

  // This is the equivalent of the event enums defined in the cobalt configuration, because of
  // this order matters.
  //
  // E.g. Metric{id:1, event_codes:{0,0,0,0,1}}
  //      Metric{id:1, event_codes:{0,0,0,0,2}}
  // Can be seen independently in the cobalt backend, or aggregated together.
  std::array<uint32_t, kMaxEventCodes> event_codes = {};
};

// Interface for Logger class. There is no requirement on what to do with the data
// in the logging buffer, that is up to the implementation.
// The default implementation is |CobaltLogger|.
class Logger {
 public:
  virtual ~Logger() = default;

  // Adds the contents of buckets and the required info to a buffer.
  virtual bool Log(const MetricInfo& remote_info, const HistogramBucket* buckets,
                   size_t num_buckets) = 0;

  // Adds the count and the required info to a buffer.
  virtual bool Log(const MetricInfo& remote_info, int64_t count) = 0;
};

// Flush Interface for the |Collector| to flush.
class FlushInterface {
 public:
  virtual ~FlushInterface() = default;

  // Returns true if the data was added to the logger successfully.
  // Returns false if logger failed to flush the data.
  virtual bool Flush(Logger* logger) = 0;

  // Undo's the effect of the on going flush.
  virtual void UndoFlush() = 0;
};

}  // namespace internal
}  // namespace cobalt_client

#endif  // COBALT_CLIENT_CPP_TYPES_INTERNAL_H_
