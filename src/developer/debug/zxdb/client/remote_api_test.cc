// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/remote_api_test.h"

#include <inttypes.h>

#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/client/process_impl.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/client/target_impl.h"
#include "src/developer/debug/zxdb/client/thread_impl.h"
#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

void RemoteAPITest::SetUp() {
  session_ = std::make_unique<Session>(GetRemoteAPIImpl(), GetArch(), 4096);
}

void RemoteAPITest::TearDown() { session_.reset(); }

void RemoteAPITest::InjectModule(Process* process, fxl::RefPtr<ModuleSymbols> mod_sym,
                                 const std::string& name, uint64_t load_address,
                                 const std::string& build_id) {
  session().system().GetSymbols()->InjectModuleForTesting(build_id, mod_sym.get());

  std::vector<debug_ipc::Module> modules;
  debug_ipc::Module load;
  load.name = name;
  load.base = load_address;
  load.build_id = build_id;
  modules.push_back(load);

  // Need to convert to an actual ProcessImpl.
  ProcessImpl* process_impl = session().system().ProcessImplFromKoid(process->GetKoid());
  FX_CHECK(process_impl);
  process_impl->OnModules(modules);
}

fxl::RefPtr<MockModuleSymbols> RemoteAPITest::InjectMockModule(Process* process,
                                                               uint64_t load_address) {
  // Index to generate unique names for each mock module created. Must start > 0 because this is
  // used to generate a load address that can't be null.
  static int next_mock_module_id = 0;
  next_mock_module_id++;

  // Generate a load address if necessary.
  int effective_load_address;
  if (load_address) {
    effective_load_address = load_address;
  } else {
    // Use our unique index as the high 32-bits, with the low bits as 0.
    effective_load_address = static_cast<uint64_t>(next_mock_module_id) << 32;
  }

  std::string build_id = "mock_build_id_" + std::to_string(next_mock_module_id);

  auto module = fxl::MakeRefCounted<MockModuleSymbols>("mock_modules.so");
  InjectModule(process, module, "mock_module", effective_load_address, build_id);

  return module;
}

Process* RemoteAPITest::InjectProcess(uint64_t process_koid) {
  auto targets = session().system().GetTargetImpls();
  if (targets.size() != 1u) {
    ADD_FAILURE();
    return nullptr;
  }
  if (targets[0]->GetState() != Target::State::kNone) {
    ADD_FAILURE();
    return nullptr;
  }
  targets[0]->CreateProcessForTesting(process_koid, "test");
  return targets[0]->GetProcess();
}

Thread* RemoteAPITest::InjectThread(uint64_t process_koid, uint64_t thread_koid) {
  debug_ipc::NotifyThreadStarting notify;
  notify.record.id = {.process = process_koid, .thread = thread_koid};
  notify.record.name = fxl::StringPrintf("test %" PRIu64, thread_koid);
  notify.record.state = debug_ipc::ThreadRecord::State::kRunning;

  session_->DispatchNotifyThreadStarting(notify);
  return session_->ThreadImplFromKoid(notify.record.id);
}

void RemoteAPITest::InjectException(const debug_ipc::NotifyException& exception) {
  session_->DispatchNotifyException(exception);
}

void RemoteAPITest::InjectExceptionWithStack(const debug_ipc::NotifyException& exception,
                                             std::vector<std::unique_ptr<Frame>> frames,
                                             bool has_all_frames) {
  ThreadImpl* thread = session_->ThreadImplFromKoid(exception.thread.id);
  FX_CHECK(thread);  // Tests should always pass valid KOIDs.

  // Create an exception record with a thread frame so it's valid. There must be one frame even
  // though the stack will be immediately overwritten.
  debug_ipc::NotifyException modified(exception);
  modified.thread.stack_amount = debug_ipc::ThreadRecord::StackAmount::kMinimal;
  modified.thread.frames.clear();
  if (!frames.empty())
    modified.thread.frames.emplace_back(frames[0]->GetAddress(), frames[0]->GetStackPointer());

  // To manually set the thread state, set the general metadata which will pick up the basic flags
  // and the first stack frame. Then re-set the stack frame with the information passed in by our
  // caller.
  thread->SetMetadata(modified.thread);
  thread->GetStack().SetFramesForTest(std::move(frames), has_all_frames);

  // Normal exception dispatch path, but skipping the metadata (so the metadata set above will
  // stay).
  session_->DispatchNotifyException(modified, false);
}

void RemoteAPITest::InjectExceptionWithStack(
    uint64_t process_koid, uint64_t thread_koid, debug_ipc::ExceptionType exception_type,
    std::vector<std::unique_ptr<Frame>> frames, bool has_all_frames,
    const std::vector<debug_ipc::BreakpointStats>& breakpoints) {
  debug_ipc::NotifyException exception;
  exception.type = exception_type;
  exception.thread.id = {.process = process_koid, .thread = thread_koid};
  exception.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  exception.thread.blocked_reason = debug_ipc::ThreadRecord::BlockedReason::kException;
  exception.hit_breakpoints = breakpoints;

  InjectExceptionWithStack(exception, std::move(frames), has_all_frames);
}

std::unique_ptr<RemoteAPI> RemoteAPITest::GetRemoteAPIImpl() {
  auto remote_api = std::make_unique<MockRemoteAPI>();
  mock_remote_api_ = remote_api.get();
  return remote_api;
}

}  // namespace zxdb
