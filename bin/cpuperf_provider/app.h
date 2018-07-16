// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CPUPERF_PROVIDER_APP_H_
#define GARNET_BIN_CPUPERF_PROVIDER_APP_H_

#include <trace/observer.h>

#include <memory>

#include "garnet/bin/cpuperf_provider/categories.h"
#include "garnet/lib/cpuperf/controller.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/macros.h"

namespace cpuperf_provider {

class App {
 public:
  explicit App(const fxl::CommandLine& command_line);
  ~App();

 private:
  void UpdateState();

  void StartTracing(const TraceConfig& trace_config);
  void StopTracing();

  void PrintHelp();

  // This is per-cpu, in megabytes.
  static constexpr uint32_t kDefaultBufferSizeInMb = 16;
  // This is the max value cpu-trace will accept
  static constexpr uint32_t kMaxBufferSizeInMb = 256;

  std::unique_ptr<component::StartupContext> startup_context_;
  trace::TraceObserver trace_observer_;
  TraceConfig trace_config_;
  trace_context_t* context_ = nullptr;
  std::unique_ptr<cpuperf::Controller> controller_;

  trace_ticks_t start_time_ = 0;
  trace_ticks_t stop_time_ = 0;

  uint32_t buffer_size_in_mb_ = kDefaultBufferSizeInMb;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace cpuperf_provider

#endif  // GARNET_BIN_CPUPERF_PROVIDER_APP_H_
