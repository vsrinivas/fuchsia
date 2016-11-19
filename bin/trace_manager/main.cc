// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "apps/modular/lib/app/application_context.h"
#include "apps/tracing/src/trace_manager/trace_manager.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/log_settings.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace tracing {

class TraceManagerApp {
 public:
  TraceManagerApp()
      : context_(modular::ApplicationContext::CreateFromStartupInfo()) {
    context_->outgoing_services()->AddService<TraceRegistry>([this](
        fidl::InterfaceRequest<TraceRegistry> request) {
      trace_registry_bindings_.AddBinding(&trace_manager_, std::move(request));
    });

    context_->outgoing_services()->AddService<TraceController>(
        [this](fidl::InterfaceRequest<TraceController> request) {
          trace_controller_bindings_.AddBinding(&trace_manager_,
                                                std::move(request));
        });
  }

 private:
  std::unique_ptr<modular::ApplicationContext> context_;
  TraceManager trace_manager_;
  fidl::BindingSet<TraceRegistry> trace_registry_bindings_;
  fidl::BindingSet<TraceController> trace_controller_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TraceManagerApp);
};

}  // namespace tracing

int main(int argc, char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  if (!ftl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  mtl::MessageLoop loop;
  tracing::TraceManagerApp trace_manager_app;
  loop.Run();
  return 0;
}
