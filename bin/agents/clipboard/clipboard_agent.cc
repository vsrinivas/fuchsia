// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app_driver/cpp/agent_driver.h"
#include "peridot/bin/agents/clipboard/clipboard_impl.h"
#include "peridot/bin/agents/clipboard/clipboard_storage.h"
#include "peridot/lib/ledger_client/ledger_client.h"

namespace modular {

// An agent responsible for providing the Clipboard service.
class ClipboardAgent {
 public:
  ClipboardAgent(AgentHost* const agent_host) {
    ComponentContextPtr component_context;
    agent_host->agent_context()->GetComponentContext(
        component_context.NewRequest());

    ledger::LedgerPtr ledger;
    component_context->GetLedger(
        ledger.NewRequest(), [](ledger::Status status) {
          if (status != ledger::Status::OK) {
            FXL_LOG(ERROR) << "Could not connect to Ledger.";
          }
        });
    ledger.set_error_handler(
        [] { FXL_LOG(ERROR) << "Ledger connection died."; });

    ledger_client_.reset(new LedgerClient(std::move(ledger)));

    clipboard_.reset(new ClipboardImpl(ledger_client_.get()));

    services_.AddService<Clipboard>(
        [this](fidl::InterfaceRequest<Clipboard> request) {
          clipboard_->Connect(std::move(request));
        });
  }

  void Connect(fidl::InterfaceRequest<app::ServiceProvider> outgoing_services) {
    services_.AddBinding(std::move(outgoing_services));
  }

  void RunTask(const fidl::String& task_id, const std::function<void()>& done) {
    done();
  }

  void Terminate(const std::function<void()>& done) { done(); }

 private:
  // The ledger client that is provided to the ClipboardImpl.
  std::unique_ptr<LedgerClient> ledger_client_;

  std::unique_ptr<ClipboardImpl> clipboard_;

  // The service namespace that the Clipboard is added to.
  app::ServiceNamespace services_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ClipboardAgent);
};

}  // namespace modular

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  auto app_context = app::ApplicationContext::CreateFromStartupInfo();
  modular::AgentDriver<modular::ClipboardAgent> driver(
      app_context.get(), [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
