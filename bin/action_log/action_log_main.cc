// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "apps/maxwell/services/action_log/user.fidl.h"
#include "apps/maxwell/src/action_log/action_log_impl.h"

#include "application/lib/app/application_context.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

using namespace maxwell;

class UserActionLogApp {
 public:
  UserActionLogApp()
      : context_(app::ApplicationContext::CreateFromStartupInfo()) {
    ProposalPublisherPtr proposal_publisher =
        context_->ConnectToEnvironmentService<ProposalPublisher>();
    factory_impl_ =
        std::make_unique<UserActionLogImpl>(std::move(proposal_publisher));

    // Singleton service
    context_->outgoing_services()->AddService<UserActionLog>(
        [this](fidl::InterfaceRequest<UserActionLog> request) {
          factory_bindings_.AddBinding(factory_impl_.get(), std::move(request));
        });
  }

 private:
  std::unique_ptr<app::ApplicationContext> context_;
  std::unique_ptr<UserActionLogImpl> factory_impl_;
  fidl::BindingSet<UserActionLog> factory_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(UserActionLogApp);
};

} // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  UserActionLogApp app;
  loop.Run();
  return 0;
}
