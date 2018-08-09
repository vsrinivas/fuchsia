// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/mock_symbol_data_provider.h"

#include "garnet/lib/debug_ipc/helper/message_loop.h"

namespace zxdb {

MockSymbolDataProvider::MockSymbolDataProvider() : weak_factory_(this) {}

void MockSymbolDataProvider::AddRegisterValue(int register_num,
                                              bool synchronous,
                                              uint64_t value) {
  regs_[register_num] = RegData(synchronous, value);
}

bool MockSymbolDataProvider::GetRegister(int dwarf_register_number,
                                         uint64_t* output) {
  const auto& found = regs_.find(dwarf_register_number);
  if (found == regs_.end())
    return false;

  if (!found->second.synchronous)
    return false;  // Force synchronous query.

  *output = found->second.value;
  return true;
}

void MockSymbolDataProvider::GetRegisterAsync(
    int dwarf_register_number,
    std::function<void(bool success, uint64_t value)> callback) {
  debug_ipc::MessageLoop::Current()->PostTask(
      [callback, weak_provider = weak_factory_.GetWeakPtr(),
       dwarf_register_number]() {
        if (!weak_provider) {
          // Destroyed before callback ready.
          return;
        }

        const auto& found = weak_provider->regs_.find(dwarf_register_number);
        if (found == weak_provider->regs_.end())
          callback(false, 0);
        callback(true, found->second.value);
      });
}

void MockSymbolDataProvider::GetMemoryAsync(
    uint64_t address, uint32_t size,
    std::function<void(const uint8_t* data)> callback) {
  // TODO(brettw) implement this.
}

}  // namespace zxdb
