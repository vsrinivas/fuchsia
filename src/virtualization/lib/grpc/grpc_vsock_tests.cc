// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async_promise/executor.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/virtualization/testing/fake_manager.h>

#include "src/virtualization/lib/grpc/grpc_vsock_server.h"
#include "src/virtualization/lib/grpc/grpc_vsock_stub.h"
#include "src/virtualization/lib/grpc/test_server.grpc.pb.h"

namespace {
constexpr uint32_t kTestServicePort = 1234;
constexpr const char* kTestMessage = "This is only a test";

class GrpcVsockTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();
    // Setup a fake guest realm that we can use to connect to a
    // HostVsockEndpoint.
    provider_.service_directory_provider()->AddService(
        fake_guest_manager_.GetHandler());
    fuchsia::virtualization::ManagerPtr manager;
    provider_.context()->svc()->Connect(manager.NewRequest());
    manager->Create("test realm", realm_.NewRequest());
    RunLoopUntilIdle();
  }

 protected:
  // Connects to the |HostVsockEnpoint| for the realm.
  fuchsia::virtualization::HostVsockEndpointPtr GetHostVsockEndpoint() {
    fuchsia::virtualization::HostVsockEndpointPtr result;
    realm_->GetHostVsockEndpoint(result.NewRequest());
    RunLoopUntilIdle();
    return result;
  }

  // Access the |FakeGuestVsock|. This can be used to simulate guest
  // interactions over virtio-vsock.
  guest::testing::FakeGuestVsock* guest_vsock() {
    return fake_guest_manager_.GuestVsock();
  }

  async::Executor* executor() { return &executor_; }

 private:
  async::Executor executor_{dispatcher()};
  fuchsia::virtualization::RealmPtr realm_;
  guest::testing::FakeManager fake_guest_manager_;
  sys::testing::ComponentContextProvider provider_;
  fuchsia::virtualization::HostVsockEndpointPtr socket_endpoint_;
};

// Simple gRPC service that echos messages back to the client.
class TestEchoServer : public vsock_test::Echo::Service {
 private:
  grpc::Status Echo(grpc::ServerContext* context,
                    const vsock_test::EchoMessage* request,
                    vsock_test::EchoMessage* response) override {
    response->mutable_echo_message()->assign(request->echo_message());
    return grpc::Status::OK;
  }
};

// Simulate a gRPC echo server running over virtio-vsock.
TEST_F(GrpcVsockTest, Echo) {
  // Setup gRPC vsock server.
  GrpcVsockServerBuilder server_builder(GetHostVsockEndpoint());
  TestEchoServer server_impl;
  server_builder.AddListenPort(kTestServicePort);
  server_builder.RegisterService(&server_impl);
  fit::result<std::unique_ptr<GrpcVsockServer>, zx_status_t> result;
  auto p = server_builder.Build().then(
      [&result](fit::result<std::unique_ptr<GrpcVsockServer>, zx_status_t>&
                    r) mutable { result = std::move(r); });
  executor()->schedule_task(std::move(p));
  RunLoopUntilIdle();

  // Verify the server was started.
  ASSERT_FALSE(result.is_pending());
  ASSERT_FALSE(result.is_error());
  ASSERT_TRUE(result.is_ok());
  auto server = result.take_value();
  ASSERT_TRUE(server != nullptr);

  // Connect to the service using the guest vsock endpoint.
  zx::handle guest_handle;
  guest_vsock()->ConnectToHost(
      kTestServicePort, [&](auto handle) { guest_handle = std::move(handle); });
  RunLoopUntilIdle();
  ASSERT_TRUE(guest_handle);
  // The gRPC server will always use socket as a transport.
  zx_info_handle_basic_t info;
  ASSERT_EQ(ZX_OK, guest_handle.get_info(ZX_INFO_HANDLE_BASIC, &info,
                                         sizeof(info), nullptr, nullptr));
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
