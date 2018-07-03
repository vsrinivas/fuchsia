// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/app_driver/cpp/agent_driver.h>
#include <lib/async-loop/cpp/loop.h>

#include "peridot/examples/simple/simple_impl.h"

using ::fuchsia::modular::examples::simple::Simple;

namespace simple {

class SimpleAgent {
 public:
  SimpleAgent(modular::AgentHost* const agent_host)
      : simple_impl_(new SimpleImpl) {
    services_.AddService<Simple>(
        [this](fidl::InterfaceRequest<Simple> request) {
          simple_impl_->Connect(std::move(request));
        });
  }

  // Called by |AgentDriver| to expose the agent's outgoing services.
  void Connect(
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> outgoing_services) {
    services_.AddBinding(std::move(outgoing_services));
  }

  // Called by |AgentDriver| to perform the task with |task_id|.
  void RunTask(const fidl::StringPtr& task_id,
               const std::function<void()>& done) {
    done();
  }

  // Called by |AgentDriver| when the agent is to terminate.
  void Terminate(const std::function<void()>& done) { done(); }

 private:
  // The services namespace that the `Simple` service is added to.
  fuchsia::sys::ServiceNamespace services_;

  // The implementation of the Simple service.
  std::unique_ptr<SimpleImpl> simple_impl_;
};

}  // namespace simple

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  auto context = fuchsia::sys::StartupContext::CreateFromStartupInfo();
  modular::AgentDriver<simple::SimpleAgent> driver(context.get(),
                                                   [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
