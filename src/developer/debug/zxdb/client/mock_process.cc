// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/mock_process.h"

#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/client/memory_dump.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"

namespace zxdb {

using debug_ipc::MessageLoop;

MockProcess::MockProcess(Session* session) : Process(session, Process::StartType::kLaunch) {}
MockProcess::~MockProcess() = default;

Target* MockProcess::GetTarget() const { return nullptr; }

uint64_t MockProcess::GetKoid() const { return 0; }

const std::string& MockProcess::GetName() const {
  static std::string name("Mock process");
  return name;
}

ProcessSymbols* MockProcess::GetSymbols() { return symbols_; }

void MockProcess::GetModules(fit::callback<void(const Err&, std::vector<debug_ipc::Module>)> cb) {
  MessageLoop::Current()->PostTask(
      FROM_HERE, [cb = std::move(cb)]() mutable { cb(Err(), std::vector<debug_ipc::Module>()); });
}

void MockProcess::GetAspace(
    uint64_t address,
    fit::callback<void(const Err&, std::vector<debug_ipc::AddressRegion>)> cb) const {
  MessageLoop::Current()->PostTask(FROM_HERE, [cb = std::move(cb)]() mutable {
    cb(Err(), std::vector<debug_ipc::AddressRegion>());
  });
}

std::vector<Thread*> MockProcess::GetThreads() const { return std::vector<Thread*>(); }

Thread* MockProcess::GetThreadFromKoid(uint64_t koid) { return nullptr; }

void MockProcess::SyncThreads(fit::callback<void()> cb) {
  MessageLoop::Current()->PostTask(FROM_HERE, [cb = std::move(cb)]() mutable { cb(); });
}

void MockProcess::Pause(fit::callback<void()> on_paused) {
  MessageLoop::Current()->PostTask(FROM_HERE,
                                   [on_paused = std::move(on_paused)]() mutable { on_paused(); });
}

void MockProcess::Continue(bool forward_exceptions) {}

void MockProcess::ContinueUntil(std::vector<InputLocation> location,
                                fit::callback<void(const Err&)> cb) {
  MessageLoop::Current()->PostTask(FROM_HERE, [cb = std::move(cb)]() mutable { cb(Err()); });
}

fxl::RefPtr<SymbolDataProvider> MockProcess::GetSymbolDataProvider() const {
  return fxl::MakeRefCounted<SymbolDataProvider>();
}

void MockProcess::GetTLSHelpers(GetTLSHelpersCallback cb) {
  if (tls_helpers_) {
    return cb(&*tls_helpers_);
  } else {
    return cb(Err("MockProcess was not provided with TLS Helpers."));
  }
}

void MockProcess::ReadMemory(uint64_t address, uint32_t size,
                             fit::callback<void(const Err&, MemoryDump)> cb) {
  MessageLoop::Current()->PostTask(FROM_HERE,
                                   [cb = std::move(cb)]() mutable { cb(Err(), MemoryDump()); });
}

void MockProcess::WriteMemory(uint64_t address, std::vector<uint8_t> data,
                              fit::callback<void(const Err&)> callback) {
  // Currently always just report success.
  MessageLoop::Current()->PostTask(FROM_HERE, [cb = std::move(callback)]() mutable { cb(Err()); });
}

void MockProcess::LoadInfoHandleTable(
    fit::callback<void(ErrOr<std::vector<debug_ipc::InfoHandleExtended>> handles)> cb) {
  MessageLoop::Current()->PostTask(FROM_HERE, [cb = std::move(cb)]() mutable { cb(Err()); });
}

}  // namespace zxdb
