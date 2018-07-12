// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_KTRACE_PROVIDER_LOG_IMPORTER_H_
#define GARNET_BIN_KTRACE_PROVIDER_LOG_IMPORTER_H_

#include <lib/async/cpp/wait.h>
#include <lib/zx/log.h>
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
  void Handle(async_dispatcher_t* dispatcher,
              async::WaitBase* wait,
              zx_status_t status,
              const zx_packet_signal_t* signal);

  zx::log log_;
  trace_ticks_t start_ticks_;
  zx_time_t start_time_;
  async::WaitMethod<LogImporter, &LogImporter::Handle> wait_{this};

  FXL_DISALLOW_COPY_AND_ASSIGN(LogImporter);
};

}  // namespace ktrace_provider

#endif  // GARNET_BIN_KTRACE_PROVIDER_LOG_IMPORTER_H_
