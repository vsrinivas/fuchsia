// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/action_log/action_log_impl.h"

namespace {

using namespace fuchsia::modular;
using namespace modular;

class UserActionLogFactoryImpl : public fuchsia::modular::UserActionLogFactory {
 public:
  UserActionLogFactoryImpl() {}

 private:
  void GetUserActionLog(
      fidl::InterfaceHandle<fuchsia::modular::ProposalPublisher>
          proposal_publisher_handle,
      fidl::InterfaceRequest<fuchsia::modular::UserActionLog> request) {
    fuchsia::modular::ProposalPublisherPtr proposal_publisher =
        proposal_publisher_handle.Bind();
    std::unique_ptr<UserActionLogImpl> user_action_log_impl(
        new UserActionLogImpl(std::move(proposal_publisher)));
    user_action_log_bindings_.AddBinding(std::move(user_action_log_impl),
                                         std::move(request));
  }

  fidl::BindingSet<fuchsia::modular::UserActionLog,
                   std::unique_ptr<fuchsia::modular::UserActionLog>>
      user_action_log_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(UserActionLogFactoryImpl);
};

class UserActionLogFactoryApp {
 public:
  UserActionLogFactoryApp()
      : context_(component::StartupContext::CreateFromStartupInfo()) {
    std::unique_ptr<UserActionLogFactoryImpl> factory_impl(
        new UserActionLogFactoryImpl());
    factory_impl_.swap(factory_impl);

    // Singleton service
    context_->outgoing()
        .AddPublicService<fuchsia::modular::UserActionLogFactory>(
            [this](
                fidl::InterfaceRequest<fuchsia::modular::UserActionLogFactory>
                    request) {
              factory_bindings_.AddBinding(factory_impl_.get(),
                                           std::move(request));
            });
  }

 private:
  std::unique_ptr<component::StartupContext> context_;
  std::unique_ptr<UserActionLogFactoryImpl> factory_impl_;
  fidl::BindingSet<fuchsia::modular::UserActionLogFactory> factory_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(UserActionLogFactoryApp);
};

}  // namespace

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  UserActionLogFactoryApp app;
  loop.Run();
  return 0;
}
