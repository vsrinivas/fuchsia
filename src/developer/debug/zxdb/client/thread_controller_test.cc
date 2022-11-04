// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/thread_controller_test.h"

#include "src/developer/debug/zxdb/client/process_impl.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/client/target_impl.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"

namespace zxdb {

// static
const uint64_t ThreadControllerTest::kUnsymbolizedModuleAddress;

// static
const uint64_t ThreadControllerTest::kSymbolizedModuleAddress;

ThreadControllerTest::ThreadControllerTest() = default;
ThreadControllerTest::~ThreadControllerTest() = default;

void ThreadControllerTest::SetUp() {
  RemoteAPITest::SetUp();
  process_ = InjectProcess(0x1234);
  thread_ = InjectThread(process_->GetKoid(), 0x7890);

  // Inject mock module symbols.
  std::string symbolized_build_id("abcd");  // Identifies the module below.
  std::string unsymbolized_build_id("zxyz");
  module_symbols_ = MakeModuleSymbols();
  unsymbolized_module_symbols_ = MakeModuleSymbols();
  session().system().GetSymbols()->InjectModuleForTesting(symbolized_build_id,
                                                          module_symbols_.get());
  session().system().GetSymbols()->InjectModuleForTesting(unsymbolized_build_id,
                                                          unsymbolized_module_symbols_.get());

  // Make the process load the mocked module symbols and the other one with no symbols.
  std::vector<debug_ipc::Module> modules;
  debug_ipc::Module unsym_load;
  unsym_load.name = "nosym";
  unsym_load.base = kUnsymbolizedModuleAddress;
  unsym_load.build_id = unsymbolized_build_id;
  modules.push_back(unsym_load);

  debug_ipc::Module sym_load;
  sym_load.name = "sym";
  sym_load.base = kSymbolizedModuleAddress;
  sym_load.build_id = symbolized_build_id;
  modules.push_back(sym_load);

  TargetImpl* target = session().system().GetTargetImpls()[0];
  target->process()->OnModules(modules);
  mock_remote_api()->GetAndResetResumeCount();  // OnModules will trigger a resume request.
}

fxl::RefPtr<MockModuleSymbols> ThreadControllerTest::MakeModuleSymbols() {
  return fxl::MakeRefCounted<MockModuleSymbols>("file.so");
}

}  // namespace zxdb
