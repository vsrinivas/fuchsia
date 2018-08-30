// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "garnet/bin/zxdb/client/remote_api_test.h"

namespace zxdb {

class Process;
class Thread;

// This test harness automatically makes a process and a thread
//
// In the future we will probably want to add support got setting up a mock
// symbol system (this is more involved).
class ThreadControllerTest : public RemoteAPITest {
 public:
  ThreadControllerTest();
  ~ThreadControllerTest();

  void SetUp() override;

  Process* process() { return process_; }
  Thread* thread() { return thread_; }

  // Information about the messages sent to the backend.
  int resume_count() const { return resume_count_; }
  int add_breakpoint_count() const { return add_breakpoint_count_; }
  int remove_breakpoint_count() const { return remove_breakpoint_count_; }
  uint32_t last_breakpoint_id() const { return last_breakpoint_id_; }
  uint64_t last_breakpoint_address() const { return last_breakpoint_address_; }

 private:
  class ControllerTestSink;
  friend ControllerTestSink;

  // RemoteAPITest implementation:
  std::unique_ptr<RemoteAPI> GetRemoteAPIImpl() override;

  // Non-owning pointer to the injected fake process/thread.
  Process* process_ = nullptr;
  Thread* thread_ = nullptr;

  int resume_count_ = 0;
  int add_breakpoint_count_ = 0;
  int remove_breakpoint_count_ = 0;
  uint32_t last_breakpoint_id_ = 0;
  uint64_t last_breakpoint_address_ = 0;
};

}  // namespace zxdb
