// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_SRC_TRACE_TRACE_APP_H_
#define APPS_TRACING_SRC_TRACE_TRACE_APP_H_

#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/services/application/application_controller.fidl.h"
#include "apps/tracing/services/trace_manager.fidl.h"
#include "apps/tracing/src/trace/configuration.h"
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace tracing {

class TraceApp : public mtl::MessageLoopHandler {
 public:
  explicit TraceApp(Configuration configuration);

 private:
  // |MessageLoopHandler| implementation.
  void OnHandleReady(mx_handle_t handle, mx_signals_t pending) override;
  void OnHandleError(mx_handle_t handle, mx_status_t error) override;

  void ExportTrace();
  void LaunchApp(modular::ApplicationLaunchInfoPtr info);

  std::unique_ptr<modular::ApplicationContext> context_;
  modular::ApplicationControllerPtr application_controller_;
  modular::ServiceProviderPtr application_services_;
  TraceControllerPtr trace_controller_;
  std::vector<char> buffer_;
  mx::socket socket_;
  files::ScopedTempDir temp_dir_;
  std::string output_file_name_;
  std::string buffer_file_name_;
  std::ofstream buffer_file_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TraceApp);
};

}  // namespace tracing

#endif  // APPS_TRACING_SRC_TRACE_TRACE_APP_H_
