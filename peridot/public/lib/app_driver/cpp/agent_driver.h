// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_APP_DRIVER_CPP_AGENT_DRIVER_H_
#define LIB_APP_DRIVER_CPP_AGENT_DRIVER_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/agent/cpp/agent_impl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/lifecycle/cpp/lifecycle_impl.h>
#include <lib/sys/cpp/component_context.h>
#include <src/lib/fxl/logging.h>

#include <memory>

namespace modular {

// This interface is passed to the Impl object that AgentDriver initializes.
class AgentHost {
 public:
  virtual sys::ComponentContext* component_context() = 0;
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
//                fit::function<void()> done) { done(); }
//
//   // Called by AgentDriver.
//   void Terminate(fit::function<void()> done) { done(); }
// };
//
// int main(int argc, const char** argv) {
//   async::Loop loop(&kAsyncLoopConfigAttachToThread);
//   auto context = sys::ComponentContext::Create();
//   modular::AgentDriver<HelloAgent> driver(context.get(),
//                                               [&loop] { loop.Quit(); });
//   loop.Run();
//   return 0;
// }
template <typename Impl>
class AgentDriver : LifecycleImpl::Delegate, AgentImpl::Delegate, AgentHost {
 public:
  AgentDriver(sys::ComponentContext* const context,
              fit::function<void()> on_terminated)
      : context_(context),
        lifecycle_impl_(context->outgoing(), this),
        agent_impl_(std::make_unique<AgentImpl>(
            context->outgoing(), static_cast<AgentImpl::Delegate*>(this))),
        on_terminated_(std::move(on_terminated)),
        agent_context_(
            context_->svc()->Connect<fuchsia::modular::AgentContext>()),
        impl_(std::make_unique<Impl>(static_cast<AgentHost*>(this))) {}

  virtual ~AgentDriver() = default;

 private:
  // |AgentHost|
  sys::ComponentContext* component_context() override { return context_; }

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
               fit::function<void()> done) override {
    impl_->RunTask(task_id, std::move(done));
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

  sys::ComponentContext* const context_;
  LifecycleImpl lifecycle_impl_;
  std::unique_ptr<AgentImpl> agent_impl_;
  fit::function<void()> on_terminated_;
  fuchsia::modular::AgentContextPtr agent_context_;
  std::unique_ptr<Impl> impl_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AgentDriver);
};

}  // namespace modular

#endif  // LIB_APP_DRIVER_CPP_AGENT_DRIVER_H_
