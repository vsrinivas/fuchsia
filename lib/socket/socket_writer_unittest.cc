// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/socket/socket_writer.h"

#include <utility>

#include "lib/fxl/macros.h"
#include "lib/gtest/test_loop_fixture.h"
#include "peridot/lib/socket/socket_drainer_client.h"
#include "peridot/lib/socket/socket_pair.h"

namespace socket {
namespace {

using SocketWriterTest = gtest::TestLoopFixture;

class StringClient : public SocketWriter::Client {
 public:
  explicit StringClient(std::string value) : value_(std::move(value)) {}

  void GetNext(size_t offset,
               size_t max_size,
               std::function<void(fxl::StringView)> callback) override {
    fxl::StringView data = value_;
    callback(data.substr(offset, max_size));
  }

  void OnDataComplete() override { completed_ = true; }

  bool completed() { return completed_; }

 private:
  std::string value_;
  bool completed_ = false;
};

TEST_F(SocketWriterTest, WriteAndRead) {
  SocketPair socket;
  StringClient client("bazinga\n");
  SocketWriter writer(&client);
  writer.Start(std::move(socket.socket1));

  std::string value;
  auto drainer = std::make_unique<SocketDrainerClient>();
  drainer->Start(std::move(socket.socket2),
                 [&value](const std::string& v) {
                   value = v;
                 });
  RunLoopUntilIdle();

  EXPECT_EQ("bazinga\n", value);
  EXPECT_TRUE(client.completed());
}

TEST_F(SocketWriterTest, ClientClosedTheirEnd) {
  SocketPair socket;
  StringClient client("bazinga\n");
  SocketWriter writer(&client);
  socket.socket2.reset();
  writer.Start(std::move(socket.socket1));
  EXPECT_TRUE(client.completed());
}

TEST_F(SocketWriterTest, StringSocketWriter) {
  SocketPair socket;
  StringSocketWriter* writer = new StringSocketWriter();
  writer->Start("bazinga\n", std::move(socket.socket1));

  std::string value;
  auto drainer = std::make_unique<SocketDrainerClient>();
  drainer->Start(std::move(socket.socket2),
                 [&value](const std::string& v) {
                   value = v;
                 });
  RunLoopUntilIdle();

  EXPECT_EQ("bazinga\n", value);
}

}  // namespace
}  // namespace socket
