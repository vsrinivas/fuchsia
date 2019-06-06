// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/app_driver/cpp/agent_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/function.h>
#include <lib/svc/cpp/service_namespace.h>

#include "peridot/bin/agents/clipboard/clipboard_impl.h"
#include "peridot/bin/agents/clipboard/clipboard_storage.h"
#include "peridot/lib/ledger_client/ledger_client.h"

namespace modular {

// An agent responsible for providing the fuchsia::modular::Clipboard service.
class ClipboardAgent {
 public:
  ClipboardAgent(AgentHost* const agent_host) {
    fuchsia::modular::ComponentContextPtr component_context;
    agent_host->agent_context()->GetComponentContext(
        component_context.NewRequest());

    fuchsia::ledger::LedgerPtr ledger;
    component_context->GetLedger(ledger.NewRequest());

    ledger_client_ = std::make_unique<LedgerClient>(
        std::move(ledger), [](zx_status_t status) {
          FXL_LOG(ERROR) << "Ledger connection died: " << status;
        });

    clipboard_ = std::make_unique<ClipboardImpl>(ledger_client_.get());

    agent_host->component_context()
        ->outgoing()
        ->AddPublicService<fuchsia::modular::Clipboard>(
            [this](
                fidl::InterfaceRequest<fuchsia::modular::Clipboard> request) {
              clipboard_->Connect(std::move(request));
            });

    services_.AddService<fuchsia::modular::Clipboard>(
        [this](fidl::InterfaceRequest<fuchsia::modular::Clipboard> request) {
          clipboard_->Connect(std::move(request));
        });
  }

  void Connect(
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> outgoing_services) {
    services_.AddBinding(std::move(outgoing_services));
  }

  void RunTask(const fidl::StringPtr& task_id, fit::function<void()> done) {
    done();
  }

  void Terminate(fit::function<void()> done) { done(); }

 private:
  // The ledger client that is provided to the ClipboardImpl.
  std::unique_ptr<LedgerClient> ledger_client_;

  std::unique_ptr<ClipboardImpl> clipboard_;

  // The service namespace that the fuchsia::modular::Clipboard is added to.
  component::ServiceNamespace services_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ClipboardAgent);
};

}  // namespace modular

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = sys::ComponentContext::Create();
  modular::AgentDriver<modular::ClipboardAgent> driver(
      context.get(), [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
