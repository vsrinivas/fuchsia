// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/mock_symbol_data_provider.h"

#include <inttypes.h>

#include <algorithm>

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

std::optional<uint64_t> MockSymbolDataProvider::GetRegister(
    int dwarf_register_number) {
  if (dwarf_register_number == kRegisterIP)
    return ip_;

  const auto& found = regs_.find(dwarf_register_number);
  if (found == regs_.end())
    return std::nullopt;

  if (!found->second.synchronous)
    return std::nullopt;  // Force synchronous query.

  return found->second.value;
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

std::optional<uint64_t> MockSymbolDataProvider::GetFrameBase() { return bp_; }

void MockSymbolDataProvider::GetFrameBaseAsync(GetRegisterCallback callback) {
  debug_ipc::MessageLoop::Current()->PostTask(
      [ callback, weak_provider = weak_factory_.GetWeakPtr() ]() {
        if (!weak_provider) {
          // Destroyed before callback ready.
          return;
        }
        callback(Err(), weak_provider->bp_);
      });
}

void MockSymbolDataProvider::GetMemoryAsync(uint64_t address, uint32_t size,
                                            GetMemoryCallback callback) {
  auto found = FindBlockForAddress(address);
  if (found == mem_.end()) {
    debug_ipc::MessageLoop::Current()->PostTask([callback]() {
      // The API states that invalid memory is not an error, it just does a
      // short read.
      callback(Err(), std::vector<uint8_t>());
    });
  } else {
    size_t offset = address - found->first;

    uint32_t size_to_return =
        std::min(size, static_cast<uint32_t>(found->second.size() - offset));

    std::vector<uint8_t> subset;
    subset.resize(size_to_return);
    memcpy(&subset[0], &found->second[offset], size_to_return);
    debug_ipc::MessageLoop::Current()->PostTask(
        [callback, subset]() { callback(Err(), subset); });
  }
}

MockSymbolDataProvider::RegisteredMemory::const_iterator
MockSymbolDataProvider::FindBlockForAddress(uint64_t address) const {
  // Finds the first block >= address.
  auto found = mem_.lower_bound(address);

  // We need the first block <= address.
  if (found != mem_.end() && found->first == address)
    return found;  // Got exact match.

  // Now find the first block < address.
  if (found == mem_.begin())
    return mem_.end();  // Nothing before the address.

  --found;
  if (address >= found->first + found->second.size())
    return mem_.end();  // Address is after this range.
  return found;
}

}  // namespace zxdb
