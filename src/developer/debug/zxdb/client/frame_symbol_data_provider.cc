// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/frame_symbol_data_provider.h"

#include <inttypes.h>

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/memory_dump.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

using debug_ipc::Register;
using debug_ipc::RegisterCategory;
using debug_ipc::RegisterID;

Err CallFrameDestroyedErr() { return Err("Call frame destroyed."); }

Err RegisterUnavailableErr(debug_ipc::RegisterID id) {
  return Err(fxl::StringPrintf("Register %s unavailable.", debug_ipc::RegisterIDToString(id)));
}

}  // namespace

FrameSymbolDataProvider::FrameSymbolDataProvider(Frame* frame)
    : ProcessSymbolDataProvider(frame->GetThread()->GetProcess()), frame_(frame) {}

FrameSymbolDataProvider::~FrameSymbolDataProvider() = default;

void FrameSymbolDataProvider::Disown() {
  ProcessSymbolDataProvider::Disown();
  frame_ = nullptr;
}

std::optional<containers::array_view<uint8_t>> FrameSymbolDataProvider::GetRegister(RegisterID id) {
  FXL_DCHECK(id != RegisterID::kUnknown);
  if (!frame_)
    return containers::array_view<uint8_t>();  // Synchronously know we don't have the value.

  RegisterCategory category = debug_ipc::RegisterIDToCategory(id);
  FXL_DCHECK(category != RegisterCategory::kNone);

  const std::vector<Register>* regs = frame_->GetRegisterCategorySync(category);
  if (!regs)
    return std::nullopt;  // Not known synchronously.

  // Have this register synchronously (or know we can't have it).
  return debug_ipc::GetRegisterData(*regs, id);
}

void FrameSymbolDataProvider::GetRegisterAsync(RegisterID id, GetRegisterCallback cb) {
  if (!frame_) {
    // Frame deleted out from under us.
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [id, cb = std::move(cb)]() mutable { cb(RegisterUnavailableErr(id), {}); });
    return;
  }

  RegisterCategory category = debug_ipc::RegisterIDToCategory(id);
  FXL_DCHECK(category != RegisterCategory::kNone);

  frame_->GetRegisterCategoryAsync(
      category,
      [id, cb = std::move(cb)](const Err& err, const std::vector<Register>& regs) mutable {
        if (err.has_error())
          return cb(err, {});

        auto found_reg_data = debug_ipc::GetRegisterData(regs, id);
        if (found_reg_data.empty())
          cb(RegisterUnavailableErr(id), {});
        else
          cb(Err(), std::vector<uint8_t>(found_reg_data.begin(), found_reg_data.end()));
      });
}

std::optional<uint64_t> FrameSymbolDataProvider::GetFrameBase() {
  if (!frame_)
    return std::nullopt;
  return frame_->GetBasePointer();
}

void FrameSymbolDataProvider::GetFrameBaseAsync(GetFrameBaseCallback cb) {
  if (!frame_) {
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [cb = std::move(cb)]() mutable { cb(CallFrameDestroyedErr(), 0); });
    return;
  }

  frame_->GetBasePointerAsync([cb = std::move(cb)](uint64_t value) mutable { cb(Err(), value); });
}

uint64_t FrameSymbolDataProvider::GetCanonicalFrameAddress() const {
  if (!frame_)
    return 0;
  return frame_->GetCanonicalFrameAddress();
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
