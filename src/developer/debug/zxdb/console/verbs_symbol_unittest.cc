// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/console/mock_console.h"
#include "src/developer/debug/zxdb/symbols/loaded_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"

namespace zxdb {

namespace {

class VerbsSymbolTest : public RemoteAPITest {};

}  // namespace

TEST_F(VerbsSymbolTest, SymStat) {
  MockConsole console(&session());

  console.ProcessInputLine("attach 1234");

  auto event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ("Attached Process 1 [Running] koid=1234 <mock>", event.output.AsString());

  auto target = console.context().GetActiveTarget();
  ASSERT_NE(nullptr, target);
  ASSERT_NE(nullptr, target->GetProcess());

  target->GetProcess()->GetSymbols()->InjectModuleForTesting(
      "fakelib", "abc123", std::make_unique<LoadedModuleSymbols>(nullptr, "abc123", 0));

  auto download = session().system().InjectDownloadForTesting("abc123");

  console.ProcessInputLine("sym-stat");

  event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);

  auto text = event.output.AsString();
  EXPECT_NE(text.find("Process 1 symbol status"), std::string::npos);
  EXPECT_NE(text.find("Build ID: abc123 (Downloading...)"), std::string::npos);

  event = console.GetOutputEvent();
  EXPECT_EQ("Downloading symbols...", event.output.AsString());

  // Releasing the download will cause it to register a failure.
  download = nullptr;

  event = console.GetOutputEvent();
  EXPECT_EQ("Process 1 [Running] koid=1234 <mock>", event.output.AsString());

  event = console.GetOutputEvent();
  EXPECT_EQ("Symbol downloading complete. 0 succeeded, 1 failed.", event.output.AsString());
}

// sym-stat will demangle symbols. It doesn't need any context to do this.
TEST_F(VerbsSymbolTest, SymInfo_Demangle) {
  MockConsole console(&session());

  // Should demangle if given mangled input.
  console.ProcessInputLine("sym-info _ZN3fxl10LogMessage6streamEv");
  auto event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ(
      "Demangled name: fxl::LogMessage::stream()\n\n"
      "No symbol \"_ZN3fxl10LogMessage6streamEv\" found in the current context.\n",
      event.output.AsString());

  // When input is not mangled it shouldn't show any demangled thing.
  console.ProcessInputLine("sym-info LogMessage6streamEv");
  event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ(
      "No symbol \"LogMessage6streamEv\" found in the current context.\n",
      event.output.AsString());
}

}  // namespace zxdb
