// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_SRC_KTRACE_PROVIDER_LOG_IMPORTER_H_
#define APPS_TRACING_SRC_KTRACE_PROVIDER_LOG_IMPORTER_H_

#include <atomic>
#include <limits>
#include <thread>

#include "apps/tracing/lib/trace/ticks.h"
#include "lib/ftl/macros.h"

namespace ktrace_provider {

class LogImporter {
 public:
  LogImporter();
  ~LogImporter();

  void Start();
  void Stop();

  bool is_running() const { return worker_.joinable(); }

 private:
  std::atomic<tracing::Ticks> stop_timestamp_{
      std::numeric_limits<tracing::Ticks>::max()};
  std::thread worker_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LogImporter);
};

}  // namesapce ktrace_provider

#endif  // APPS_TRACING_SRC_KTRACE_PROVIDER_LOG_IMPORTER_H_
