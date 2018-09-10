// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "garnet/bin/zxdb/client/remote_api_test.h"
#include "garnet/bin/zxdb/client/symbols/system_symbols.h"

namespace zxdb {

class MockModuleSymbols;
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

  // Load address that the mock module is loaded at. Addresses you want to
  // support symbol lookup for need to be larger than this.
  static const uint64_t kModuleAddress;

  // The mock module symbols. Addresses above kModuleAddress will be handled
  // by this mock. Test code should inject the responses it wants into this
  // mock.
  MockModuleSymbols* module_symbols() const { return module_symbols_; }

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

  // Non-owning (the pointer is owned by the SystemSymbols and held alive
  // because of our ModuleRef below).
  MockModuleSymbols* module_symbols_ = nullptr;

  // This reference ensures the module_symbols_ pointer above is kept alive
  // for the duration of the test.
  fxl::RefPtr<SystemSymbols::ModuleRef> symbol_module_ref_;
};

}  // namespace zxdb
