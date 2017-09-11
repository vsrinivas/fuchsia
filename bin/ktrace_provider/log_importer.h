// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_SRC_KTRACE_PROVIDER_LOG_IMPORTER_H_
#define APPS_TRACING_SRC_KTRACE_PROVIDER_LOG_IMPORTER_H_

#include <async/wait.h>
#include <mx/log.h>
#include <trace-engine/types.h>

#include "lib/fxl/macros.h"

namespace ktrace_provider {

class LogImporter {
 public:
  LogImporter();
  ~LogImporter();

  void Start();
  void Stop();

 private:
  async_wait_result_t Handle(async_t* async,
                             mx_status_t status,
                             const mx_packet_signal_t* signal);

  mx::log log_;
  trace_ticks_t start_ticks_;
  mx_time_t start_time_;
  async::Wait wait_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LogImporter);
};

}  // namespace ktrace_provider

#endif  // APPS_TRACING_SRC_KTRACE_PROVIDER_LOG_IMPORTER_H_
