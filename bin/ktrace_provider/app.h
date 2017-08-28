// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_SRC_KTRACE_PROVIDER_APP_H_
#define APPS_TRACING_SRC_KTRACE_PROVIDER_APP_H_

#include <trace/observer.h>

#include "application/lib/app/application_context.h"
#include "apps/tracing/src/ktrace_provider/log_importer.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/macros.h"

namespace ktrace_provider {

class App {
 public:
  explicit App(const ftl::CommandLine& command_line);
  ~App();

 private:
  void UpdateState();

  void StartKTrace(uint32_t group_mask);
  void StopKTrace();

  std::unique_ptr<app::ApplicationContext> application_context_;
  trace::TraceObserver trace_observer_;
  LogImporter log_importer_;
  uint32_t current_group_mask_ = 0u;
  trace_context_t* context_ = nullptr;

  FTL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace ktrace_provider

#endif  // APPS_TRACING_SRC_KTRACE_PROVIDER_APP_H_
