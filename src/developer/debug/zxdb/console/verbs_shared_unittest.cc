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

class VerbsSharedTest : public RemoteAPITest {
 public:
  std::unique_ptr<RemoteAPI> GetRemoteAPIImpl() {
    auto remote_api = std::make_unique<MockRemoteAPI>();
    mock_remote_api_ = remote_api.get();
    return remote_api;
  }

  MockRemoteAPI* mock_remote_api() const { return mock_remote_api_; }

 private:
  MockRemoteAPI* mock_remote_api_ = nullptr;  // Owned by System.
};

}  // namespace

TEST_F(VerbsSharedTest, Rm) {
  MockConsole console(&session());

  console.ProcessInputLine("attach foobar");

  auto event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ("Waiting for process matching \"foobar\"", event.output.AsString());

  console.ProcessInputLine("filter");

  event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ(
      "  # Pattern Job\n"
      "▶ 1 foobar    *\n",
      event.output.AsString());

  console.ProcessInputLine("filter 1 rm");
  console.ProcessInputLine("filter");

  event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ("No filters.\n", event.output.AsString());
}

}  // namespace zxdb
