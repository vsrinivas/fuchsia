// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/console/mock_console.h"
#include "src/developer/debug/zxdb/symbols/loaded_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"

namespace zxdb {

namespace {

class VerbNewRmTest : public RemoteAPITest {
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

TEST_F(VerbNewRmTest, FilterAndJob) {
  MockConsole console(&session());

  console.ProcessInputLine("attach foobar");
  // This will issue a warning because there's no attached job. This comes as a separate output
  // event. Ignore this.
  console.GetOutputEvent();

  auto event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ(
      "Waiting for process matching \"foobar\".\n"
      "Type \"filter\" to see the current filters.",
      event.output.AsString());

  console.ProcessInputLine("filter");
  event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ(
      "  # pattern job\n"
      "▶ 1 foobar    *\n",
      event.output.AsString());

  // Create a new filter, it should be cloned from the original.
  console.ProcessInputLine("filter new");
  event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Filter 2 pattern=foobar job=* (all attached jobs)", event.output.AsString());

  // Delete the original filter.
  console.ProcessInputLine("filter 1 rm");
  event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Removed Filter 1 pattern=foobar job=* (all attached jobs)", event.output.AsString());

  // Create a new job.
  console.ProcessInputLine("job new");
  event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Job 2 state=\"Not attached\" name=\"\"", event.output.AsString());

  // Create a new filter specifically for the new job.
  console.ProcessInputLine("job 2 attach ninjas");
  // This will issue a warning because the job isn't attached. This comes as a separate output
  // event. Ignore this.
  console.GetOutputEvent();
  event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ(
      "Waiting for process matching \"ninjas\".\n"
      "Type \"filter\" to see the current filters.",
      event.output.AsString());

  // The filter list should be the 2nd filter with the 1st one's settings and the job-specific one.
  console.ProcessInputLine("filter");
  event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ(
      "  # pattern job\n"
      "  2 foobar    *\n"
      "▶ 3 ninjas    2\n",
      event.output.AsString());

  // Delete the job.
  console.ProcessInputLine("job rm");
  event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Removed Job 2 state=\"Not attached\" name=\"\"", event.output.AsString());

  // Deleting the job again should yield an error.
  console.ProcessInputLine("job rm");
  event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("No job to remove.", event.output.AsString());

  // The associated filter should have been automatically deleted.
  console.ProcessInputLine("filter");
  event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ(
      " # pattern job\n"
      " 2 foobar    *\n",
      event.output.AsString());
}

TEST_F(VerbNewRmTest, Process) {
  MockConsole console(&session());

  // Create process 2. It will become the current one.
  console.ProcessInputLine("pr new");
  auto event = console.GetOutputEvent();
  EXPECT_EQ("Process 2 state=\"Not running\" name=\"\"", event.output.AsString());

  console.ProcessInputLine("process rm");
  event = console.GetOutputEvent();
  EXPECT_EQ("Removed Process 2 state=\"Not running\" name=\"\"", event.output.AsString());

  // The removal should have reassigned the current process to #1.
  console.ProcessInputLine("pr");
  event = console.GetOutputEvent();
  EXPECT_EQ(
      "  # State       Koid Name\n"
      "▶ 1 Not running      \n",
      event.output.AsString());

  // Trying to delete the last one should fail.
  console.ProcessInputLine("pr 1 rm");
  event = console.GetOutputEvent();
  EXPECT_EQ("Can't delete the last target.", event.output.AsString());
}

TEST_F(VerbNewRmTest, Breakpoint) {
  MockConsole console(&session());

  // Removing with no breakpoint.
  console.ProcessInputLine("bp rm");
  auto event = console.GetOutputEvent();
  EXPECT_EQ("No breakpoint to remove.", event.output.AsString());

  // Create a new breakpoint.
  console.ProcessInputLine("bp new");
  event = console.GetOutputEvent();
  EXPECT_EQ("Breakpoint 1 pending @ <no location>\n", event.output.AsString());

  // Delete it.
  console.ProcessInputLine("breakpoint rm");
  event = console.GetOutputEvent();
  EXPECT_EQ("Removed Breakpoint 1 pending @ <no location>\n", event.output.AsString());
}

}  // namespace zxdb
