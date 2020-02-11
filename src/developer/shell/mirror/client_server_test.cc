// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/time.h>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "gtest/gtest.h"
#include "src/developer/shell/mirror/client.h"
#include "src/developer/shell/mirror/command_line_options.h"
#include "src/developer/shell/mirror/server.h"
#include "src/developer/shell/mirror/test_shared.h"

namespace shell::mirror {

class ClientServerTest : public ::testing::Test {
 public:
  ClientServerTest() {}
};

class MessageLoopHolder {
 public:
  explicit MessageLoopHolder(std::unique_ptr<debug_ipc::PlatformMessageLoop> ptr)
      : ptr_(std::move(ptr)) {}
  ~MessageLoopHolder() { ptr_->Cleanup(); }
  debug_ipc::PlatformMessageLoop* operator->() { return ptr_.get(); }
  debug_ipc::PlatformMessageLoop* get() { return ptr_.get(); }

 private:
  std::unique_ptr<debug_ipc::PlatformMessageLoop> ptr_;
};

TEST_F(ClientServerTest, RoundTrip) {
  const std::string kDataDir = "/client_server_test_tmp";
  FileRepo repo(&kAsyncLoopConfigNoAttachToCurrentThread);
  repo.InitMemRepo(kDataDir);

  // Generate some fake files.
  std::vector<std::pair<std::string, std::string>> golden = {
      {kDataDir + "/z.txt", ""},
      {kDataDir + "/a.txt", "Once upon a midnight dreary, while I pondered, weak and weary,"},
      {kDataDir + "/b.txt", "Over many a quaint and curious volume of forgotten lore"},
      {kDataDir + "/c.txt", "While I nodded, nearly napping, suddenly there came a tapping,"},
      {kDataDir + "/d.txt", "As of some one gently rapping, rapping at my chamber door."}};

  repo.WriteFiles(golden);

  CommandLineOptions options;
  options.port = 0;
  options.path = kDataDir;

  std::condition_variable cond_var;
  std::mutex mutex;
  uint16_t port;

  std::thread server_thread([&options, &cond_var, &port]() {
    SocketServer server;
    SocketServer::ConnectionConfig config;
    config.port = options.port;
    config.path = options.path;
    Err err = server.RunInLoop(config, FROM_HERE, [&cond_var, &server, &port]() {
      port = server.GetPort();
      cond_var.notify_all();
    });
    ASSERT_TRUE(err.ok()) << err.msg;
  });

  // Wait until the server has started.
  {
    std::unique_lock<std::mutex> l(mutex);
    cond_var.wait(l);
  }

  std::string host_and_port = "[::1]:" + std::to_string(port);

  // Initialize connection and load results.
  client::ClientConnection load_connection;
  Err err = load_connection.Init(host_and_port);
  ASSERT_TRUE(err.ok()) << err.msg;
  // Impossibly high timeout, because we're a test.
  struct timeval tv;
  tv.tv_sec = 10000;
  tv.tv_usec = 0;
  Files files;
  err = load_connection.Load(&files, &tv);
  ASSERT_TRUE(err.ok()) << err.msg;

  // Initialize connection and kill server.
  client::ClientConnection kill_connection;
  err = kill_connection.Init(host_and_port);
  ASSERT_TRUE(err.ok()) << err.msg;
  err = kill_connection.KillServer();
  ASSERT_TRUE(err.ok()) << err.msg;
  server_thread.join();

  // Make sure results are as expected.
  ASSERT_EQ(files.size(), golden.size());
  for (const auto& file : files) {
    const auto& data = file.View();
    const auto& name = file.Name();
    bool found = false;
    for (size_t i = 0; i < golden.size(); i++) {
      if ((golden[i].first.substr(kDataDir.length() + 1) == name) && (golden[i].second == data)) {
        found = true;
        break;
      }
    }
    ASSERT_TRUE(found) << "Not found in expected: " << name << " with data \"" << data << "\"";
  }
}

}  // namespace shell::mirror
