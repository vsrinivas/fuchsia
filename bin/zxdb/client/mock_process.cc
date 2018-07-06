// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/mock_process.h"

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/memory_dump.h"
#include "garnet/lib/debug_ipc/helper/message_loop.h"
#include "garnet/lib/debug_ipc/records.h"

namespace zxdb {

using debug_ipc::MessageLoop;

MockProcess::MockProcess(Session* session) : Process(session) {}
MockProcess::~MockProcess() = default;

Target* MockProcess::GetTarget() const { return nullptr; }

uint64_t MockProcess::GetKoid() const { return 0; }

const std::string& MockProcess::GetName() const {
  static std::string name("Mock process");
  return name;
}

ProcessSymbols* MockProcess::GetSymbols() { return nullptr; }

void MockProcess::GetModules(
    std::function<void(const Err&, std::vector<debug_ipc::Module>)> cb) {
  MessageLoop::Current()->PostTask(
      [cb]() { cb(Err(), std::vector<debug_ipc::Module>()); });
}

void MockProcess::GetAspace(
    uint64_t address,
    std::function<void(const Err&, std::vector<debug_ipc::AddressRegion>)> cb)
    const {
  MessageLoop::Current()->PostTask(
      [cb]() { cb(Err(), std::vector<debug_ipc::AddressRegion>()); });
}

std::vector<Thread*> MockProcess::GetThreads() const {
  return std::vector<Thread*>();
}

Thread* MockProcess::GetThreadFromKoid(uint64_t koid) { return nullptr; }

void MockProcess::SyncThreads(std::function<void()> cb) {
  MessageLoop::Current()->PostTask([cb]() { cb(); });
}

void MockProcess::Pause() {}

void MockProcess::Continue() {}

void MockProcess::ContinueUntil(const InputLocation& location,
                                std::function<void(const Err&)> cb) {
  MessageLoop::Current()->PostTask([cb]() { cb(Err()); });
}

void MockProcess::ReadMemory(uint64_t address, uint32_t size,
                             std::function<void(const Err&, MemoryDump)> cb) {
  MessageLoop::Current()->PostTask([cb]() { cb(Err(), MemoryDump()); });
}

}  // namespace zxdb
