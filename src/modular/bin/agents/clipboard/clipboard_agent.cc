// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/function.h>
#include <lib/modular/cpp/agent.h>
#include <lib/sys/cpp/component_context.h>

#include "peridot/lib/ledger_client/ledger_client.h"
#include "src/modular/bin/agents/clipboard/clipboard_impl.h"
#include "src/modular/bin/agents/clipboard/clipboard_storage.h"

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();

  // 1. Get Ledger.
  auto modular_component_context = context->svc()->Connect<fuchsia::modular::ComponentContext>();
  fuchsia::ledger::LedgerPtr ledger;
  modular_component_context->GetLedger(ledger.NewRequest());

  modular::LedgerClient ledger_client(std::move(ledger), /* on_error */ [](zx_status_t status) {
    FXL_LOG(ERROR) << "Ledger connection died: " << status;
  });

  // 2. Setup the clipboard impl (give it the ledger).
  modular::ClipboardImpl clipboard_impl(&ledger_client);
  fidl::BindingSet<fuchsia::modular::Clipboard> clipboard_bindings;

  // 3. Publish the Clipboard service (both as an agent, and as a normal component)
  context->outgoing()->AddPublicService<fuchsia::modular::Clipboard>(
      clipboard_bindings.GetHandler(&clipboard_impl));

  modular::Agent clipboard_agent(context->outgoing(), [&loop] { loop.Quit(); });
  clipboard_agent.AddService<fuchsia::modular::Clipboard>(
      clipboard_bindings.GetHandler(&clipboard_impl));

  loop.Run();
  return 0;
}
