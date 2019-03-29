// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/frame_symbol_data_provider.h"

#include <inttypes.h>

#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/memory_dump.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/register.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

namespace {

Err CallFrameDestroyedErr() { return Err("Call frame destroyed."); }

debug_ipc::Arch ArchForFrame(Frame* frame) {
  if (!frame)
    return debug_ipc::Arch::kUnknown;
  return frame->GetThread()->session()->arch();
}

}  // namespace

FrameSymbolDataProvider::FrameSymbolDataProvider(Frame* frame)
    : frame_(frame), arch_(ArchForFrame(frame)) {}

FrameSymbolDataProvider::~FrameSymbolDataProvider() = default;

void FrameSymbolDataProvider::DisownFrame() { frame_ = nullptr; }

debug_ipc::Arch FrameSymbolDataProvider::GetArch() { return arch_; }

std::optional<uint64_t> FrameSymbolDataProvider::GetRegister(
    debug_ipc::RegisterID id) {
  if (!frame_)
    return std::nullopt;

  // Some common registers are known without having to do a register request.
  switch (debug_ipc::GetSpecialRegisterType(id)) {
    case debug_ipc::SpecialRegisterType::kIP:
      return frame_->GetAddress();
    case debug_ipc::SpecialRegisterType::kSP:
      return frame_->GetStackPointer();
    case debug_ipc::SpecialRegisterType::kBP:
      return frame_->GetBasePointerRegister();
    case debug_ipc::SpecialRegisterType::kNone:
      break;
  }

  // TODO(brettw) enable synchronous access if the registers are cached.
  // See GetRegisterAsync().
  return std::nullopt;
}

void FrameSymbolDataProvider::GetRegisterAsync(debug_ipc::RegisterID id,
                                               GetRegisterCallback callback) {
  // TODO(brettw) registers are not available except when this frame is the
  // top stack frame. Currently, there is no management of this and the frame
  // doesn't get notifications when it's topmost or not, and whether the thread
  // has been resumed (both things would invalidate cached registers). As
  // a result, currently we do not cache register values and always do a
  // full async request for each one.
  //
  // Additionally, some registers can be made available in non-top stack
  // frames. Libunwind should be able to tell us the saved registers for older
  // stack frames.
  if (!frame_ || !IsInTopPhysicalFrame()) {
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [id, cb = std::move(callback)]() {
          cb(Err(fxl::StringPrintf("Register %s unavailable.",
                                   debug_ipc::RegisterIDToString(id))),
             0);
        });
    return;
  }

  // We only need the general registers.
  // TODO: Other categories will need to be supported here (eg. floating point).
  frame_->GetThread()->ReadRegisters(
      {debug_ipc::RegisterCategory::Type::kGeneral},
      [id, cb = std::move(callback)](const Err& err, const RegisterSet& regs) {
        if (err.has_error()) {
          cb(err, 0);
        } else if (auto reg = regs[id]) {
          cb(Err(), reg->GetValue());  // Success.
        } else {
          cb(Err(fxl::StringPrintf("Register %s unavailable.",
                                   debug_ipc::RegisterIDToString(id))),
             0);
        }
      });
}

std::optional<uint64_t> FrameSymbolDataProvider::GetFrameBase() {
  if (!frame_)
    return std::nullopt;
  return frame_->GetBasePointer();
}

void FrameSymbolDataProvider::GetFrameBaseAsync(GetRegisterCallback cb) {
  if (!frame_) {
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [cb = std::move(cb)]() { cb(CallFrameDestroyedErr(), 0); });
    return;
  }

  frame_->GetBasePointerAsync(
      [cb = std::move(cb)](uint64_t value) { cb(Err(), value); });
}

void FrameSymbolDataProvider::GetMemoryAsync(uint64_t address, uint32_t size,
                                             GetMemoryCallback callback) {
  if (!frame_) {
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [cb = std::move(callback)]() {
          cb(CallFrameDestroyedErr(), std::vector<uint8_t>());
        });
    return;
  }

  // Mistakes may make extremely large memory requests which can OOM the
  // system. Prevent those.
  if (size > 1024 * 1024) {
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [address, size, cb = std::move(callback)]() {
          cb(Err(fxl::StringPrintf("Memory request for %u bytes at 0x%" PRIx64
                                   " is too large.",
                                   size, address)),
             std::vector<uint8_t>());
        });
    return;
  }

  frame_->GetThread()->GetProcess()->ReadMemory(
      address, size,
      [address, size, cb = std::move(callback)](const Err& err,
                                                MemoryDump dump) {
        if (err.has_error()) {
          cb(err, std::vector<uint8_t>());
          return;
        }

        FXL_DCHECK(size == 0 || dump.address() == address);
        FXL_DCHECK(dump.size() == size);
        if (dump.blocks().size() == 1 ||
            (dump.blocks().size() > 1 && !dump.blocks()[1].valid)) {
          // Common case: came back as one block OR it read until an invalid
          // memory boundary and the second block is invalid.
          //
          // In both these cases we can directly return the first data block.
          // We don't have to check the first block's valid flag since if it's
          // not valid it will be empty, which is what our API specifies.
          cb(Err(), std::move(dump.blocks()[0].data));
        } else {
          // The debug agent doesn't guarantee that a memory dump will exist in
          // only one block even if the memory is all valid. Flatten all
          // contiguous valid regions to a single buffer.
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

void FrameSymbolDataProvider::WriteMemory(uint64_t address,
                                          std::vector<uint8_t> data,
                                          std::function<void(const Err&)> cb) {
  if (!frame_) {
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [cb = std::move(cb)]() { cb(CallFrameDestroyedErr()); });
    return;
  }
  frame_->GetThread()->GetProcess()->WriteMemory(address, std::move(data),
                                                 std::move(cb));
}

bool FrameSymbolDataProvider::IsInTopPhysicalFrame() const {
  if (!frame_)
    return false;
  const Stack& stack = frame_->GetThread()->GetStack();
  if (stack.empty())
    return false;

  // Search for the first physical frame, and return true if it or anything
  // above it matches the current frame.
  for (size_t i = 0; i < stack.size(); i++) {
    if (stack[i] == frame_)
      return true;
    if (!stack[i]->IsInline())
      break;
  }
  return false;
}

}  // namespace zxdb
