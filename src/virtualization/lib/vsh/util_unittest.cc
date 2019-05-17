// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/lib/vsh/util.h"

#include <lib/gtest/test_loop_fixture.h>

#include "src/virtualization/packages/biscotti_guest/third_party/protos/vsh.pb.h"

namespace {

class VshUtilTest : public gtest::TestLoopFixture {};

// Test sending and receiving messages by simulating a simple vsh session.
TEST_F(VshUtilTest, SendRecv) {
  zx::socket client_end, server_end;
  ASSERT_EQ(ZX_OK,
            zx::socket::create(ZX_SOCKET_STREAM, &client_end, &server_end));

  {  // Client sends ConnectionSetupRequest
    vm_tools::vsh::SetupConnectionRequest request;
    auto env = request.mutable_env();
    (*env)["ENV_VAR1"] = "ENV_VAR_VALUE_1";
    (*env)["ENV_VAR2"] = "ENV_VAR_VALUE_2";
    request.add_argv("echo");
    request.add_argv("hello world");
    ASSERT_TRUE(vsh::SendMessage(client_end, request));
  }
  {
    vm_tools::vsh::SetupConnectionRequest request;
    ASSERT_TRUE(vsh::RecvMessage(server_end, &request));
    ASSERT_EQ(2, request.env_size());
    auto env = request.env();
    ASSERT_EQ(env["ENV_VAR1"], "ENV_VAR_VALUE_1");
    ASSERT_EQ(env["ENV_VAR2"], "ENV_VAR_VALUE_2");
    ASSERT_EQ(2, request.argv_size());
    ASSERT_EQ(request.argv(0), "echo");
    ASSERT_EQ(request.argv(1), "hello world");
  }

  {  // Server sends some stdout data
    vm_tools::vsh::HostMessage request;
    auto data_message = request.mutable_data_message();
    data_message->set_data("hello world");
    data_message->set_stream(vm_tools::vsh::StdioStream::STDOUT_STREAM);
    ASSERT_TRUE(vsh::SendMessage(server_end, request));
  }
  {
    vm_tools::vsh::HostMessage request;
    ASSERT_TRUE(vsh::RecvMessage(client_end, &request));
    ASSERT_TRUE(request.has_data_message());
    auto data_message = request.data_message();
    ASSERT_EQ("hello world", data_message.data());
    ASSERT_EQ(vm_tools::vsh::StdioStream::STDOUT_STREAM, data_message.stream());
  }

  {  // Server sends exit message
    vm_tools::vsh::HostMessage request;
    auto status_message = request.mutable_status_message();
    status_message->set_code(0);
    status_message->set_status(vm_tools::vsh::ConnectionStatus::EXITED);
    ASSERT_TRUE(vsh::SendMessage(server_end, request));
  }
  {
    vm_tools::vsh::HostMessage request;
    ASSERT_TRUE(vsh::RecvMessage(client_end, &request));
    ASSERT_TRUE(request.has_status_message());
    auto status_message = request.status_message();
    ASSERT_EQ(0, status_message.code());
    ASSERT_EQ(vm_tools::vsh::ConnectionStatus::EXITED, status_message.status());
  }
}

}  // namespace
