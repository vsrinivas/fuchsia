// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_KTRACE_PROVIDER_APP_H_
#define GARNET_BIN_KTRACE_PROVIDER_APP_H_

#include <trace/observer.h>

#include "garnet/bin/ktrace_provider/log_importer.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/macros.h"

namespace ktrace_provider {

class App {
 public:
  explicit App(const fxl::CommandLine& command_line);
  ~App();

 private:
  void UpdateState();

  void StartKTrace(uint32_t group_mask);
  void StopKTrace();

  std::unique_ptr<component::StartupContext> startup_context_;
  trace::TraceObserver trace_observer_;
  LogImporter log_importer_;
  uint32_t current_group_mask_ = 0u;
  // This context keeps the trace context alive until we've written our trace
  // records, which doesn't happen until after tracing has stopped.
  trace_prolonged_context_t* context_ = nullptr;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace ktrace_provider

#endif  // GARNET_BIN_KTRACE_PROVIDER_APP_H_
