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

std::optional<uint128_t> FindRegister(const std::vector<Register>& regs, RegisterID id) {
  for (const auto& r : regs) {
    // TODO(brettw) handle non-canonical registers which are a subset of data from one of the
    // canonical ones in the |regs| vector.
    if (r.id == id) {
      // Currently we expect all general registers to be <= 128 bits.
      if (r.data.size() > 0 && r.data.size() <= sizeof(uint128_t))
        return r.GetValue();
      return std::nullopt;
    }
  }

  return std::nullopt;
}

}  // namespace

FrameSymbolDataProvider::FrameSymbolDataProvider(Frame* frame)
    : ProcessSymbolDataProvider(frame->GetThread()->GetProcess()), frame_(frame) {}

FrameSymbolDataProvider::~FrameSymbolDataProvider() = default;

void FrameSymbolDataProvider::Disown() {
  ProcessSymbolDataProvider::Disown();
  frame_ = nullptr;
}

bool FrameSymbolDataProvider::GetRegister(RegisterID id, std::optional<uint128_t>* value) {
  FXL_DCHECK(id != RegisterID::kUnknown);

  *value = std::nullopt;
  if (!frame_)
    return true;  // Synchronously know we don't have the value.

  RegisterCategory category = debug_ipc::RegisterIDToCategory(id);
  FXL_DCHECK(category != RegisterCategory::kNone);

  const std::vector<Register>* regs = frame_->GetRegisterCategorySync(category);
  if (regs) {
    // Have this register synchronously (or know we can't have it).
    *value = FindRegister(*regs, id);
    return true;  // Result known synchronously.
  }

  return false;
}

void FrameSymbolDataProvider::GetRegisterAsync(RegisterID id, GetRegisterCallback cb) {
  if (!frame_) {
    // Frame deleted out from under us.
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [id, cb = std::move(cb)]() mutable { cb(RegisterUnavailableErr(id), 0); });
    return;
  }

  RegisterCategory category = debug_ipc::RegisterIDToCategory(id);
  FXL_DCHECK(category != RegisterCategory::kNone);

  frame_->GetRegisterCategoryAsync(
      category,
      [id, cb = std::move(cb)](const Err& err, const std::vector<Register>& regs) mutable {
        if (err.has_error())
          return cb(err, 0);

        if (std::optional<uint128_t> value = FindRegister(regs, id))
          cb(Err(), *value);
        else
          cb(RegisterUnavailableErr(id), 0);
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
