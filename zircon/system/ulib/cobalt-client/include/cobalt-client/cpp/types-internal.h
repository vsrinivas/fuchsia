// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_CLIENT_CPP_TYPES_INTERNAL_H_
#define COBALT_CLIENT_CPP_TYPES_INTERNAL_H_

#include <fuchsia/cobalt/c/fidl.h>
#include <stdint.h>
#include <unistd.h>

#include <cobalt-client/cpp/metric-options.h>
#include <fbl/string.h>
#include <fbl/vector.h>

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

struct RemoteMetricInfo {
  // Generates |name| from the contents of metric options.
  static RemoteMetricInfo From(const MetricOptions& options);

  RemoteMetricInfo() = default;
  RemoteMetricInfo(const RemoteMetricInfo&) = default;

  // Allows comparing two |RemoteMetricInfo|, which is a shortcut for checking if
  // all fields are equal.
  bool operator==(const RemoteMetricInfo& rhs) const;
  bool operator!=(const RemoteMetricInfo& rhs) const;

  // Provides refined metric collection for remote metrics.
  // Warning: |component| is not yet supported in the backend, so it will be ignored.
  fbl::String component;

  // Used by remote metrics to match with the respective unique id for the projects defined
  // metrics in the backend.
  uint32_t metric_id;

  // Provides refined metric collection for remote metrics.
  // Warning: |event_code| is not yet supported in the backend, so it will be treated as 0.
  uint32_t event_code;
};

struct LocalMetricInfo {
  // Generates |name| from the contents of metric options.
  static LocalMetricInfo From(const MetricOptions& options);

  LocalMetricInfo() = default;
  LocalMetricInfo(const LocalMetricInfo&) = default;
  bool operator==(const LocalMetricInfo& rhs) const;
  bool operator!=(const LocalMetricInfo& rhs) const;

  fbl::String name;
};

// Interface for Logger class. There is no requirement on what to do with the data
// in the logging buffer, that is up to the implementation.
// The default implementation is |CobaltLogger|.
class Logger {
 public:
  virtual ~Logger() = default;

  // Adds the contents of buckets and the required info to a buffer.
  virtual bool Log(const RemoteMetricInfo& remote_info, const HistogramBucket* buckets,
                   size_t num_buckets) = 0;

  // Adds the count and the required info to a buffer.
  virtual bool Log(const RemoteMetricInfo& remote_info, int64_t count) = 0;
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
