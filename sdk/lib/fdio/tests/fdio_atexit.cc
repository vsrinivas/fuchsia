// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.posix.socket/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/spawn.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/sync/completion.h>
#include <lib/zx/process.h>
#include <zircon/processargs.h>

#include <zxtest/zxtest.h>

namespace {

class Server final : public fidl::testing::WireTestBase<fuchsia_posix_socket::StreamSocket> {
 public:
  Server(zx_handle_t channel, zx::socket peer) : channel_(channel), peer_(std::move(peer)) {}

  void NotImplemented_(const std::string& name, ::fidl::CompleterBase& completer) override {
    ADD_FAILURE("%s should not be called", name.c_str());
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Query(QueryCompleter::Sync& completer) final {
    const std::string_view kProtocol = fuchsia_posix_socket::wire::kStreamSocketProtocolName;
    uint8_t* data = reinterpret_cast<uint8_t*>(const_cast<char*>(kProtocol.data()));
    completer.Reply(fidl::VectorView<uint8_t>::FromExternal(data, kProtocol.size()));
  }

  void Describe2(Describe2Completer::Sync& completer) override {
    zx::socket peer;
    if (const zx_status_t status =
            peer_.duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_WRITE, &peer);
        status != ZX_OK) {
      return completer.Close(status);
    }
    fidl::Arena alloc;
    completer.Reply(fuchsia_posix_socket::wire::StreamSocketDescribe2Response::Builder(alloc)
                        .socket(std::move(peer))
                        .Build());
  }

  void Accept(AcceptRequestView request, AcceptCompleter::Sync& completer) override {
    zx_status_t status = zx_object_signal_peer(channel_, 0, ZX_USER_SIGNAL_0);
    if (status != ZX_OK) {
      return completer.Close(status);
    }
    return completer.Close(sync_completion_wait(&accept_end_, ZX_TIME_INFINITE));
  }

  sync_completion_t& accept_end() { return accept_end_; }

 private:
  zx_handle_t channel_;
  zx::socket peer_;

  sync_completion_t accept_end_;
};

TEST(AtExit, ExitInAccept) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_posix_socket::StreamSocket>();
  ASSERT_OK(endpoints.status_value());

  zx::socket client_socket, server_socket;
  ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &client_socket, &server_socket));

  // We're going to need the raw handle so we can signal on it and close it.
  zx_handle_t server_handle = endpoints->server.channel().get();

  Server server(server_handle, std::move(server_socket));
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(fidl::BindSingleInFlightOnly(loop.dispatcher(), std::move(endpoints->server), &server));
  ASSERT_OK(loop.StartThread("fake-socket-server"));

  const char* argv[] = {"/pkg/bin/accept-child", nullptr};
  const fdio_spawn_action_t actions[] = {
      {
          .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
          .h =
              {
                  .id = PA_HND(PA_USER0, 0),
                  .handle = endpoints->client.channel().release(),
              },
      },
  };
  zx::process process;
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  ASSERT_OK(fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, argv[0], argv, nullptr,
                           sizeof(actions) / sizeof(fdio_spawn_action_t), actions,
                           process.reset_and_get_address(), err_msg),
            "%s", err_msg);

  // Wait until the child has let us know that it is exiting.
  ASSERT_OK(zx_object_wait_one(server_handle, ZX_USER_SIGNAL_0, ZX_TIME_INFINITE, nullptr));

  // Verify that the child didn't crash.
  ASSERT_OK(process.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr));
  sync_completion_signal(&server.accept_end());
  zx_info_process_t proc_info;
  ASSERT_OK(process.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr, nullptr));
  ASSERT_EQ(proc_info.return_code, 0);
}

}  // namespace
