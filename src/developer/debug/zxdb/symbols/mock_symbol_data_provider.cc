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

void MockSymbolDataProvider::AddRegisterValue(debug_ipc::RegisterID id, bool synchronous,
                                              uint64_t value) {
  std::vector<uint8_t> data;
  data.resize(sizeof(value));
  memcpy(&data[0], &value, sizeof(value));
  regs_[id] = RegData(synchronous, std::move(data));
}

void MockSymbolDataProvider::AddRegisterValue(debug_ipc::RegisterID id, bool synchronous,
                                              std::vector<uint8_t> value) {
  regs_[id] = RegData(synchronous, std::move(value));
}

void MockSymbolDataProvider::AddMemory(uint64_t address, std::vector<uint8_t> data) {
  memory_.AddMemory(address, std::move(data));
}

debug_ipc::Arch MockSymbolDataProvider::GetArch() { return debug_ipc::Arch::kArm64; }

std::optional<containers::array_view<uint8_t>> MockSymbolDataProvider::GetRegister(
    debug_ipc::RegisterID id) {
  if (GetSpecialRegisterType(id) == debug_ipc::SpecialRegisterType::kIP) {
    const uint8_t* ip_as_char = reinterpret_cast<const uint8_t*>(&ip_);
    return containers::array_view(ip_as_char, ip_as_char + sizeof(ip_));
  }

  const auto& found = regs_.find(id);
  if (found == regs_.end())
    return containers::array_view<uint8_t>();  // Known to be unknown.

  if (!found->second.synchronous)
    return std::nullopt;

  return found->second.value;
}

void MockSymbolDataProvider::GetRegisterAsync(debug_ipc::RegisterID id,
                                              GetRegisterCallback callback) {
  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE,
      [callback = std::move(callback), weak_provider = weak_factory_.GetWeakPtr(), id]() mutable {
        if (!weak_provider) {
          // Destroyed before callback ready.
          return;
        }

        const auto& found = weak_provider->regs_.find(id);
        if (found == weak_provider->regs_.end())
          callback(Err("Failed"), {});
        callback(Err(), found->second.value);
      });
}

void MockSymbolDataProvider::WriteRegister(debug_ipc::RegisterID id, std::vector<uint8_t> data,
                                           WriteCallback cb) {
  register_writes_.emplace_back(id, std::move(data));

  // Declare success.
  debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE,
                                              [cb = std::move(cb)]() mutable { cb(Err()); });
}

std::optional<uint64_t> MockSymbolDataProvider::GetFrameBase() { return bp_; }

void MockSymbolDataProvider::GetFrameBaseAsync(GetFrameBaseCallback callback) {
  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE,
      [callback = std::move(callback), weak_provider = weak_factory_.GetWeakPtr()]() mutable {
        if (!weak_provider) {
          // Destroyed before callback ready.
          return;
        }
        callback(Err(), weak_provider->bp_);
      });
}

std::optional<uint64_t> MockSymbolDataProvider::GetDebugAddressForContext(
    const SymbolContext&) const {
  return 0;
}

void MockSymbolDataProvider::GetTLSSegment(const SymbolContext& /*symbol_context*/,
                                           GetTLSSegmentCallback cb) {
  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE, [cb = std::move(cb), tls_segment = tls_segment_]() mutable { cb(tls_segment); });
}

uint64_t MockSymbolDataProvider::GetCanonicalFrameAddress() const { return cfa_; }

void MockSymbolDataProvider::GetMemoryAsync(uint64_t address, uint32_t size,
                                            GetMemoryCallback callback) {
  std::vector<uint8_t> result = memory_.ReadMemory(address, size);
  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE, [callback = std::move(callback), result]() mutable { callback(Err(), result); });
}

void MockSymbolDataProvider::WriteMemory(uint64_t address, std::vector<uint8_t> data,
                                         WriteCallback cb) {
  memory_writes_.emplace_back(address, std::move(data));

  // Declare success.
  debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE,
                                              [cb = std::move(cb)]() mutable { cb(Err()); });
}

}  // namespace zxdb
