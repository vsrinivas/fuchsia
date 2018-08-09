// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/frame_symbol_data_provider.h"

#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/memory_dump.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/register.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/lib/debug_ipc/helper/message_loop.h"
#include "lib/fxl/logging.h"

namespace zxdb {

FrameSymbolDataProvider::FrameSymbolDataProvider(Frame* frame)
    : frame_(frame) {}

FrameSymbolDataProvider::~FrameSymbolDataProvider() = default;

void FrameSymbolDataProvider::DisownFrame() { frame_ = nullptr; }

bool FrameSymbolDataProvider::GetRegister(int dwarf_register_number,
                                          uint64_t* output) {
  // TODO(brettw) enable synchronous access if the registers are cached.
  // See GetRegisterAsync().
  return false;
}

void FrameSymbolDataProvider::GetRegisterAsync(
    int dwarf_register_number,
    std::function<void(bool success, uint64_t value)> callback) {
  // TODO(brettw) registers are not available except when this frame is the
  // top stack frame. Currently, there is no management of this and the frame
  // doesn't get notifications when it's topmost or not, and whether the thread
  // has been resumed (both things would invalidate cached registers). As
  // a result, currently we do not cache register values and always do a
  // full async request for each one.
  if (!frame_ || !IsTopFrame()) {
    debug_ipc::MessageLoop::Current()->PostTask(
        [cb = std::move(callback)]() { cb(false, 0); });
    return;
  }

  frame_->GetThread()->GetRegisters(
      [dwarf_register_number, cb = std::move(callback)](
          const Err& err, const RegisterSet& regs) {
        // TODO(brettw) the callback should take an error so we can forward the
        // one from getting the registers.
        uint64_t value = 0;
        if (err.has_error() ||
            !regs.GetRegisterValueFromDWARF(dwarf_register_number, &value)) {
          cb(false, 0);
        } else {
          cb(true, value);
        }
      });
}

void FrameSymbolDataProvider::GetMemoryAsync(
    uint64_t address, uint32_t size,
    std::function<void(const uint8_t* data)> callback) {
  if (!frame_) {
    debug_ipc::MessageLoop::Current()->PostTask(
        [cb = std::move(callback)]() { cb(nullptr); });
    return;
  }

  frame_->GetThread()->GetProcess()->ReadMemory(
      address, size,
      [address, size, cb = std::move(callback)](const Err&, MemoryDump dump) {
        if (dump.address() != address || dump.size() != size) {
          cb(nullptr);
          return;
        }

        // TODO(brettw) Needs implementation and testing. In particular, the
        // returned memory blocks may need to be coalesced into one sequential
        // block to be passed to the callback.
        FXL_NOTREACHED();
        cb(nullptr);
      });
}

bool FrameSymbolDataProvider::IsTopFrame() const {
  if (!frame_)
    return false;
  auto frames = frame_->GetThread()->GetFrames();
  return frames[0] == frame_;
}

}  // namespace zxdb
