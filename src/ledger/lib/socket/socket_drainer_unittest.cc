// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/socket/socket_drainer.h"

#include <lib/fit/function.h>

#include "src/ledger/lib/socket/strings.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace ledger {
namespace {

using SocketDrainerTest = ::gtest::TestLoopFixture;

class Client : public SocketDrainer::Client {
 public:
  Client(fit::function<void()> available_callback, fit::function<void()> completion_callback)
      : available_callback_(std::move(available_callback)),
        completion_callback_(std::move(completion_callback)) {}
  ~Client() override {}

  std::string GetValue() { return value_; }

 private:
  void OnDataAvailable(const void* data, size_t num_bytes) override {
    value_.append(static_cast<const char*>(data), num_bytes);
    available_callback_();
  }
  void OnDataComplete() override { completion_callback_(); }

  std::string value_;
  fit::function<void()> available_callback_;
  fit::function<void()> completion_callback_;
};

TEST_F(SocketDrainerTest, ReadData) {
  Client client([] {}, [] {});
  SocketDrainer drainer(&client);
  drainer.Start(ledger::WriteStringToSocket("Hello"));
  RunLoopUntilIdle();
  EXPECT_EQ("Hello", client.GetValue());
}

TEST_F(SocketDrainerTest, DeleteOnCallback) {
  std::unique_ptr<SocketDrainer> drainer;
  Client client([&drainer] { drainer.reset(); }, [] {});
  drainer = std::make_unique<SocketDrainer>(&client);
  drainer->Start(ledger::WriteStringToSocket("H"));
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
  socket1.shutdown(ZX_SOCKET_SHUTDOWN_WRITE);
  RunLoopUntilIdle();
  EXPECT_EQ("Hello", client.GetValue());
}

}  // namespace
}  // namespace ledger
