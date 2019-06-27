// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/component/cpp/connect.h>
#include <lib/modular/testing/cpp/test_harness_builder.h>
#include <lib/modular_test_harness/cpp/test_harness_fixture.h>

#include "peridot/bin/agents/clipboard/clipboard_impl.h"
#include "peridot/bin/basemgr/noop_clipboard_impl.h"
#include "peridot/lib/testing/ledger_repository_for_testing.h"

namespace {

class ClipboardTest : public modular::testing::TestHarnessFixture {};

// Ensure that a Clipboard can successfully push and peek its contents.
TEST_F(ClipboardTest, PushAndPeekToTheSameClipboard) {
  std::unique_ptr<modular::testing::LedgerRepositoryForTesting> ledger_app_ =
      std::make_unique<modular::testing::LedgerRepositoryForTesting>();
  std::unique_ptr<modular::LedgerClient> ledger_client_ = std::make_unique<modular::LedgerClient>(
      ledger_app_->ledger_repository(), __FILE__,
      [](zx_status_t status) { ASSERT_TRUE(false) << "Status: " << status; });

  modular::ClipboardImpl clipboard_impl(ledger_client_.get());
  fidl::BindingSet<fuchsia::modular::Clipboard> clipboard_bindings;

  fuchsia::modular::ClipboardPtr clipboard_ptr;
  clipboard_impl.Connect(clipboard_ptr.NewRequest());

  std::string expected_value = "pushed to the clipboard";
  bool clipboard_peek_success = false;

  // Expect nothing in the initialized clipboard
  clipboard_ptr->Peek([&](std::string text) { EXPECT_EQ(text, ""); });
  clipboard_ptr->Push(expected_value);
  clipboard_ptr->Peek([&](std::string text) {
    clipboard_peek_success = true;
    EXPECT_EQ(text, expected_value);
  });
  RunLoopUntil([&] { return clipboard_peek_success; });
}

// Ensure that a NoopClipboardImpl doesn't push anything.
TEST_F(ClipboardTest, NoopClipboard) {
  modular::NoopClipboardImpl clipboard_impl;
  fidl::BindingSet<fuchsia::modular::Clipboard> clipboard_bindings;

  fuchsia::modular::ClipboardPtr clipboard_ptr;
  clipboard_impl.Connect(clipboard_ptr.NewRequest());

  clipboard_ptr->Push("noop");
  bool clipboard_peek_success = false;
  clipboard_ptr->Peek([&](std::string text) {
    clipboard_peek_success = true;
    EXPECT_EQ(text, "");
  });
  RunLoopUntil([&] { return clipboard_peek_success; });
}

// Ensure that a module can use ConnectToAgent to aquire a clipboard
TEST_F(ClipboardTest, ClipboardAgentProvidesClipboard) {
  constexpr char kClipboardAgentUrl[] =
      "fuchsia-pkg://fuchsia.com/clipboard_agent#meta/clipboard_agent.cmx";

  modular::testing::TestHarnessBuilder builder;
  builder.BuildAndRun(test_harness());

  fuchsia::modular::testing::ModularService svc;
  fuchsia::modular::ComponentContextPtr component_context;
  svc.set_component_context(component_context.NewRequest());

  test_harness()->ConnectToModularService(std::move(svc));

  fuchsia::modular::AgentControllerPtr agent_controller_;
  fuchsia::modular::ClipboardPtr clipboard_ptr;
  fuchsia::sys::ServiceProviderPtr agent_services;
  component_context->ConnectToAgent(kClipboardAgentUrl, agent_services.NewRequest(),
                                    agent_controller_.NewRequest());
  component::ConnectToService(agent_services.get(), clipboard_ptr.NewRequest());

  bool clipboard_peek_success = false;
  std::string expected_value = "ahoy matey";

  clipboard_ptr->Push(expected_value);

  clipboard_ptr->Peek([&](std::string text) {
    clipboard_peek_success = true;
    EXPECT_EQ(text, expected_value);
  });

  RunLoopUntil([&] { return clipboard_peek_success; });
}

}  // namespace
