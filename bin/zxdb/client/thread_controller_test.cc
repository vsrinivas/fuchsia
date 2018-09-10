// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/thread_controller_test.h"

#include "garnet/bin/zxdb/client/process_impl.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/system.h"
#include "garnet/bin/zxdb/client/symbols/mock_module_symbols.h"
#include "garnet/bin/zxdb/client/target_impl.h"
#include "garnet/bin/zxdb/client/thread.h"

namespace zxdb {

// static
const uint64_t ThreadControllerTest::kModuleAddress = 0x5000000;

class ThreadControllerTest::ControllerTestSink : public RemoteAPI {
 public:
  // The argument is a variable to increment every time Resume() is called.
  explicit ControllerTestSink(ThreadControllerTest* test) : test_(test) {}

  void Resume(
      const debug_ipc::ResumeRequest& request,
      std::function<void(const Err&, debug_ipc::ResumeReply)> cb) override {
    test_->resume_count_++;
  }

  void AddOrChangeBreakpoint(
      const debug_ipc::AddOrChangeBreakpointRequest& request,
      std::function<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)> cb)
      override {
    test_->last_breakpoint_address_ = request.breakpoint.locations[0].address;
    test_->last_breakpoint_id_ = request.breakpoint.breakpoint_id;
    test_->add_breakpoint_count_++;
  }

  void RemoveBreakpoint(
      const debug_ipc::RemoveBreakpointRequest& request,
      std::function<void(const Err&, debug_ipc::RemoveBreakpointReply)> cb)
      override {
    test_->remove_breakpoint_count_++;
  }

 private:
  ThreadControllerTest* test_;
};

ThreadControllerTest::ThreadControllerTest() = default;
ThreadControllerTest::~ThreadControllerTest() = default;

void ThreadControllerTest::SetUp() {
  RemoteAPITest::SetUp();
  process_ = InjectProcess(0x1234);
  thread_ = InjectThread(process_->GetKoid(), 0x7890);

  // Inject a mock module symbols.
  std::string build_id("abcd");  // Identifies the module below.
  auto module_symbols = std::make_unique<MockModuleSymbols>("file.so");
  module_symbols_ = module_symbols.get();  // Save pointer for tests.
  symbol_module_ref_ =
      session().system().GetSymbols()->InjectModuleForTesting(
          build_id, std::move(module_symbols));

  // Make the process load the mocked module symbols.
  std::vector<debug_ipc::Module> modules;
  debug_ipc::Module load;
  load.name = "test";
  load.base = kModuleAddress;
  load.build_id = build_id;
  modules.push_back(load);

  TargetImpl* target = session().system_impl().GetTargetImpls()[0];
  target->process()->OnModules(modules, std::vector<uint64_t>());
}

std::unique_ptr<RemoteAPI> ThreadControllerTest::GetRemoteAPIImpl() {
  return std::make_unique<ControllerTestSink>(this);
}

}  // namespace zxdb
