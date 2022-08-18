// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/executor.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/virtualization/testing/fake_manager.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/virtualization/lib/grpc/grpc_vsock_server.h"
#include "src/virtualization/lib/grpc/grpc_vsock_stub.h"
#include "src/virtualization/lib/grpc/test_server.grpc.pb.h"

namespace {

using ::fuchsia::virtualization::HostVsockEndpoint_Listen_Result;
using ::fuchsia::virtualization::Listener;

constexpr uint32_t kTestServicePort = 1234;
constexpr const char* kTestMessage = "This is only a test";

class GrpcVsockTest : public gtest::TestLoopFixture {
 protected:
  // Connects to the |HostVsockEnpoint| for the realm.
  fuchsia::virtualization::HostVsockEndpointPtr GetHostVsockEndpoint() {
    fuchsia::virtualization::HostVsockEndpointPtr result;
    host_vsock_.AddBinding(result.NewRequest());
    RunLoopUntilIdle();
    return result;
  }

  // Access the |FakeGuestVsock|. This can be used to simulate guest
  // interactions over virtio-vsock.
  guest::testing::FakeGuestVsock* guest_vsock() { return &guest_vsock_; }

  async::Executor* executor() { return &executor_; }

 private:
  async::Executor executor_{dispatcher()};
  guest::testing::FakeGuestVsock guest_vsock_{&host_vsock_};
  guest::testing::FakeHostVsock host_vsock_{&guest_vsock_};
  fuchsia::virtualization::HostVsockEndpointPtr socket_endpoint_;
};

// Simple gRPC service that echos messages back to the client.
class TestEchoServer : public vsock_test::Echo::Service {
 private:
  grpc::Status Echo(grpc::ServerContext* context, const vsock_test::EchoMessage* request,
                    vsock_test::EchoMessage* response) override {
    response->mutable_echo_message()->assign(request->echo_message());
    return grpc::Status::OK;
  }
};

// Simulate a gRPC echo server running over virtio-vsock.
TEST_F(GrpcVsockTest, Echo) {
  // Setup gRPC vsock server.
  GrpcVsockServerBuilder server_builder;
  TestEchoServer server_impl;
  server_builder.AddListenPort(kTestServicePort);
  server_builder.RegisterService(&server_impl);
  auto result = server_builder.Build();

  // Verify the server was started.
  ASSERT_FALSE(result.is_error());
  ASSERT_TRUE(result.is_ok());

  auto server = std::move(result->first);
  auto listeners = std::move(result->second);

  ASSERT_TRUE(server != nullptr);
  ASSERT_EQ(listeners.size(), 1ul);

  auto endpoint = GetHostVsockEndpoint();
  bool listen_callback_seen = false;
  endpoint->Listen(listeners[0].port, std::move(listeners[0].acceptor),
                   [&listen_callback_seen](HostVsockEndpoint_Listen_Result result) {
                     ASSERT_TRUE(result.is_response());
                     listen_callback_seen = true;
                   });
  RunLoopUntilIdle();
  ASSERT_TRUE(listen_callback_seen);

  // Connect to the service using the guest vsock endpoint.
  zx::handle guest_handle;
  guest_vsock()->ConnectToHost(kTestServicePort,
                               [&](auto handle) { guest_handle = std::move(handle); });
  RunLoopUntilIdle();
  ASSERT_TRUE(guest_handle);
  // The gRPC server will always use socket as a transport.
  zx_info_handle_basic_t info;
  ASSERT_EQ(ZX_OK,
            guest_handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
  ASSERT_EQ(zx::socket::TYPE, info.type);
  zx::socket guest_socket(std::move(guest_handle));

  // Convert zx::socket into a gRPC stub that can be used to send RPCs.
  auto stub_result = NewGrpcStub<vsock_test::Echo>(std::move(guest_socket));
  ASSERT_TRUE(stub_result.is_ok());
  auto stub = stub_result.take_value();

  // Test echo
  grpc::ClientContext context;
  vsock_test::EchoMessage request;
  vsock_test::EchoMessage response;
  request.mutable_echo_message()->assign(kTestMessage);
  auto grpc_status = stub->Echo(&context, request, &response);
  ASSERT_TRUE(grpc_status.ok());
  ASSERT_EQ(kTestMessage, response.echo_message());
}

}  // namespace
