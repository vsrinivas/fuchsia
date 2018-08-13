// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/mock_symbol_data_provider.h"

#include <inttypes.h>

#include "garnet/bin/zxdb/common/err.h"
#include "garnet/lib/debug_ipc/helper/message_loop.h"
#include "lib/fxl/strings/string_printf.h"

namespace zxdb {

MockSymbolDataProvider::MockSymbolDataProvider() : weak_factory_(this) {}

void MockSymbolDataProvider::AddRegisterValue(int register_num,
                                              bool synchronous,
                                              uint64_t value) {
  regs_[register_num] = RegData(synchronous, value);
}

void MockSymbolDataProvider::AddMemory(uint64_t address,
                                       std::vector<uint8_t> data) {
  mem_[address] = std::move(data);
}

bool MockSymbolDataProvider::GetRegister(int dwarf_register_number,
                                         uint64_t* output) {
  if (dwarf_register_number == kRegisterIP) {
    *output = ip_;
    return true;
  }
  if (dwarf_register_number == kRegisterBP) {
    *output = bp_;
    return true;
  }

  const auto& found = regs_.find(dwarf_register_number);
  if (found == regs_.end())
    return false;

  if (!found->second.synchronous)
    return false;  // Force synchronous query.

  *output = found->second.value;
  return true;
}

void MockSymbolDataProvider::GetRegisterAsync(int dwarf_register_number,
                                              GetRegisterCallback callback) {
  debug_ipc::MessageLoop::Current()->PostTask([
    callback, weak_provider = weak_factory_.GetWeakPtr(), dwarf_register_number
  ]() {
    if (!weak_provider) {
      // Destroyed before callback ready.
      return;
    }

    const auto& found = weak_provider->regs_.find(dwarf_register_number);
    if (found == weak_provider->regs_.end())
      callback(Err("Failed"), 0);
    callback(Err(), found->second.value);
  });
}

void MockSymbolDataProvider::GetMemoryAsync(uint64_t address, uint32_t size,
                                            GetMemoryCallback callback) {
  auto found = mem_.find(address);
  if (found == mem_.end() || size > found->second.size()) {
    debug_ipc::MessageLoop::Current()->PostTask([callback, address]() {
      callback(Err(fxl::StringPrintf("MockSymbolDataProvider::GetMemoryAsync: "
                                     "Memory not found 0x%" PRIx64,
                                     address)),
               std::vector<uint8_t>());
    });
  } else {
    std::vector<uint8_t> subset;
    subset.resize(size);
    memcpy(&subset[0], &found->second[0], size);
    debug_ipc::MessageLoop::Current()->PostTask(
        [callback, subset]() { callback(Err(), subset); });
  }
}

}  // namespace zxdb
