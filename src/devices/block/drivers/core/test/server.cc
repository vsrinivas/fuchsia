// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "server.h"

#include <lib/sync/completion.h>
#include <unistd.h>

#include <thread>

#include <zxtest/zxtest.h>

#include "test/stub-block-device.h"

namespace {

class ServerTestFixture : public zxtest::Test {
 public:
  ServerTestFixture() : client_(blkdev_.proto()) {}

 protected:
  void SetUp() override {}
  void TearDown() override;

  void CreateThread();
  void WaitForThreadStart();
  void WaitForThreadExit();
  void JoinThread();

  StubBlockDevice blkdev_;
  ddk::BlockProtocolClient client_;
  std::unique_ptr<Server> server_;
  fzl::fifo<block_fifo_request_t, block_fifo_response_t> fifo_;

 private:
  static int RunServer(void* arg);

  sync_completion_t thread_started_;
  sync_completion_t thread_exited_;
  bool is_thread_running_ = false;
  std::thread thread_;
};

void ServerTestFixture::TearDown() { ASSERT_FALSE(is_thread_running_); }

int ServerTestFixture::RunServer(void* arg) {
  ServerTestFixture* fix = static_cast<ServerTestFixture*>(arg);
  sync_completion_signal(&fix->thread_started_);
  fix->server_->Serve();
  sync_completion_signal(&fix->thread_exited_);
  return 0;
}

void ServerTestFixture::CreateThread() {
  std::thread th(RunServer, this);
  thread_ = std::move(th);
  is_thread_running_ = true;
}

void ServerTestFixture::WaitForThreadStart() {
  ASSERT_OK(sync_completion_wait(&thread_started_, ZX_SEC(5)));
}

void ServerTestFixture::WaitForThreadExit() {
  ASSERT_OK(sync_completion_wait(&thread_exited_, ZX_SEC(5)));
}

void ServerTestFixture::JoinThread() {
  thread_.join();
  is_thread_running_ = false;
}

TEST_F(ServerTestFixture, CreateServer) { ASSERT_OK(Server::Create(&client_, &fifo_, &server_)); }

TEST_F(ServerTestFixture, StartServer) {
  ASSERT_OK(Server::Create(&client_, &fifo_, &server_));

  CreateThread();
  WaitForThreadStart();

  // This code is racy with Serve() being called. This is expected.
  // The server should handle shutdown commands at any time.
  server_->Shutdown();

  WaitForThreadExit();
  JoinThread();
}

TEST_F(ServerTestFixture, CloseFifo) {
  ASSERT_OK(Server::Create(&client_, &fifo_, &server_));

  CreateThread();
  WaitForThreadStart();

  // Allow the server thread to do some work. The thread may not always get to make progress
  // before the fifo is closed, but the server thread should handle it regardless.
  usleep(20 * 1000);

  fifo_.reset();

  WaitForThreadExit();
  JoinThread();
}

}  // namespace
