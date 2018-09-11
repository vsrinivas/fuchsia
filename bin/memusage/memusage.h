// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEMUSAGE_APP_H_
#define GARNET_BIN_MEMUSAGE_APP_H_

#include <memory>

#include <lib/async/dispatcher.h>
#include <lib/zx/vmo.h>
#include <trace/observer.h>
#include <zircon/types.h>

#include "lib/component/cpp/startup_context.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/macros.h"

namespace memusage {

class App {
 public:
  explicit App(const fxl::CommandLine& command_line,
               async_dispatcher_t* dispatcher);
  ~App();

 private:
  void UpdateState();

  void StartTracing();
  void StopTracing();

  void SampleAndPost();
  void PrintHelp();

  uint64_t prealloc_size_;
  zx::vmo prealloc_vmo_;
  bool logging_;
  bool tracing_;
  zx::duration delay_;
  zx_handle_t root_;
  std::unique_ptr<component::StartupContext> startup_context_;
  trace::TraceObserver trace_observer_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace memusage

#endif  // GARNET_BIN_MEMUSAGE_APP_H_
