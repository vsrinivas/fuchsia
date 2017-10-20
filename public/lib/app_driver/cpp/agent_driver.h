// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_APP_DRIVER_CPP_AGENT_DRIVER_H_
#define LIB_APP_DRIVER_CPP_AGENT_DRIVER_H_

#include <memory>

#include "lib/agent/cpp/agent_impl.h"
#include "lib/agent/fidl/agent.fidl.h"
#include "lib/agent/fidl/agent_context.fidl.h"
#include "lib/app/cpp/application_context.h"
#include "lib/app/fidl/service_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"
#include "lib/lifecycle/cpp/lifecycle_impl.h"

namespace modular {

// This interface is passed to the Impl object that AgentDriver initializes.
class AgentHost {
 public:
  virtual app::ApplicationContext* application_context() = 0;
  virtual AgentContext* agent_context() = 0;
};

// AgentDriver provides a way to write agents and participate in application
// lifecycle.
//
// class HelloAgent {
//  public:
//   HelloAgent(AgentHost* host) {}
//
//   // Called by AgentDriver.
//   void Connect(fidl::InterfaceRequest<ServiceProvider> outgoing_services) {}
//
//   // Called by AgentDriver.
//   void RunTask(const fidl::String& task_id,
//                const std::function<void()>& done) { done(); }
//
//   // Called by AgentDriver.
//   void Terminate(const std::function<void()>& done) { done(); }
// };
//
// int main(int argc, const char** argv) {
//   fsl::MessageLoop loop;
//   auto app_context = app::ApplicationContext::CreateFromStartupInfo();
//   modular::AgentDriver<HelloAgent> driver(app_context.get(),
//                                               [&loop] { loop.QuitNow(); });
//   loop.Run();
//   return 0;
// }
template <typename Impl>
class AgentDriver : LifecycleImpl::Delegate, AgentImpl::Delegate, AgentHost {
 public:
  AgentDriver(app::ApplicationContext* const app_context,
              std::function<void()> on_terminated)
      : app_context_(app_context),
        lifecycle_impl_(app_context->outgoing_services(), this),
        agent_impl_(std::make_unique<AgentImpl>(
            app_context->outgoing_services(),
            static_cast<AgentImpl::Delegate*>(this))),
        on_terminated_(std::move(on_terminated)) {}

 private:
  // |AgentHost|
  app::ApplicationContext* application_context() override {
    return app_context_;
  }

  // |AgentHost|
  AgentContext* agent_context() override {
    FXL_DCHECK(agent_context_);
    return agent_context_.get();
  }

  // |AgentImpl::Delegate|
  void AgentInit(fidl::InterfaceHandle<AgentContext> agent_context) override {
    agent_context_.Bind(std::move(agent_context));
    impl_ = std::make_unique<Impl>(static_cast<AgentHost*>(this));
  }
  // |AgentImpl::Delegate|
  void Connect(fidl::InterfaceRequest<app::ServiceProvider>
                   outgoing_services_request) override {
    impl_->Connect(std::move(outgoing_services_request));
  };
  // |AgentImpl::Delegate|
  void RunTask(const fidl::String& task_id,
               const std::function<void()>& done) override {
    impl_->RunTask(task_id, done);
  };

  // |LifecycleImpl::Delegate|
  void Terminate() override {
    agent_impl_.reset();
    if (impl_) {
      impl_->Terminate([this] {
          // Cf. AppDriver::Terminate().
          fsl::MessageLoop::GetCurrent()->task_runner()->PostTask([this] {
              impl_.reset();
              on_terminated_();
            });
        });
    } else {
      on_terminated_();
    }
  }

  app::ApplicationContext* const app_context_;
  LifecycleImpl lifecycle_impl_;
  std::unique_ptr<AgentImpl> agent_impl_;
  std::function<void()> on_terminated_;
  AgentContextPtr agent_context_;
  std::unique_ptr<Impl> impl_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AgentDriver);
};

}  // namespace modular

#endif  // LIB_APP_DRIVER_CPP_AGENT_DRIVER_H_
