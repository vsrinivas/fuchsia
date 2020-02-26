// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/lib/vsh/command_runner.h"

#include <lib/async/cpp/executor.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/virtualization/testing/fake_manager.h>
#include <lib/virtualization/testing/guest_cid.h>
#include <threads.h>

#include "src/lib/fxl/logging.h"
#include "src/virtualization/lib/vsh/util.h"
#include "src/virtualization/packages/biscotti_guest/third_party/protos/vsh.pb.h"

namespace vsh {
namespace {

// FakeVshd simply listens on the guest vsock endpoint and accepts connections
// on the vshd port.
//
// Tests can access the accepted connections with |TakeConnection| and then
// simulate vshd by reading/writing messages to the socket.
struct FakeVshd {
  zx_status_t Listen(guest::testing::FakeGuestVsock* guest_vsock) {
    return guest_vsock->Listen(vsh::kVshPort, fit::bind_member(this, &FakeVshd::OnNewConnection));
  }

  zx::socket TakeConnection() {
    FXL_CHECK(!connections.empty());
    auto conn = std::move(connections[0]);
    connections.erase(connections.begin());
    return conn;
  }

  std::vector<zx::socket> connections;

 private:
  zx_status_t OnNewConnection(zx::handle h) {
    zx::socket socket(std::move(h));
    connections.push_back(std::move(socket));
    return ZX_OK;
  }
};

struct BlockingCommandRunnerParams {
  BlockingCommandRunner runner;
  BlockingCommandRunner::Command command;
  fit::result<BlockingCommandRunner::CommandResult, zx_status_t> result;
};

int RunBlockingCommand(void* arg) {
  BlockingCommandRunnerParams* params = static_cast<BlockingCommandRunnerParams*>(arg);
  params->result = params->runner.Execute(std::move(params->command));
  return 0;
}

class VshCommandRunnerTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();
    // Setup a fake guest realm that we can use to connect to a
    // HostVsockEndpoint.
    provider_.service_directory_provider()->AddService(fake_guest_manager_.GetHandler());
    fuchsia::virtualization::ManagerPtr manager;
    provider_.context()->svc()->Connect(manager.NewRequest());
    manager->Create("test realm", realm_.NewRequest());
    RunLoopUntilIdle();

    ASSERT_EQ(ZX_OK, vshd_.Listen(fake_guest_manager_.GuestVsock()));
  }

  void TearDown() override { CloseConnectionsAndWaitForThreadJoin(); }

 protected:
  void SendStdout(const zx::socket& socket, const std::string& data) {
    SendStdio(socket, data, vm_tools::vsh::StdioStream::STDOUT_STREAM);
  }
  void SendStderr(const zx::socket& socket, const std::string& data) {
    SendStdio(socket, data, vm_tools::vsh::StdioStream::STDERR_STREAM);
  }
  void SendStdio(const zx::socket& socket, std::string data, vm_tools::vsh::StdioStream stream) {
    vm_tools::vsh::HostMessage host_message;
    auto data_message = host_message.mutable_data_message();
    data_message->set_stream(stream);
    data_message->set_data(data);
    ASSERT_TRUE(vsh::SendMessage(socket, host_message));
  }
  void SendExit(const zx::socket& socket, int return_code) {
    vm_tools::vsh::HostMessage host_message;
    auto status_message = host_message.mutable_status_message();
    status_message->set_status(vm_tools::vsh::ConnectionStatus::EXITED);
    status_message->set_code(return_code);
    ASSERT_TRUE(vsh::SendMessage(socket, host_message));
  }

  // Connects to the |HostVsockEnpoint| for the realm.
  fidl::InterfaceHandle<fuchsia::virtualization::HostVsockEndpoint> GetHostVsockEndpoint() {
    fuchsia::virtualization::HostVsockEndpointPtr result;
    realm_->GetHostVsockEndpoint(result.NewRequest());
    RunLoopUntilIdle();
    return result;
  }

  // Starts a new thread to execute the |BlockingCommandRunner| on. This is on
  // a different thread so that we can continue to interact with the
  // |BlockingCommandRunner| on the main thread by interacting with the server
  // end of the vshd socket.
  //
  // It is an error to call this method multiple times before a corresponding
  // call to |CloseConnectionsAndWaitForThreadJoin|.
  zx::socket StartBlockingCommandOnThread(BlockingCommandRunnerParams* params) {
    FXL_CHECK(!thread_running_);
    FXL_CHECK(thrd_create(&thread_, RunBlockingCommand, params) == thrd_success);
    thread_running_ = true;

    // Ensure the command runner is connected before continuing.
    size_t count = 0;
    while (vshd_.connections.empty() && count++ < 1000) {
      usleep(1000);
      RunLoopUntilIdle();
    }
    FXL_CHECK(!vshd_.connections.empty());
    return vshd_.TakeConnection();
  }

  // If the thread is running a |BlockingCommandRunner| wait for the thread
  // to join.
  //
  // Note this also closes all sockets that have been accepted. This is to
  // enable the |BlockingCommandRunner| to become unblocked from any blocking
  // read/write operations by virtue of receiving a |ZX_SOCKET_PEER_CLOSED|
  // signal.
  void CloseConnectionsAndWaitForThreadJoin() {
    vshd_.connections.clear();
    if (thread_running_) {
      thrd_join(thread_, nullptr);
      thread_running_ = false;
    }
  }

 private:
  bool thread_running_ = false;
  thrd_t thread_;
  FakeVshd vshd_;
  fuchsia::virtualization::RealmPtr realm_;
  guest::testing::FakeManager fake_guest_manager_;
  sys::testing::ComponentContextProvider provider_;
  std::unique_ptr<BlockingCommandRunner> command_runner_;
};

// Test sending and receiving messages by simulating a simple vsh session.
TEST_F(VshCommandRunnerTest, RunCommandCollectResult) {
  BlockingCommandRunnerParams params{
      {GetHostVsockEndpoint(), guest::testing::kGuestCid, vsh::kVshPort},
      /* argv */ /*env*/
      {{"some_test_exe"}, {}},
      {},
  };

  // Run the blocking operation on a different thread. Returns a socket to the
  // server (vshd) endpoint of the session.
  zx::socket conn = StartBlockingCommandOnThread(&params);

  {  // Read SetupConnectionRequest
    vm_tools::vsh::SetupConnectionRequest request;
    ASSERT_TRUE(vsh::RecvMessage(conn, &request));
    ASSERT_EQ(0, request.env_size());
    ASSERT_EQ(1, request.argv_size());
    ASSERT_EQ("some_test_exe", request.argv(0));
  }
  {  // Send SetupConnectionResponse
    vm_tools::vsh::SetupConnectionResponse response;
    response.set_status(vm_tools::vsh::ConnectionStatus::READY);
    ASSERT_TRUE(vsh::SendMessage(conn, response));
  }

  // Send some stdout/stderr
  SendStdout(conn, "Hello ");
  SendStderr(conn, "WARNING:");
  SendStdout(conn, "world!");
  SendStderr(conn, " this isn't real");

  // Now terminate the application
  SendExit(conn, -123);
  CloseConnectionsAndWaitForThreadJoin();

  // Verify we collected the expected out/err/exit_code.
  ASSERT_TRUE(params.result.is_ok());
  auto command_result = params.result.take_value();
  ASSERT_EQ(-123, command_result.return_code);
  ASSERT_EQ("Hello world!", command_result.out);
  ASSERT_EQ("WARNING: this isn't real", command_result.err);
}

}  // namespace
}  // namespace vsh
