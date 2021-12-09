// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <fuchsia/gpu/agis/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/global.h>

#include <thread>

#include <gtest/gtest.h>
#include <sdk/lib/fdio/include/lib/fdio/fd.h>

namespace {
std::atomic<int> outstanding = 0;

zx_koid_t ProcessID() {
  zx::unowned<zx::process> process = zx::process::self();
  zx_info_handle_basic_t info;
  zx_status_t status = zx_object_get_info(process->get(), ZX_INFO_HANDLE_BASIC, &info, sizeof(info),
                                          nullptr /* actual */, nullptr /* avail */);
  EXPECT_EQ(status, ZX_OK);
  return info.koid;
}

std::string ProcessName() {
  zx::unowned<zx::process> process = zx::process::self();
  char process_name[ZX_MAX_NAME_LEN];
  process->get_property(ZX_PROP_NAME, process_name, sizeof(process_name));
  return process_name;
}

}  // namespace

// AgisTest uses asynchronous FIDL entrypoints to provide reusable code for real
// world usage of the agis service.
class AgisTest : public testing::Test {
 protected:
  void SetUp() override {
    num_connections_ = 0;
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);

    std::unique_ptr<sys::ComponentContext> context = sys::ComponentContext::Create();
    context->svc()->Connect(session_.NewRequest(loop_->dispatcher()));

    session_.set_error_handler([this](zx_status_t status) {
      FX_LOG(ERROR, "agis-test", "AgisTest::ErrHandler");
      loop_->Quit();
    });

    process_id_ = ProcessID();
    process_name_ = ProcessName();
  }

  void TearDown() override {
    if (loop_) {
      LoopWait();
      loop_->Quit();
    }
  }

  void LoopWait() {
    while (outstanding) {
      EXPECT_EQ(loop_->RunUntilIdle(), ZX_OK);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }

  void Register(zx_koid_t process_id, std::string process_name) {
    outstanding++;
    session_->Register(
        process_id, std::move(process_name),
        [&](fuchsia::gpu::agis::Session_Register_Result result) {
          fuchsia::gpu::agis::Session_Register_Response response(std::move(result.response()));
          const auto status = result.err();
          EXPECT_EQ(status, fuchsia::gpu::agis::Status::OK);
          zx::handle socket = response.ResultValue_();
          EXPECT_NE(socket.get(), 0ul) << "Null socket handle response";
          int socket_fd = -1;
          EXPECT_EQ(ZX_OK, fdio_fd_create(socket.release(), &socket_fd));
          close(socket_fd);
          outstanding--;
        });
  }

  void Unregister(zx_koid_t process_id) {
    outstanding++;
    session_->Unregister(process_id, [&](fuchsia::gpu::agis::Session_Unregister_Result result) {
      EXPECT_EQ(result.err(), fuchsia::gpu::agis::Status::OK);
      outstanding--;
    });
  }

  void Connections() {
    outstanding++;
    session_->Connections([&](fuchsia::gpu::agis::Session_Connections_Result result) {
      fuchsia::gpu::agis::Session_Connections_Response response(std::move(result.response()));
      const auto status = result.err();
      EXPECT_EQ(status, fuchsia::gpu::agis::Status::OK);
      switch (status) {
        case fuchsia::gpu::agis::Status::OK: {
          std::vector<fuchsia::gpu::agis::Connection> connections(response.ResultValue_());
          for (auto &connection : connections) {
            FX_LOGF(INFO, "agis-test", "AgisTest::Connection \"%lu\" \"%s\"",
                    connection.process_id(), connection.process_name().c_str());
          }
          num_connections_ = connections.size();
          break;
        }
        default:
          num_connections_ = 0;
          break;
      }
      outstanding--;
    });
  }

  std::unique_ptr<async::Loop> loop_;
  fuchsia::gpu::agis::SessionPtr session_;
  size_t num_connections_;
  zx_koid_t process_id_;
  std::string process_name_;
};

