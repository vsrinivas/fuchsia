// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_CONTEXT_TEST_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_CONTEXT_TEST_H_

#include <memory>

#include "src/developer/debug/shared/test_stream_buffer.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/debug_adapter/context.h"

namespace zxdb {

class Process;
class Thread;

// Test class that uses two StreamBuffer to create a two-way pipe.
class TestPipe {
 public:
  TestPipe() = default;
  ~TestPipe() = default;

  debug_ipc::StreamBuffer* end1() { return &end1_; }
  const debug_ipc::StreamBuffer& end1() const { return end1_; }

  debug_ipc::StreamBuffer* end2() { return &end2_; }
  const debug_ipc::StreamBuffer& end2() const { return end2_; }

  class PipeWriter : public debug_ipc::StreamBuffer::Writer {
   public:
    PipeWriter(debug_ipc::StreamBuffer& src, debug_ipc::StreamBuffer& sink)
        : src_(src), sink_(sink) {
      src_.set_writer(this);
    }

   private:
    // StreamBuffer::Writer implementation.
    size_t ConsumeStreamBufferData(const char* data, size_t len) override {
      sink_.AddReadData(std::vector<char>(data, data + len));
      return len;
    }
    debug_ipc::StreamBuffer& src_;
    debug_ipc::StreamBuffer& sink_;
  };

 private:
  debug_ipc::StreamBuffer end1_;
  debug_ipc::StreamBuffer end2_;
  PipeWriter end1_to_2{end1_, end2_};
  PipeWriter end2_to_1{end2_, end1_};

  FXL_DISALLOW_COPY_AND_ASSIGN(TestPipe);
};

// Test harness that sets up a RemoteAPITest (mocked target by replacing IPC) with a
// DebugAdapterContext and a debug adapter client session using the cppdap library.
//
// DebugAdapterContext is connected to the client via TestPipe.
// Client session can be used to send requests to DebugAdapterContext:
//
//   auto response = client().send(dap::InitializeRequest{});
//
// And then invoke context() to process incoming request:
//
//   context().OnStreamReadable();
//
// Lastly invoke client() to receive the response:
//
//   RunClient();
//   auto got = response.get();
//
class DebugAdapterContextTest : public RemoteAPITest {
 public:
  // The IDs associated with the process/thread that are set up by default.
  static constexpr uint64_t kProcessKoid = 875123541;
  static constexpr uint64_t kThreadKoid = 19028730;

  DebugAdapterContext& context() { return *context_.get(); }
  dap::Session& client() { return *client_.get(); }

  void RunClient() {
    if (auto payload = client_->getPayload()) {
      payload();
    }
  }

  Process* process() const { return process_; }
  Thread* thread() const { return thread_; }

  // testing::Test implementation.
  void SetUp() override;
  void TearDown() override;

 private:
  std::unique_ptr<DebugAdapterContext> context_;
  std::unique_ptr<dap::Session> client_;
  TestPipe pipe_;

  // The injected process/thread.
  Process* process_ = nullptr;
  Thread* thread_ = nullptr;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_CONTEXT_TEST_H_
