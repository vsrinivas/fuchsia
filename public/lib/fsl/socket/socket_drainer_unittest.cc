// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/socket/socket_drainer.h"

#include "lib/fsl/socket/strings.h"
#include "lib/gtest/test_loop_fixture.h"

namespace fsl {
namespace {

using SocketDrainerTest = ::gtest::TestLoopFixture;

class Client : public SocketDrainer::Client {
 public:
  Client(const std::function<void()>& available_callback,
         const std::function<void()>& completion_callback)
      : available_callback_(available_callback),
        completion_callback_(completion_callback) {}
  ~Client() override {}

  std::string GetValue() { return value_; }

 private:
  void OnDataAvailable(const void* data, size_t num_bytes) override {
    value_.append(static_cast<const char*>(data), num_bytes);
    available_callback_();
  }
  void OnDataComplete() override { completion_callback_(); }

  std::string value_;
  std::function<void()> available_callback_;
  std::function<void()> completion_callback_;
};

TEST_F(SocketDrainerTest, ReadData) {
  Client client([] {}, [] {});
  SocketDrainer drainer(&client);
  drainer.Start(fsl::WriteStringToSocket("Hello"));
  RunLoopUntilIdle();
  EXPECT_EQ("Hello", client.GetValue());
}

TEST_F(SocketDrainerTest, DeleteOnCallback) {
  std::unique_ptr<SocketDrainer> drainer;
  Client client([&drainer] { drainer.reset(); }, [] {});
  drainer = std::make_unique<SocketDrainer>(&client);
  drainer->Start(fsl::WriteStringToSocket("H"));
  RunLoopUntilIdle();
  EXPECT_EQ("H", client.GetValue());
  EXPECT_EQ(nullptr, drainer.get());
}

TEST_F(SocketDrainerTest, ShutdownRead) {
  Client client([] {}, [] {});
  SocketDrainer drainer(&client);
  zx::socket socket1, socket2;
  ASSERT_EQ(ZX_OK, zx::socket::create(0u, &socket1, &socket2));
  drainer.Start(std::move(socket2));
  char buf[] = {'H', 'e', 'l', 'l', 'o'};
  socket1.write(0u, buf, sizeof(buf), nullptr);
  socket1.write(ZX_SOCKET_SHUTDOWN_WRITE, nullptr, 0u, nullptr);
  RunLoopUntilIdle();
  EXPECT_EQ("Hello", client.GetValue());
}

}  // namespace
}  // namespace fsl
