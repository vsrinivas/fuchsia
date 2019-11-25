// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FRAME_IMPL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FRAME_IMPL_H_

#include <memory>
#include <optional>

#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/lib/fxl/memory/ref_counted.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class DwarfExprEval;
class EvalContextImpl;
class FrameSymbolDataProvider;
class Thread;

// A frame is lazily symbolized.
class FrameImpl final : public Frame {
 public:
  FrameImpl(Thread* thread, const debug_ipc::StackFrame& stack_frame, Location location);
  ~FrameImpl() override;

  // Frame implementation.
  Thread* GetThread() const override;
  bool IsInline() const override;
  const Frame* GetPhysicalFrame() const override;
  const Location& GetLocation() const override;
  uint64_t GetAddress() const override;
  const std::vector<debug_ipc::Register>* GetRegisterCategorySync(
      debug_ipc::RegisterCategory category) const override;
  void GetRegisterCategoryAsync(
      debug_ipc::RegisterCategory category,
      fit::function<void(const Err&, const std::vector<debug_ipc::Register>&)> cb) override;
  void WriteRegister(debug_ipc::RegisterID id, std::vector<uint8_t> data,
                     fit::callback<void(const Err&)> cb) override;
  std::optional<uint64_t> GetBasePointer() const override;
  void GetBasePointerAsync(fit::callback<void(uint64_t bp)> cb) override;
  uint64_t GetStackPointer() const override;
  uint64_t GetCanonicalFrameAddress() const override;
  fxl::RefPtr<SymbolDataProvider> GetSymbolDataProvider() const override;
  fxl::RefPtr<EvalContext> GetEvalContext() const override;
  bool IsAmbiguousInlineLocation() const override;

 private:
  // Returns true if this is in the topmsot physical frame. This includes all inline frames
  // created inside the topmost physical frame.
  bool IsInTopmostPhysicalFrame() const;

  void EnsureSymbolized() const;

  // Ensures that the base pointer evaluation has at least started. If this returns true the
  // computed_base_pointer_ is valid and can be used. If this returns false, the computation of the
  // base pointer will be pending. Callers can add a callback to base_pointer_requests_ to be
  // notified when computation is done.
  bool EnsureBasePointer();

  // Updates the given cached registers. If a register category is represented here, the array will
  // contain all known registers from that category so entire categories can be overwritten.
  void SaveRegisterUpdates(std::vector<debug_ipc::Register> regs);

  Thread* thread_;

  uint64_t sp_;
  uint64_t cfa_;

  // Currently cached registers, indexed by register category.
  std::optional<std::vector<debug_ipc::Register>>
      registers_[static_cast<size_t>(debug_ipc::RegisterCategory::kLast)];

  mutable Location location_;                                          // Lazily symbolized.
  mutable fxl::RefPtr<FrameSymbolDataProvider> symbol_data_provider_;  // Lazy.
  mutable fxl::RefPtr<EvalContextImpl> symbol_eval_context_;           // Lazy.

  // The lazily computed frame base. This will be from DW_AT_frame_base on the function if there is
  // one.
  std::optional<uint64_t> computed_base_pointer_;

  // Non-null when evaluating a frame base pointer expression.
  std::unique_ptr<DwarfExprEval> base_pointer_eval_;

  // When an async base pointer request is pending, this maintains all pending callbacks.
  std::vector<fit::callback<void(uint64_t)>> base_pointer_requests_;

  fxl::WeakPtrFactory<FrameImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FrameImpl);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FRAME_IMPL_H_
