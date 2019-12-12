// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_status.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/console/mock_console.h"

namespace zxdb {

namespace {

class TestRemoteAPI : public MockRemoteAPI {
 public:
  void Status(const debug_ipc::StatusRequest&,
              fit::callback<void(const Err&, debug_ipc::StatusReply)> cb) override {
    status_requests_++;

    debug_ipc::StatusReply reply = {};
    reply.limbo = limbo_;

    cb(Err(), std::move(reply));
  }

  void AppendToLimbo(uint64_t process_koid, const std::string& process_name) {
    debug_ipc::ProcessRecord record = {};
    record.process_koid = process_koid;
    record.process_name = process_name;
    limbo_.push_back(std::move(record));
  }

  int status_requests() const { return status_requests_; }

 private:
  std::vector<debug_ipc::ProcessRecord> limbo_;
  int status_requests_ = 0;
};

class VerbStatus : public RemoteAPITest {
 public:
  TestRemoteAPI* remote_api() const { return remote_api_; }

 protected:
  std::unique_ptr<RemoteAPI> GetRemoteAPIImpl() override {
    auto remote_api = std::make_unique<TestRemoteAPI>();
    remote_api_ = remote_api.get();
    return remote_api;
  }

 private:
  TestRemoteAPI* remote_api_;
};

}  // namespace

TEST_F(VerbStatus, Status) {
  debug_ipc::StreamBuffer stream;
  session().set_stream(&stream);
  ASSERT_TRUE(session().IsConnected());

  MockConsole console(&session());

  console.ProcessInputLine("status");
  ASSERT_EQ(remote_api()->status_requests(), 1);

  auto output = console.GetOutputEvent();
  ASSERT_EQ(output.type, MockConsole::OutputEvent::Type::kOutput);

  // Check that there are no processes found.
  {
    auto msg = output.output.AsString();
    ASSERT_NE(msg.find("No processes waiting on exception."), std::string::npos);
  }

  // Append a pair of exceptions.
  constexpr uint64_t kProcessKoid1 = 1;
  constexpr uint64_t kProcessKoid2 = 2;
  const std::string kProcessName1 = "process-1";
  const std::string kProcessName2 = "process-2";
  remote_api()->AppendToLimbo(kProcessKoid1, kProcessName1);
  remote_api()->AppendToLimbo(kProcessKoid2, kProcessName2);

  console.ProcessInputLine("status");
  ASSERT_EQ(remote_api()->status_requests(), 2);

  output = console.GetOutputEvent();
  ASSERT_EQ(output.type, MockConsole::OutputEvent::Type::kOutput);

  {
    auto msg = output.output.AsString();
    ASSERT_NE(msg.find("2 process(es) waiting on exception."), std::string::npos);
    ASSERT_NE(msg.find("process-1"), std::string::npos);
    ASSERT_NE(msg.find("process-2"), std::string::npos);
  }
}

TEST_F(VerbStatus, ConnectionStatus) {
  Session session;

  // No connection state.
  std::string no_conn_string = GetConnectionStatus(&session).AsString();
  EXPECT_NE(std::string::npos, no_conn_string.find("Not connected"));

  // Testing the connected connection status is currently difficult to mock and
  // is low-priority for testing. If Session were refactored this could become
  // practical.
}

TEST_F(VerbStatus, JobStatusNone) {
  Session empty_session;
  ConsoleContext empty_context(&empty_session);

  std::string no_conn_status = GetJobStatus(&empty_context).AsString();
  EXPECT_NE(std::string::npos, no_conn_status.find("Attached to 0 job(s)"));
}

}  // namespace zxdb
