// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_CLIENT_CPP_TYPES_INTERNAL_H_
#define COBALT_CLIENT_CPP_TYPES_INTERNAL_H_

#include <fuchsia/cobalt/llcpp/fidl.h>
#include <string.h>
#include <unistd.h>

#include <cstdint>
#include <type_traits>

#include <cobalt-client/cpp/metric_options.h>

namespace cobalt_client {
namespace internal {
// Note: Everything on this namespace is internal, no external users should rely
// on the behaviour of any of these classes.
// A value pair which represents a bucket index and the count for such index.
using HistogramBucket = ::llcpp::fuchsia::cobalt::HistogramBucket;

static_assert(::llcpp::fuchsia::cobalt::MAX_EVENT_CODE_COUNT == MetricOptions::kMaxEventCodes);

// Interface for Logger class. There is no requirement on what to do with the data
// in the logging buffer, that is up to the implementation.
// The default implementation is |CobaltLogger|.
class Logger {
 public:
  virtual ~Logger() = default;

  // Adds the contents of buckets and the required info to a buffer.
  virtual bool Log(const MetricOptions& remote_info, const HistogramBucket* buckets,
                   size_t num_buckets) = 0;

  // Adds the count and the required info to a buffer.
  virtual bool Log(const MetricOptions& remote_info, int64_t count) = 0;
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
