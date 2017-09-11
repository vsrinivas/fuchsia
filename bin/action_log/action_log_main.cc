// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "apps/maxwell/services/action_log/factory.fidl.h"
#include "apps/maxwell/services/action_log/user.fidl.h"
#include "apps/maxwell/src/action_log/action_log_impl.h"

#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

using namespace maxwell;

class UserActionLogFactoryImpl : public UserActionLogFactory {
 public:
  UserActionLogFactoryImpl() {}

 private:
  void GetUserActionLog(
      fidl::InterfaceHandle<ProposalPublisher> proposal_publisher_handle,
      fidl::InterfaceRequest<UserActionLog> request) {
    ProposalPublisherPtr proposal_publisher =
        ProposalPublisherPtr::Create(std::move(proposal_publisher_handle));
    std::unique_ptr<UserActionLogImpl> user_action_log_impl(
        new UserActionLogImpl(std::move(proposal_publisher)));
    user_action_log_bindings_.AddBinding(std::move(user_action_log_impl),
                                         std::move(request));
  }

  fidl::BindingSet<UserActionLog, std::unique_ptr<UserActionLog>>
      user_action_log_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(UserActionLogFactoryImpl);
};

class UserActionLogFactoryApp {
 public:
  UserActionLogFactoryApp()
      : context_(app::ApplicationContext::CreateFromStartupInfo()) {
    std::unique_ptr<UserActionLogFactoryImpl> factory_impl(
        new UserActionLogFactoryImpl());
    factory_impl_.swap(factory_impl);

    // Singleton service
    context_->outgoing_services()->AddService<UserActionLogFactory>(
        [this](fidl::InterfaceRequest<UserActionLogFactory> request) {
          factory_bindings_.AddBinding(factory_impl_.get(), std::move(request));
        });
  }

 private:
  std::unique_ptr<app::ApplicationContext> context_;
  std::unique_ptr<UserActionLogFactoryImpl> factory_impl_;
  fidl::BindingSet<UserActionLogFactory> factory_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(UserActionLogFactoryApp);
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  UserActionLogFactoryApp app;
  loop.Run();
  return 0;
}
