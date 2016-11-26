// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_SRC_KTRACE_PROVIDER_APP_H_
#define APPS_TRACING_SRC_KTRACE_PROVIDER_APP_H_

#include "apps/modular/lib/app/application_context.h"
#include "apps/tracing/lib/trace/writer.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/weak_ptr.h"

namespace ktrace_provider {

class App {
 public:
  explicit App(const ftl::CommandLine& command_line);
  ~App();

 private:
  void UpdateState(tracing::writer::TraceState state);

  void RestartTracing();
  void StopTracing();
  bool SendDevMgrCommand(std::string command);

  void CollectTraces();

  std::unique_ptr<modular::ApplicationContext> application_context_;
  tracing::writer::TraceHandlerKey trace_handler_key_;

  bool trace_running_ = false;

  ftl::WeakPtrFactory<App> weak_ptr_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace ktrace_provider

#endif  // APPS_TRACING_SRC_KTRACE_PROVIDER_APP_H_
