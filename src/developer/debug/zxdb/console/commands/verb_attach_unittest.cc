// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_attach.h"

#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/console/console_test.h"

namespace zxdb {

namespace {

class AttachTestRemoteAPI : public RemoteAPI {
 public:
  struct AttachLog {
    debug_ipc::AttachRequest request;
    fit::callback<void(const Err&, debug_ipc::AttachReply)> cb;
  };

  void Attach(const debug_ipc::AttachRequest& request,
              fit::callback<void(const Err&, debug_ipc::AttachReply)> cb) override {
    last_attach = AttachLog{request, std::move(cb)};
  }

  void UpdateFilter(const debug_ipc::UpdateFilterRequest& request,
                    fit::callback<void(const Err&, debug_ipc::UpdateFilterReply)> cb) override {
    update_filter_requests.push_back(request);
  }

  // Stores the last one.
  std::optional<AttachLog> last_attach;

  // Stores a log of all requests (since the tests needs all of them).
  std::vector<debug_ipc::UpdateFilterRequest> update_filter_requests;
};

class VerbAttach : public ConsoleTest {
 public:
  AttachTestRemoteAPI* attach_remote_api() { return attach_remote_api_; }

  // Returns the last filter in the last UpdateFilter request.
  const debug_ipc::Filter& GetLastFilter() {
    loop().RunUntilNoTasks();  // Filter sync is asynchronous.
    FX_CHECK(!attach_remote_api_->update_filter_requests.empty());
    FX_CHECK(!attach_remote_api_->update_filter_requests.back().filters.empty());
    return attach_remote_api_->update_filter_requests.back().filters.back();
  }

 protected:
  // RemoteAPITest overrides.
  std::unique_ptr<RemoteAPI> GetRemoteAPIImpl() override {
    auto remote_api = std::make_unique<AttachTestRemoteAPI>();
    attach_remote_api_ = remote_api.get();
    return remote_api;
  }

  AttachTestRemoteAPI* attach_remote_api_ = nullptr;
};

// a large but valid koid, as kernel-generated koids only use 63 bits.
constexpr uint64_t kLargeKoid = 0x1ull << 60;

}  // namespace

TEST_F(VerbAttach, Bad) {
  // Missing argument.
  console().ProcessInputLine("attach");
  auto event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Wrong number of arguments to attach.", event.output.AsString());

  // Can't attach to a process by filter.
  console().ProcessInputLine("process attach --exact 123");
  event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Attaching by filters doesn't support \"process\" noun.", event.output.AsString());

  console().ProcessInputLine("attach --exact");
  event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Wrong number of arguments to attach.", event.output.AsString());

  console().ProcessInputLine("attach --job 123 --exact");
  event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Wrong number of arguments to attach.", event.output.AsString());
}

TEST_F(VerbAttach, Koid) {
  const std::string kLargeKoidInString = std::to_string(kLargeKoid);
  const std::string kCommand = "attach " + kLargeKoidInString;
  console().ProcessInputLine(kCommand);

  // This should create a new process context and give "process 2" because the default console test
  // harness makes a mock running process #1 by default.
  ASSERT_TRUE(attach_remote_api()->last_attach);
  ASSERT_EQ(kLargeKoid, attach_remote_api()->last_attach->request.koid);
  debug_ipc::AttachReply reply;
  reply.status = debug::Status();
  reply.koid = kLargeKoid;
  reply.name = "some process";
  attach_remote_api()->last_attach->cb(Err(), reply);

  auto event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ(
      "Attached Process 2 state=Running koid=" + kLargeKoidInString + " name=\"some process\"\n",
      event.output.AsString());

  // Attaching to the same process again should give an error.
  console().ProcessInputLine(kCommand);
  event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Process " + kLargeKoidInString + " is already being debugged.",
            event.output.AsString());
}

TEST_F(VerbAttach, Filter) {
  // Normal filter case.
  console().ProcessInputLine("attach foo");
  auto event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ(
      "Waiting for process matching \"foo\".\n"
      "Type \"filter\" to see the current filters.",
      event.output.AsString());
  EXPECT_EQ(debug_ipc::Filter::Type::kProcessNameSubstr, GetLastFilter().type);
  EXPECT_EQ("foo", GetLastFilter().pattern);

  // Exact name.
  console().ProcessInputLine("attach --exact 12345");
  EXPECT_EQ(debug_ipc::Filter::Type::kProcessName, GetLastFilter().type);
  EXPECT_EQ("12345", GetLastFilter().pattern);
  console().ProcessInputLine("attach --exact /pkg/bin/true");
  EXPECT_EQ(debug_ipc::Filter::Type::kProcessName, GetLastFilter().type);
  EXPECT_EQ("/pkg/bin/true", GetLastFilter().pattern);

  // Extra long filter case with an exact name.
  const std::string kSuperLongName = "super_long_name_with_over_32_characters";
  console().ProcessInputLine("attach --exact " + kSuperLongName);
  EXPECT_EQ(debug_ipc::Filter::Type::kProcessName, GetLastFilter().type);
  EXPECT_EQ(kSuperLongName.substr(0, kZirconMaxNameLength), GetLastFilter().pattern);

  // Component URL.
  const std::string kComponentUrl = "fuchsia-pkg://devhost/package#meta/component.cm";
  console().ProcessInputLine("attach " + kComponentUrl);
  EXPECT_EQ(debug_ipc::Filter::Type::kComponentUrl, GetLastFilter().type);
  EXPECT_EQ(kComponentUrl, GetLastFilter().pattern);

  // Component moniker.
  const std::string kComponentMoniker = "/some_realm/" + kSuperLongName;
  console().ProcessInputLine("attach " + kComponentMoniker);
  EXPECT_EQ(debug_ipc::Filter::Type::kComponentMoniker, GetLastFilter().type);
  EXPECT_EQ(kComponentMoniker, GetLastFilter().pattern);

  // Component name.
  const std::string kComponentName = kSuperLongName + ".cm";
  console().ProcessInputLine("attach " + kComponentName);
  EXPECT_EQ(debug_ipc::Filter::Type::kComponentName, GetLastFilter().type);
  EXPECT_EQ(kComponentName, GetLastFilter().pattern);

  // Job without a name.
  console().ProcessInputLine("attach --job " + std::to_string(kLargeKoid));
  EXPECT_EQ(debug_ipc::Filter::Type::kProcessNameSubstr, GetLastFilter().type);
  EXPECT_EQ("", GetLastFilter().pattern);
  EXPECT_EQ(kLargeKoid, GetLastFilter().job_koid);

  // Job with an exact name.
  console().ProcessInputLine("attach -j 1234 --exact " + kSuperLongName);
  EXPECT_EQ(debug_ipc::Filter::Type::kProcessName, GetLastFilter().type);
  EXPECT_EQ(kSuperLongName.substr(0, kZirconMaxNameLength), GetLastFilter().pattern);
  EXPECT_EQ(1234ull, GetLastFilter().job_koid);
}

}  // namespace zxdb
