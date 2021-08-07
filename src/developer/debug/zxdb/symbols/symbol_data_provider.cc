// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

namespace {

// Helper class to gather all the data and issue the callback when all requests are filled.
class GatherRegisters {
 public:
  GatherRegisters(SymbolDataProvider::GetRegistersCallback cb,
                  std::map<debug::RegisterID, std::vector<uint8_t>> initial_values,
                  int remaining_values)
      : cb_(std::move(cb)),
        values_(std::move(initial_values)),
        remaining_values_(remaining_values) {}

  void SupplyReply(const Err& err, debug::RegisterID reg, std::vector<uint8_t> value) {
    if (failed_)
      return;  // Already issued failure, ignore other data.
    if (err.has_error()) {
      // Reply failure, mark the qhole request as failed.
      failed_ = true;
      cb_(err, {});
      return;
    }

    values_[reg] = std::move(value);
    FX_DCHECK(remaining_values_ > 0);
    remaining_values_--;
    if (remaining_values_ == 0)  // Got all the registers.
      cb_(err, std::move(values_));
  }

 private:
  SymbolDataProvider::GetRegistersCallback cb_;
  bool failed_ = false;
  std::map<debug::RegisterID, std::vector<uint8_t>> values_;
  int remaining_values_;
};

Err NoProcessErr() { return Err("No process for memory operations."); }
Err NoFrameErr() { return Err("No stack frame to evaluate."); }

}  // namespace

debug::Arch SymbolDataProvider::GetArch() { return debug::Arch::kUnknown; }

fxl::RefPtr<SymbolDataProvider> SymbolDataProvider::GetEntryDataProvider() const {
  // Default to not known.
  return fxl::RefPtr<SymbolDataProvider>();
}

std::optional<containers::array_view<uint8_t>> SymbolDataProvider::GetRegister(
    debug::RegisterID id) {
  return containers::array_view<uint8_t>();  // Known to be unknown.
}

void SymbolDataProvider::GetRegisterAsync(debug::RegisterID id, GetRegisterCallback cb) {
  debug::MessageLoop::Current()->PostTask(
      FROM_HERE, [cb = std::move(cb)]() mutable { cb(NoFrameErr(), std::vector<uint8_t>()); });
}

void SymbolDataProvider::GetRegisters(const std::vector<debug::RegisterID>& regs,
                                      GetRegistersCallback cb) {
  // This currently assumes we're only requesting a couple of registers. So for simplicity this just
  // does asynchronous requests for each if they're not available synchronously. If we start
  // requesting many registers, the registers in the same register category could be processed at
  // the same time with many fewer callbacks.

  std::map<debug::RegisterID, std::vector<uint8_t>> sync_values;  // Data read synchronosly.
  std::vector<debug::RegisterID> async_requests;                  // Values to collect later.

  // Fill in all synchronously known registers and schedule callbacks for the rest.
  for (debug::RegisterID reg : regs) {
    if (std::optional<containers::array_view<uint8_t>> view_or = GetRegister(reg)) {
      sync_values[reg] = std::vector<uint8_t>(view_or->begin(), view_or->end());
    } else {
      async_requests.push_back(reg);
    }
  }

  if (async_requests.empty()) {
    cb(Err(), std::move(sync_values));
    return;
  }

  // Schedule the async requests.
  auto gather = std::make_shared<GatherRegisters>(std::move(cb), std::move(sync_values),
                                                  async_requests.size());
  for (debug::RegisterID reg : async_requests) {
    GetRegisterAsync(reg, [gather, reg](const Err& err, std::vector<uint8_t> value) mutable {
      gather->SupplyReply(err, reg, std::move(value));
    });
  }
}

void SymbolDataProvider::WriteRegister(debug::RegisterID id, std::vector<uint8_t> data,
                                       WriteCallback cb) {
  debug::MessageLoop::Current()->PostTask(FROM_HERE,
                                          [cb = std::move(cb)]() mutable { cb(NoFrameErr()); });
}

std::optional<uint64_t> SymbolDataProvider::GetFrameBase() { return std::nullopt; }

void SymbolDataProvider::GetFrameBaseAsync(GetFrameBaseCallback cb) {
  debug::MessageLoop::Current()->PostTask(FROM_HERE,
                                          [cb = std::move(cb)]() mutable { cb(NoFrameErr(), 0); });
}

uint64_t SymbolDataProvider::GetCanonicalFrameAddress() const { return 0; }

std::optional<uint64_t> SymbolDataProvider::GetDebugAddressForContext(
    const SymbolContext& /*context*/) const {
  return std::nullopt;
}

void SymbolDataProvider::GetTLSSegment(const SymbolContext& /*symbol_context*/,
                                       GetTLSSegmentCallback cb) {
  cb(NoProcessErr());
}

void SymbolDataProvider::GetMemoryAsync(uint64_t address, uint32_t size, GetMemoryCallback cb) {
  debug::MessageLoop::Current()->PostTask(
      FROM_HERE, [cb = std::move(cb)]() mutable { cb(NoProcessErr(), std::vector<uint8_t>()); });
}

void SymbolDataProvider::WriteMemory(uint64_t address, std::vector<uint8_t> data,
                                     WriteCallback cb) {
  debug::MessageLoop::Current()->PostTask(FROM_HERE,
                                          [cb = std::move(cb)]() mutable { cb(NoProcessErr()); });
}

}  // namespace zxdb
