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

Err RegisterUnavailableErr(debug_ipc::RegisterID id) {
  return Err(fxl::StringPrintf("Register %s unavailable.",
                               debug_ipc::RegisterIDToString(id)));
}

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
  FXL_DCHECK(id != debug_ipc::RegisterID::kUnknown);

  *value = std::nullopt;
  if (!frame_)
    return true;  // Synchronously know we don't have the value.

  if (!debug_ipc::IsGeneralRegister(id))
    return false;  // Don't have non-general register synchronously.

  for (const auto& r : frame_->GetGeneralRegisters()) {
    if (r.id() == id) {
      // Currently we expect all general registers to be <= 64 bits and we're
      // returning the value in 64 bits.
      if (r.size() > 0 && r.size() <= sizeof(uint64_t))
        *value = r.GetValue();
      return true;
    }
  }

  // Getting here means a general register we don't have was requsted and this
  // can never be provided. Return "synchronously complete" (true) and leave
  // *value as nullopt to indicate this state.
  return true;
}

void FrameSymbolDataProvider::GetRegisterAsync(debug_ipc::RegisterID id,
                                               GetRegisterCallback callback) {
  if (!frame_) {
    // Frame deleted out from under us.
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [id, cb = std::move(callback)]() {
          cb(RegisterUnavailableErr(id), 0);
        });
    return;
  }

  // General registers are stored on the frame and are synchronously available.
  // Return them (or error if not available) if somebody requests them
  // asynchronously.
  if (debug_ipc::IsGeneralRegister(id)) {
    std::optional<uint64_t> value;
    GetRegister(id, &value);

    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [id, value, cb = std::move(callback)]() {
          if (value)
            cb(Err(), *value);
          else
            cb(RegisterUnavailableErr(id), 0);
        });
    return;
  }

  // if (IsInTopPhysicalFrame()) {
  //  TODO Support for dynamically fetching other registers like floating point
  //  and vector registers here.
  //}

  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE, [id, cb = std::move(callback)]() {
        cb(Err(fxl::StringPrintf("Register %s unavailable.",
                                 debug_ipc::RegisterIDToString(id))),
           0);
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
