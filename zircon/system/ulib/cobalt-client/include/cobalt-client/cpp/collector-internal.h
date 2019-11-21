// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_CLIENT_CPP_COLLECTOR_INTERNAL_H_
#define COBALT_CLIENT_CPP_COLLECTOR_INTERNAL_H_

#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <limits.h>
#include <zircon/types.h>

#include <cstdint>
#include <string>
#include <string_view>

#include <cobalt-client/cpp/types-internal.h>

namespace cobalt_client {
namespace internal {

struct CobaltOptions {
  // Service path to LoggerFactory interface.
  std::string service_path;

  // Performs a connection to a service at a given path.
  fit::function<zx_status_t(const char* service_path, zx::channel service)> service_connect =
      nullptr;

  // Used to acquire a logger instance.
  std::string project_name;

  // Which release stage to use for persisting metrics.
  ReleaseStage release_stage;
};

// Logger implementation that pushes data to cobalt.
class CobaltLogger final : public Logger {
 public:
  static std::string_view GetServiceName();

  CobaltLogger() = delete;
  // instance from cobalt service;
  explicit CobaltLogger(CobaltOptions options) : options_(std::move(options)) { logger_.reset(); }
  CobaltLogger(const CobaltLogger&) = delete;
  CobaltLogger(CobaltLogger&&) = delete;
  CobaltLogger& operator=(const CobaltLogger&) = delete;
  CobaltLogger& operator=(CobaltLogger&&) = delete;
  ~CobaltLogger() final = default;

  // Returns true if the histogram was persisted.
  bool Log(const MetricOptions& metric_info, const HistogramBucket* buckets,
           size_t bucket_count) final;

  // Returns true if the counter was persisted.
  bool Log(const MetricOptions& metric_info, int64_t count) final;

 protected:
  // Returns true if |logger_| is able to  provide a logging service instance for |options_|.
  bool TryObtainLogger();

  // Cleans up the state of the logger.
  void Reset() { logger_.reset(); }

  // Set of options for this logger.
  CobaltOptions options_;

  zx::channel logger_;
};

}  // namespace internal
}  // namespace cobalt_client

#endif  // COBALT_CLIENT_CPP_COLLECTOR_INTERNAL_H_
