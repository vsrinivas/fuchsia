// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/process_symbol_data_provider.h"

#include <inttypes.h>

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/memory_dump.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"
#include "src/developer/debug/zxdb/symbols/loaded_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

Err ProcessDestroyedErr() { return Err("Process destroyed."); }

debug_ipc::Arch ArchForProcess(Process* process) {
  if (!process)
    return debug_ipc::Arch::kUnknown;
  return process->session()->arch();
}

}  // namespace

ProcessSymbolDataProvider::ProcessSymbolDataProvider(Process* process)
    : process_(process), arch_(ArchForProcess(process)) {}

ProcessSymbolDataProvider::~ProcessSymbolDataProvider() = default;

void ProcessSymbolDataProvider::Disown() { process_ = nullptr; }

debug_ipc::Arch ProcessSymbolDataProvider::GetArch() { return arch_; }

void ProcessSymbolDataProvider::GetMemoryAsync(uint64_t address, uint32_t size,
                                               GetMemoryCallback callback) {
  if (!process_) {
    debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE, [cb = std::move(callback)]() mutable {
      cb(ProcessDestroyedErr(), std::vector<uint8_t>());
    });
    return;
  }

  // Mistakes may make extremely large memory requests which can OOM the system. Prevent those.
  if (size > 1024 * 1024) {
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [address, size, cb = std::move(callback)]() mutable {
          cb(Err(fxl::StringPrintf("Memory request for %u bytes at 0x%" PRIx64 " is too large.",
                                   size, address)),
             std::vector<uint8_t>());
        });
    return;
  }

  process_->ReadMemory(
      address, size,
      [address, size, cb = std::move(callback)](const Err& err, MemoryDump dump) mutable {
        if (err.has_error()) {
          cb(err, std::vector<uint8_t>());
          return;
        }

        FXL_DCHECK(size == 0 || dump.address() == address);
        FXL_DCHECK(dump.size() == size);
        if (dump.blocks().size() == 1 || (dump.blocks().size() > 1 && !dump.blocks()[1].valid)) {
          // Common case: came back as one block OR it read until an invalid memory boundary and the
          // second block is invalid.
          //
          // In both these cases we can directly return the first data block. We don't have to check
          // the first block's valid flag since if it's not valid it will be empty, which is what
          // our API specifies.
          cb(Err(), std::move(dump.blocks()[0].data));
        } else {
          // The debug agent doesn't guarantee that a memory dump will exist in only one block even
          // if the memory is all valid. Flatten all contiguous valid regions to a single buffer.
          std::vector<uint8_t> flat;
          flat.reserve(dump.size());
          for (const auto block : dump.blocks()) {
            if (!block.valid)
              break;
            flat.insert(flat.end(), block.data.begin(), block.data.end());
          }
          cb(Err(), std::move(flat));
        }
      });
}

void ProcessSymbolDataProvider::WriteMemory(uint64_t address, std::vector<uint8_t> data,
                                            WriteCallback cb) {
  if (!process_) {
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [cb = std::move(cb)]() mutable { cb(ProcessDestroyedErr()); });
    return;
  }
  process_->WriteMemory(address, std::move(data), std::move(cb));
}

std::optional<uint64_t> ProcessSymbolDataProvider::GetDebugAddressForContext(
    const SymbolContext& context) const {
  if (process_) {
    if (auto syms = process_->GetSymbols()) {
      if (auto lms = syms->GetModuleForAddress(context.load_address())) {
        return lms->debug_address();
      }
    }
  }

  return std::nullopt;
}

}  // namespace zxdb