TEST_F(AgisTest, Register) {
  Register(process_id_, process_name_);
  outstanding++;
  session_->Register(process_id_, process_name_,
                     [&](fuchsia::gpu::agis::Session_Register_Result result) {
                       EXPECT_EQ(result.err(), fuchsia::gpu::agis::Status::ALREADY_REGISTERED);
                       outstanding--;
                     });
  Unregister(process_id_);
}

TEST_F(AgisTest, Unregister) {
  Register(process_id_, process_name_);
  Unregister(process_id_);
  session_->Unregister(process_id_, [&](fuchsia::gpu::agis::Session_Unregister_Result result) {
    const auto status = result.err();
    EXPECT_EQ(status, fuchsia::gpu::agis::Status::NOT_FOUND);
  });
}

TEST_F(AgisTest, Connections) {
  Register(process_id_, process_name_);
  Register(process_id_ + 1, process_name_ + "+1");
  Connections();
  LoopWait();
  EXPECT_EQ(num_connections_, 2ul);
  Unregister(process_id_);
  Unregister(process_id_ + 1);
  Connections();
  LoopWait();
  EXPECT_EQ(num_connections_, 0ul);
}

TEST_F(AgisTest, MaxConnections) {
  uint32_t i = 0;
  for (i = 0; i < fuchsia::gpu::agis::MAX_CONNECTIONS; i++) {
    Register(process_id_ + i, process_name_ + "+" + std::to_string(i));
  }
  outstanding++;
  session_->Register(process_id_ + i, process_name_ + "+" + std::to_string(i),
                     [&](fuchsia::gpu::agis::Session_Register_Result result) {
                       EXPECT_EQ(result.err(), fuchsia::gpu::agis::Status::CONNECTIONS_EXCEEDED);
                       outstanding--;
                     });
  for (i = 0; i < fuchsia::gpu::agis::MAX_CONNECTIONS; i++) {
    Unregister(process_id_ + i);
  }
}

TEST_F(AgisTest, UsableSocket) {
  // Register and retrieve the socket for the server.
  int server_fd = -1;
  outstanding++;
  session_->Register(
      process_id_, process_name_, [&](fuchsia::gpu::agis::Session_Register_Result result) {
        fuchsia::gpu::agis::Session_Register_Response response(std::move(result.response()));
        EXPECT_EQ(result.err(), fuchsia::gpu::agis::Status::OK);
        zx::handle socket = response.ResultValue_();
        EXPECT_EQ(ZX_OK, fdio_fd_create(socket.release(), &server_fd));
        outstanding--;
      });
  LoopWait();
  EXPECT_NE(server_fd, -1);

  // Get the port assignment for the client.
  outstanding++;
  uint16_t port = 0;
  std::string process_name;
  session_->Connections([&](fuchsia::gpu::agis::Session_Connections_Result result) {
    fuchsia::gpu::agis::Session_Connections_Response response(std::move(result.response()));
    EXPECT_EQ(result.err(), fuchsia::gpu::agis::Status::OK);
    std::vector<fuchsia::gpu::agis::Connection> connections(response.ResultValue_());
    EXPECT_EQ(connections.size(), 1ul);
    port = connections.front().port();
    process_name = connections.front().process_name();
    outstanding--;
  });
  LoopWait();
  EXPECT_NE(port, 0);
  EXPECT_EQ(process_name, process_name_);

  // Start server listening on the socket retrieved from AGIS.
  std::mutex mutex;
  bool listening(false);
  std::condition_variable condition;
  const char message[] = "AGIS Client Message";
  outstanding++;
  std::thread server([&] {
    {
      std::unique_lock lock(mutex);
      const int kMaxPendingQueueSize = 1;
      const int listen_rv = listen(server_fd, kMaxPendingQueueSize);
      ASSERT_EQ(listen_rv, 0) << "Listen failed with " << strerror(errno);
      listening = true;
      condition.notify_one();
    }

    // Accept connection from the client.
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    const int connection_fd =
        accept(server_fd, reinterpret_cast<struct sockaddr *>(&client_addr), &client_addr_len);
    ASSERT_NE(connection_fd, -1) << "Accept failed with: " << strerror(errno);

    // Read client request.
    char buffer[128] = {};
    read(connection_fd, buffer, sizeof(buffer) - 1);
    EXPECT_EQ(strcmp(buffer, message), 0);
    close(connection_fd);
    outstanding--;
  });

  // Create client connection and send a message to the server.
  int client_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  ASSERT_NE(client_fd, -1) << "Socket failed with: " << strerror(errno);

  struct sockaddr_in serv_addr = {};
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);
  inet_aton("127.0.0.1", &(serv_addr.sin_addr));

  // Connect to the server.
  {
    std::unique_lock lock(mutex);
    while (!listening) {
      condition.wait(lock);
    }
    const int connect_rv =
        connect(client_fd, reinterpret_cast<struct sockaddr *>(&serv_addr), sizeof(serv_addr));
    ASSERT_EQ(connect_rv, 0) << "Connect failed with: " << strerror(errno);
  }

  // Send message to the server.
  const ssize_t bytes_written = write(client_fd, message, strlen(message));
  EXPECT_EQ(bytes_written, static_cast<ssize_t>(strlen(message)));

  LoopWait();
  close(client_fd);
  close(server_fd);
  server.join();
}

