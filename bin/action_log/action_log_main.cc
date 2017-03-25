// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "apps/maxwell/services/action_log/action_log.fidl.h"
#include "apps/maxwell/src/action_log/action_log_impl.h"

#include "application/lib/app/application_context.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

using namespace maxwell;

class ActionLogFactoryApp {
 public:
  ActionLogFactoryApp()
    : context_(app::ApplicationContext::CreateFromStartupInfo()) {
    std::unique_ptr<ActionLogFactoryImpl> factory_impl;
    factory_impl_.swap(factory_impl);

    // Singleton service
    context_->outgoing_services()->AddService<ActionLogFactory>([this](
        fidl::InterfaceRequest<ActionLogFactory> request) {
      factory_bindings_.AddBinding(factory_impl_.get(), std::move(request));
    });
  }

 private:
  std::unique_ptr<app::ApplicationContext> context_;
  std::unique_ptr<ActionLogFactoryImpl> factory_impl_;
  fidl::BindingSet<ActionLogFactory> factory_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ActionLogFactoryApp);
};

} // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  ActionLogFactoryApp app;
  loop.Run();
  return 0;
}
