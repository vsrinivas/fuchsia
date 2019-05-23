// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/mock_symbol_data_provider.h"

#include <inttypes.h>

#include <algorithm>

#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

MockSymbolDataProvider::MockSymbolDataProvider() : weak_factory_(this) {}

void MockSymbolDataProvider::AddRegisterValue(debug_ipc::RegisterID id,
                                              bool synchronous,
                                              uint64_t value) {
  regs_[id] = RegData(synchronous, value);
}

void MockSymbolDataProvider::AddMemory(uint64_t address,
                                       std::vector<uint8_t> data) {
  memory_.AddMemory(address, std::move(data));
}

debug_ipc::Arch MockSymbolDataProvider::GetArch() {
  return debug_ipc::Arch::kArm64;
}

bool MockSymbolDataProvider::GetRegister(debug_ipc::RegisterID id,
                                         std::optional<uint64_t>* value) {
  *value = std::nullopt;

  if (GetSpecialRegisterType(id) == debug_ipc::SpecialRegisterType::kIP) {
    *value = ip_;
    return true;
  }

  const auto& found = regs_.find(id);
  if (found == regs_.end())
    return true;  // Known to be unknown.

  if (!found->second.synchronous)
    return false;

  *value = found->second.value;
  return true;
}

void MockSymbolDataProvider::GetRegisterAsync(debug_ipc::RegisterID id,
                                              GetRegisterCallback callback) {
  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE, [callback, weak_provider = weak_factory_.GetWeakPtr(), id]() {
        if (!weak_provider) {
          // Destroyed before callback ready.
          return;
        }

        const auto& found = weak_provider->regs_.find(id);
        if (found == weak_provider->regs_.end())
          callback(Err("Failed"), 0);
        callback(Err(), found->second.value);
      });
}

std::optional<uint64_t> MockSymbolDataProvider::GetFrameBase() { return bp_; }

void MockSymbolDataProvider::GetFrameBaseAsync(GetRegisterCallback callback) {
  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE, [callback, weak_provider = weak_factory_.GetWeakPtr()]() {
        if (!weak_provider) {
          // Destroyed before callback ready.
          return;
        }
        callback(Err(), weak_provider->bp_);
      });
}

void MockSymbolDataProvider::GetMemoryAsync(uint64_t address, uint32_t size,
                                            GetMemoryCallback callback) {
  std::vector<uint8_t> result = memory_.ReadMemory(address, size);
  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE, [callback, result]() { callback(Err(), result); });
}

void MockSymbolDataProvider::WriteMemory(uint64_t address,
                                         std::vector<uint8_t> data,
                                         std::function<void(const Err&)> cb) {
  memory_writes_.emplace_back(address, std::move(data));

  // Declare success.
  debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE, [cb]() { cb(Err()); });
}

}  // namespace zxdb