TEST(AgisDisconnect, Main) {
  zx_koid_t process_id = ProcessID();
  std::string process_name = ProcessName();
  bool disconnect_outstanding = false;
  auto loop = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
  std::unique_ptr<sys::ComponentContext> context = sys::ComponentContext::Create();
  auto loop_wait = [&disconnect_outstanding, &loop]() {
    while (disconnect_outstanding) {
      EXPECT_EQ(loop->RunUntilIdle(), ZX_OK);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  };

  // Create a Session, register |process_id| and verify its presence.
  {
    fuchsia::gpu::agis::SessionPtr session;
    context->svc()->Connect(session.NewRequest(loop->dispatcher()));
    session.set_error_handler([&loop](zx_status_t status) {
      FX_LOGF(ERROR, "agis-test", "Register Disconnect ErrHandler - status %d", status);
      if (loop) {
        loop->Quit();
      }
    });

    disconnect_outstanding = true;
    session->Register(process_id, process_name,
                      [&](fuchsia::gpu::agis::Session_Register_Result result) {
                        EXPECT_EQ(result.err(), fuchsia::gpu::agis::Status::OK);
                        disconnect_outstanding = false;
                      });
    loop_wait();

    disconnect_outstanding = true;
    session->Connections([&](fuchsia::gpu::agis::Session_Connections_Result result) {
      fuchsia::gpu::agis::Session_Connections_Response response(std::move(result.response()));
      EXPECT_EQ(result.err(), fuchsia::gpu::agis::Status::OK);
      std::vector<fuchsia::gpu::agis::Connection> connections(response.ResultValue_());
      EXPECT_EQ(connections.size(), 1ul);
      bool found = false;
      for (const auto &connection : connections) {
        if (connection.process_id() == process_id) {
          EXPECT_EQ(connection.process_name(), process_name);
          found = true;
          break;
        }
      }
      EXPECT_TRUE(found);
      disconnect_outstanding = false;
    });
    loop_wait();
  }

  // Create a new Session and verify that |process_id| is no longer registered.
  fuchsia::gpu::agis::SessionPtr session;
  context->svc()->Connect(session.NewRequest(loop->dispatcher()));
  session.set_error_handler([&loop](zx_status_t status) {
    FX_LOGF(ERROR, "agis-test", "Verify Disconnect ErrHandler - status %d", status);
    if (loop) {
      loop->Quit();
    }
  });

  bool found = true;
  while (found) {
    disconnect_outstanding = true;
    session->Connections([&](fuchsia::gpu::agis::Session_Connections_Result result) {
      EXPECT_EQ(result.err(), fuchsia::gpu::agis::Status::OK);
      auto connections(result.response().ResultValue_());
      bool component_found = false;
      for (const auto &connection : connections) {
        if (connection.process_id() == process_id) {
          EXPECT_EQ(connection.process_name(), process_name);
          component_found = true;
          break;
        }
      }
      found = component_found;
      disconnect_outstanding = false;
    });
    loop_wait();
  }

  // Self-documenting no-op.
  EXPECT_FALSE(found);

  loop->Quit();
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
