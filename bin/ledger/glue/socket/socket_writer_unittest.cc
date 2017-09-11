// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/glue/socket/socket_writer.h"

#include <utility>

#include "apps/ledger/src/glue/socket/socket_drainer_client.h"
#include "apps/ledger/src/glue/socket/socket_pair.h"
#include "gtest/gtest.h"
#include "lib/fxl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace glue {
namespace {

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

TEST(SocketWriter, WriteAndRead) {
  mtl::MessageLoop message_loop;
  glue::SocketPair socket;
  StringClient client("bazinga\n");
  SocketWriter writer(&client);
  writer.Start(std::move(socket.socket1));

  std::string value;
  auto drainer = std::make_unique<SocketDrainerClient>();
  drainer->Start(std::move(socket.socket2),
                 [&value, &message_loop](const std::string& v) {
                   value = v;
                   message_loop.PostQuitTask();
                 });
  message_loop.Run();

  EXPECT_EQ("bazinga\n", value);
  EXPECT_TRUE(client.completed());
}

TEST(SocketWriter, ClientClosedTheirEnd) {
  mtl::MessageLoop message_loop;
  glue::SocketPair socket;
  StringClient client("bazinga\n");
  SocketWriter writer(&client);
  socket.socket2.reset();
  writer.Start(std::move(socket.socket1));
  EXPECT_TRUE(client.completed());
}

TEST(SocketWriter, StringSocketWriter) {
  mtl::MessageLoop message_loop;
  glue::SocketPair socket;
  StringSocketWriter* writer = new StringSocketWriter();
  writer->Start("bazinga\n", std::move(socket.socket1));

  std::string value;
  auto drainer = std::make_unique<SocketDrainerClient>();
  drainer->Start(std::move(socket.socket2),
                 [&value, &message_loop](const std::string& v) {
                   value = v;
                   message_loop.PostQuitTask();
                 });
  message_loop.Run();

  EXPECT_EQ("bazinga\n", value);
}

}  // namespace
}  // namespace glue
