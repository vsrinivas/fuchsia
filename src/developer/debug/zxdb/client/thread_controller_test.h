// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "garnet/bin/zxdb/symbols/system_symbols.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"

namespace zxdb {

class MockModuleSymbols;
class Process;
class Thread;

// This test harness automatically makes a process and a thread
//
// In the future we will probably want to add support for setting up a mock
// symbol system (this is more involved).
class ThreadControllerTest : public RemoteAPITest {
 public:
  ThreadControllerTest();
  ~ThreadControllerTest();

  void SetUp() override;

  Process* process() { return process_; }
  Thread* thread() { return thread_; }

  // Load address that a mock module with no symbols is loaded at. If a test
  // needs an address into an unsymbolized module, it should be between this
  // value and kSymbolizedModuleAddress.
  static const uint64_t kUnsymbolizedModuleAddress;

  // Load address that the mock module with symbols is loaded at. Addresses you
  // want to support symbol lookup for need to be larger than this.
  static const uint64_t kSymbolizedModuleAddress;

  // The mock module symbols. Addresses above kModuleAddress will be handled
  // by this mock. Test code should inject the responses it wants into this
  // mock.
  MockModuleSymbols* module_symbols() const { return module_symbols_; }

  MockRemoteAPI* mock_remote_api() const { return mock_remote_api_; }

 private:
  // RemoteAPITest implementation:
  std::unique_ptr<RemoteAPI> GetRemoteAPIImpl() override;

  // Non-owning pointer to the injected fake process/thread.
  Process* process_ = nullptr;
  Thread* thread_ = nullptr;

  MockRemoteAPI* mock_remote_api_;  // Owned by the session.

  // Non-owning (the pointer is owned by the SystemSymbols and held alive
  // because of our ModuleRef below).
  MockModuleSymbols* module_symbols_ = nullptr;

  // This reference ensures the module_symbols_ pointer above is kept alive
  // for the duration of the test.
  fxl::RefPtr<SystemSymbols::ModuleRef> symbol_module_ref_;
};

}  // namespace zxdb
