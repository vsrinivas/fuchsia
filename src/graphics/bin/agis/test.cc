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

  void Register(const char *url) {
    outstanding++;
    session_->Register(url, [&](fuchsia::gpu::agis::Session_Register_Result result) {
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

  void Unregister(const char *url) {
    outstanding++;
    session_->Unregister(url, [&](fuchsia::gpu::agis::Session_Unregister_Result result) {
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
            FX_LOGF(INFO, "agis-test", "AgisTest::Connection \"%s\"",
                    connection.component_url.c_str());
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
};

TEST_F(AgisTest, Register) {
  const char *url = "fuchsia.gpu.agis.test-register";
  Register(url);
  outstanding++;
  session_->Register(url, [&](fuchsia::gpu::agis::Session_Register_Result result) {
    EXPECT_EQ(result.err(), fuchsia::gpu::agis::Status::ALREADY_REGISTERED);
    outstanding--;
  });
  Unregister(url);
}

TEST_F(AgisTest, Unregister) {
  const char *url = "fuchsia.gpu.agis.test-unregister";
  Register(url);
  Unregister(url);
  session_->Unregister(url, [&](fuchsia::gpu::agis::Session_Unregister_Result result) {
    const auto status = result.err();
    EXPECT_EQ(status, fuchsia::gpu::agis::Status::NOT_FOUND);
  });
}

TEST_F(AgisTest, Connections) {
  const char *url0 = "fuchsia.gpu.agis.test-connections-0";
  const char *url1 = "fuchsia.gpu.agis.test-connections-1";
  Register(url0);
  Register(url1);
  Connections();
  LoopWait();
  EXPECT_EQ(num_connections_, 2ul);
  Unregister(url0);
  Unregister(url1);
  Connections();
  LoopWait();
  EXPECT_EQ(num_connections_, 0ul);
}

TEST_F(AgisTest, MaxConnections) {
  char url[PATH_MAX];
  uint32_t i = 0;
  for (i = 0; i < fuchsia::gpu::agis::MAX_CONNECTIONS; i++) {
    snprintf(url, PATH_MAX, "%s%04d", "fuchsia.gpu.agis.max-connections", i);
    Register(url);
  }
  snprintf(url, PATH_MAX, "%s%04d", "fuchsia.gpu.agis.max-connections", i);
  outstanding++;
  session_->Register(url, [&](fuchsia::gpu::agis::Session_Register_Result result) {
    EXPECT_EQ(result.err(), fuchsia::gpu::agis::Status::CONNECTIONS_EXCEEDED);
    outstanding--;
  });
  for (i = 0; i < fuchsia::gpu::agis::MAX_CONNECTIONS; i++) {
    snprintf(url, PATH_MAX, "%s%04d", "fuchsia.gpu.agis.max-connections", i);
    Unregister(url);
  }
}

TEST_F(AgisTest, UsableSocket) {
  // Register and retrieve the socket for the server.
  int server_fd = -1;
  const char *url = "fuchsia.gpu.agis.test-usable-socket";
  outstanding++;
  session_->Register(url, [&](fuchsia::gpu::agis::Session_Register_Result result) {
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
  session_->Connections([&](fuchsia::gpu::agis::Session_Connections_Result result) {
    fuchsia::gpu::agis::Session_Connections_Response response(std::move(result.response()));
    EXPECT_EQ(result.err(), fuchsia::gpu::agis::Status::OK);
    std::vector<fuchsia::gpu::agis::Connection> connections(response.ResultValue_());
    EXPECT_EQ(connections.size(), 1ul);
    port = connections.front().port;
    outstanding--;
  });
  LoopWait();
  EXPECT_NE(port, 0);

  // Start server listening on the socket retrieved from AGIS.
  const char message[] = "AGIS Client Message";
  outstanding++;
  std::thread server([&] {
    const int kMaxPendingQueueSize = 1;
    const int listen_rv = listen(server_fd, kMaxPendingQueueSize);
    ASSERT_EQ(listen_rv, 0) << "Listen failed with " << strerror(errno);

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
  const int connect_rv =
      connect(client_fd, reinterpret_cast<struct sockaddr *>(&serv_addr), sizeof(serv_addr));
  ASSERT_EQ(connect_rv, 0) << "Connect failed with: " << strerror(errno);

  // Send message to the server.
  const ssize_t bytes_written = write(client_fd, message, strlen(message));
  EXPECT_EQ(bytes_written, static_cast<ssize_t>(strlen(message)));

  LoopWait();
  close(client_fd);
  close(server_fd);
  server.join();
}

TEST(AgisDisconnect, Main) {
  const std::string url("fuchsia.gpu.agis.test-disconnect");
  bool disconnect_outstanding = false;
  auto loop = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
  std::unique_ptr<sys::ComponentContext> context = sys::ComponentContext::Create();
  auto loop_wait = [&disconnect_outstanding, &loop]() {
    while (disconnect_outstanding) {
      EXPECT_EQ(loop->RunUntilIdle(), ZX_OK);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  };

  // Create a Session, register |url| and verify its presence.
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
    session->Register(url, [&](fuchsia::gpu::agis::Session_Register_Result result) {
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
        if (connection.component_url == url) {
          found = true;
          break;
        }
      }
      EXPECT_TRUE(found);
      disconnect_outstanding = false;
    });
    loop_wait();
  }

  // Create a new Session and verify that |url| is no longer registered.
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
        if (connection.component_url == url) {
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
