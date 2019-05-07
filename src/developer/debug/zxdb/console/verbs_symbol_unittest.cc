// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/console/mock_console.h"
#include "src/developer/debug/zxdb/symbols/loaded_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"

namespace zxdb {

class ConsoleTest : public testing::Test {
 public:
  ConsoleTest() { loop_.Init(); }
  ~ConsoleTest() { loop_.Cleanup(); }

 private:
  debug_ipc::PlatformMessageLoop loop_;
};

TEST_F(ConsoleTest, SymStat) {
  auto remote_api = std::make_unique<MockRemoteAPI>();
  auto session =
      std::make_unique<Session>(std::move(remote_api), debug_ipc::Arch::kX64);
  MockConsole console(session.get());

  console.ProcessInputLine("attach 1234");

  auto event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ(OutputBuffer("Attached Process 1 [Running] koid=1234 <mock>"),
            event.output);

  auto target = console.context().GetActiveTarget();
  ASSERT_NE(nullptr, target);
  ASSERT_NE(nullptr, target->GetProcess());

  target->GetProcess()->GetSymbols()->InjectModuleForTesting(
      "fakelib", "abc123",
      std::make_unique<LoadedModuleSymbols>(nullptr, "abc123", 0));

  console.ProcessInputLine("sym-stat");

  event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);

  auto text = event.output.AsString();
  EXPECT_NE(text.find("Process 1 symbol status"), std::string::npos);
  EXPECT_NE(text.find("Build ID: abc123"), std::string::npos);
}

}  // namespace zxdb
