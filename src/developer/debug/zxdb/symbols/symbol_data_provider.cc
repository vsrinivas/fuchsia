// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

namespace {

Err NoProcessErr() { return Err("No process for memory operations."); }
Err NoFrameErr() { return Err("No stack frame to evaluate."); }

}  // namespace

debug_ipc::Arch SymbolDataProvider::GetArch() {
  return debug_ipc::Arch::kUnknown;
}

bool SymbolDataProvider::GetRegister(debug_ipc::RegisterID id,
                                     std::optional<uint64_t>* value) {
  *value = std::nullopt;
  return true;  // Known to be unknown.
}

void SymbolDataProvider::GetRegisterAsync(debug_ipc::RegisterID,
                                          GetRegisterCallback cb) {
  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE, [cb = std::move(cb)]() { cb(NoFrameErr(), 0); });
}

std::optional<uint64_t> SymbolDataProvider::GetFrameBase() {
  return std::nullopt;
}

void SymbolDataProvider::GetFrameBaseAsync(GetRegisterCallback cb) {
  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE, [cb = std::move(cb)]() { cb(NoFrameErr(), 0); });
}

void SymbolDataProvider::GetMemoryAsync(uint64_t address, uint32_t size,
                                        GetMemoryCallback cb) {
  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE,
      [cb = std::move(cb)]() { cb(NoProcessErr(), std::vector<uint8_t>()); });
}

void SymbolDataProvider::WriteMemory(uint64_t address,
                                     std::vector<uint8_t> data,
                                     std::function<void(const Err&)> cb) {
  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE, [cb = std::move(cb)]() { cb(NoProcessErr()); });
}

}  // namespace zxdb
