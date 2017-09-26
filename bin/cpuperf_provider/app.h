// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CPUPERF_PROVIDER_APP_H_
#define GARNET_BIN_CPUPERF_PROVIDER_APP_H_

#include <trace/observer.h>

#include <memory>

#include "lib/app/cpp/application_context.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/macros.h"

namespace cpuperf_provider {

class App {
public:
  explicit App(const fxl::CommandLine& command_line);
  ~App();

private:
  uint32_t GetCategoryMask();
  void UpdateState();

  void StartTracing(uint32_t category_mask);
  void StopTracing();

  void PrintHelp();

  // This is per-cpu.
  static constexpr uint32_t kDefaultBufferSize = 16 * 1024 * 1024;

  std::unique_ptr<app::ApplicationContext> application_context_;
  trace::TraceObserver trace_observer_;
  uint32_t current_category_mask_ = 0u;
  trace_context_t* context_ = nullptr;

  trace_ticks_t start_time_ = 0;
  trace_ticks_t stop_time_ = 0;

  uint32_t buffer_size_ = kDefaultBufferSize;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace cpuperf_provider

#endif  // GARNET_BIN_CPUPERF_PROVIDER_APP_H_
