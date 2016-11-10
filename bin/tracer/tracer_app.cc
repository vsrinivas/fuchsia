// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>
#include <iostream>
#include <utility>

#include "apps/modular/lib/app/application_context.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/split_string.h"
#include "lib/mtl/data_pipe/data_pipe_drainer.h"
#include "lib/mtl/tasks/message_loop.h"
#include "apps/tracing/services/trace_manager.fidl.h"

namespace tracing {

class TracerApp {
 public:
  TracerApp()
      : context_(modular::ApplicationContext::CreateFromStartupInfo()),
        trace_controller_(
            context_->ConnectToEnvironmentService<tracing::TraceController>()) {
  }

 private:
  std::unique_ptr<modular::ApplicationContext> context_;
  TraceControllerPtr trace_controller_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TracerApp);
};

}  // namespace tracing

int main(int argc, char** argv) {
  mtl::MessageLoop loop;
  tracing::TracerApp tracer_app;
  loop.Run();
  return 0;
}
