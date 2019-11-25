// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MOCK_FRAME_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MOCK_FRAME_H_

#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace zxdb {

class EvalContextImpl;
class MockSymbolDataProvider;

// Provides a MockFrame implementation that just returns constant values for everything. Tests can
// override this to implement the subset of functionality they need.
class MockFrame : public Frame {
 public:
  // Session and Thread can be null as long as no code that uses this object needs it.
  //
  // The physical frame is the non-inlined call frame associated with this one. The pointer must
  // outlive this class (normally both are owned by the Stack). A null physical frame indicates that
  // this is not inline.
  MockFrame(Session* session, Thread* thread, const Location& location, uint64_t sp,
            uint64_t cfa = 0, std::vector<debug_ipc::Register> regs = {}, uint64_t frame_base = 0,
            const Frame* physical_frame = nullptr, bool is_ambiguous_inline = false);

  ~MockFrame() override;

  // Use GetLocation() to retrieve the location.
  void set_location(Location l) { location_ = std::move(l); }

  // Overrides all IPs with a new address, but doesn't change anything else about the location
  // including the stack or symbols.
  void SetAddress(uint64_t address);

  // Overrides the location's file_line with the new value, leaving everything else as-is.
  void SetFileLine(const FileLine& file_line);

  void set_is_ambiguous_inline(bool ambiguous) { is_ambiguous_inline_ = ambiguous; }

  MockSymbolDataProvider* GetMockSymbolDataProvider();

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
  Thread* thread_;

  uint64_t sp_;
  uint64_t cfa_;
  std::vector<debug_ipc::Register> general_registers_;
  uint64_t frame_base_;
  const Frame* physical_frame_;  // Null if non-inlined.
  Location location_;
  mutable fxl::RefPtr<MockSymbolDataProvider> symbol_data_provider_;  // Lazy.
  mutable fxl::RefPtr<EvalContextImpl> eval_context_;                 // Lazy.
  bool is_ambiguous_inline_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(MockFrame);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MOCK_FRAME_H_
