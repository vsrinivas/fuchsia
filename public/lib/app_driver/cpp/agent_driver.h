// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_APP_DRIVER_CPP_AGENT_DRIVER_H_
#define LIB_APP_DRIVER_CPP_AGENT_DRIVER_H_

#include <memory>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/agent/cpp/agent_impl.h>
#include <lib/app/cpp/startup_context.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fxl/logging.h>
#include <lib/lifecycle/cpp/lifecycle_impl.h>

namespace modular {

// This interface is passed to the Impl object that AgentDriver initializes.
class AgentHost {
 public:
  virtual fuchsia::sys::StartupContext* startup_context() = 0;
  virtual fuchsia::modular::AgentContext* agent_context() = 0;
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
//   void RunTask(const fidl::StringPtr& task_id,
//                const std::function<void()>& done) { done(); }
//
//   // Called by AgentDriver.
//   void Terminate(const std::function<void()>& done) { done(); }
// };
//
// int main(int argc, const char** argv) {
//   async::Loop loop(&kAsyncLoopConfigMakeDefault);
//   auto context = fuchsia::sys::StartupContext::CreateFromStartupInfo();
//   modular::AgentDriver<HelloAgent> driver(context.get(),
//                                               [&loop] { loop.Quit(); });
//   loop.Run();
//   return 0;
// }
template <typename Impl>
class AgentDriver : LifecycleImpl::Delegate, AgentImpl::Delegate, AgentHost {
 public:
  AgentDriver(fuchsia::sys::StartupContext* const context,
              std::function<void()> on_terminated)
      : context_(context),
        lifecycle_impl_(context->outgoing().deprecated_services(), this),
        agent_impl_(std::make_unique<AgentImpl>(
            context->outgoing().deprecated_services(),
            static_cast<AgentImpl::Delegate*>(this))),
        on_terminated_(std::move(on_terminated)),
        agent_context_(context_->ConnectToEnvironmentService<
                       fuchsia::modular::AgentContext>()),
        impl_(std::make_unique<Impl>(static_cast<AgentHost*>(this))) {}

 private:
  // |AgentHost|
  fuchsia::sys::StartupContext* startup_context() override { return context_; }

  // |AgentHost|
  fuchsia::modular::AgentContext* agent_context() override {
    FXL_DCHECK(agent_context_);
    return agent_context_.get();
  }

  // |AgentImpl::Delegate|
  void Connect(fidl::InterfaceRequest<fuchsia::sys::ServiceProvider>
                   outgoing_services_request) override {
    impl_->Connect(std::move(outgoing_services_request));
  };
  // |AgentImpl::Delegate|
  void RunTask(const fidl::StringPtr& task_id,
               const std::function<void()>& done) override {
    impl_->RunTask(task_id, done);
  };

  // |LifecycleImpl::Delegate|
  void Terminate() override {
    agent_impl_.reset();
    if (impl_) {
      impl_->Terminate([this] {
        // Cf. AppDriver::Terminate().
        async::PostTask(async_get_default_dispatcher(), [this] {
          impl_.reset();
          on_terminated_();
        });
      });
    } else {
      on_terminated_();
    }
  }

  fuchsia::sys::StartupContext* const context_;
  LifecycleImpl lifecycle_impl_;
  std::unique_ptr<AgentImpl> agent_impl_;
  std::function<void()> on_terminated_;
  fuchsia::modular::AgentContextPtr agent_context_;
  std::unique_ptr<Impl> impl_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AgentDriver);
};

}  // namespace modular

#endif  // LIB_APP_DRIVER_CPP_AGENT_DRIVER_H_
