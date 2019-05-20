// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/frame_symbol_data_provider.h"

#include <inttypes.h>

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/memory_dump.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/register.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

Err CallFrameDestroyedErr() { return Err("Call frame destroyed."); }

}  // namespace

FrameSymbolDataProvider::FrameSymbolDataProvider(Frame* frame)
    : ProcessSymbolDataProvider(frame->GetThread()->GetProcess()),
      frame_(frame) {}

FrameSymbolDataProvider::~FrameSymbolDataProvider() = default;

void FrameSymbolDataProvider::Disown() {
  ProcessSymbolDataProvider::Disown();
  frame_ = nullptr;
}

bool FrameSymbolDataProvider::GetRegister(debug_ipc::RegisterID id,
                                          std::optional<uint64_t>* value) {
  *value = std::nullopt;
  if (!frame_)
    return true;  // Synchronously know we don't have the value.

  // Some common registers are known without having to do a register request.
  switch (debug_ipc::GetSpecialRegisterType(id)) {
    case debug_ipc::SpecialRegisterType::kIP:
      *value = frame_->GetAddress();
      return true;
    case debug_ipc::SpecialRegisterType::kSP:
      *value = frame_->GetStackPointer();
      return true;
    case debug_ipc::SpecialRegisterType::kNone:
      break;
  }

  // TODO(brettw) enable synchronous access if the registers are cached.
  // See GetRegisterAsync().
  return false;
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
