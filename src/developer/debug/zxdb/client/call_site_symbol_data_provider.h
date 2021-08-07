// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_CALL_SITE_SYMBOL_DATA_PROVIDER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_CALL_SITE_SYMBOL_DATA_PROVIDER_H_

#include "src/developer/debug/shared/register_id.h"
#include "src/developer/debug/zxdb/client/process_symbol_data_provider.h"

namespace zxdb {

class CallSite;
class Frame;
class Location;

// Implementation of SymbolDataProvider that links to a function call site within a given frame.
// This is ued by a deeper frame to evaluate the registers at the call site for the purposes of
// DWARF expressions containing DW_OP_entry_value.
//
// This uses the saved registers for the previous frame (which should be valid at the nested frame's
// call site), as well as any DW_TAG_call_site / DW_TAG_call_site_parameter entries corresponding
// to the call (see CallSite objects exposed by CodeBlock).
//
// It allows access to memory. Theoretically, any memory could have changed from the time of the
// call, but we expect any references from with an "entry value" DWARF expression to make sense in
// this context. Generally any memory accesses will refer to entries in the caller's stack.
class CallSiteSymbolDataProvider : public ProcessSymbolDataProvider {
 public:
  // SymbolDataProvider implementation:
  fxl::RefPtr<SymbolDataProvider> GetEntryDataProvider() const override;
  std::optional<containers::array_view<uint8_t>> GetRegister(debug::RegisterID id) override;
  void GetRegisterAsync(debug::RegisterID id, GetRegisterCallback callback) override;
  void WriteRegister(debug::RegisterID id, std::vector<uint8_t> data, WriteCallback cb) override;
  std::optional<uint64_t> GetFrameBase() override;
  void GetFrameBaseAsync(GetFrameBaseCallback callback) override;
  uint64_t GetCanonicalFrameAddress() const override;

 private:
  FRIEND_MAKE_REF_COUNTED(CallSiteSymbolDataProvider);
  FRIEND_REF_COUNTED_THREAD_SAFE(CallSiteSymbolDataProvider);

  // The return location is the location of the previous frame, which should be the return address
  // from the function being called. The frame_provider is the data provider from the calling frame
  // and is used to access the saved registers and memory.
  CallSiteSymbolDataProvider(fxl::WeakPtr<Process> process, const Location& return_location,
                             fxl::RefPtr<SymbolDataProvider> frame_provider);

  // Constructor with a known call site (for use with unit tests).
  CallSiteSymbolDataProvider(fxl::WeakPtr<Process> process, fxl::RefPtr<CallSite> call_site,
                             const SymbolContext& call_site_symbol_context,
                             fxl::RefPtr<SymbolDataProvider> frame_provider);

  ~CallSiteSymbolDataProvider() override;

  // The unwind tables will generate values for every register but normally only the callee-saved
  // registers will have valid values. Code should check this before returning any registers from
  // the frame_provider_.
  //
  // TODO(fxbug.dev/74320) remove this when the unwinder only reports registers it knows about.
  bool IsRegisterCalleeSaved(debug::RegisterID id);

  // Looks up to see if there's a matching call site parameter for the given register. Returns if
  // if so, or nullptr if no match.
  fxl::RefPtr<CallSiteParameter> ParameterForRegister(debug::RegisterID id);

  // Possibly null if no match.
  fxl::RefPtr<CallSite> call_site_;

  // The symbol context associated with the call site.
  SymbolContext call_site_symbol_context_;

  // Guaranteed non-null.
  fxl::RefPtr<SymbolDataProvider> frame_provider_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_CALL_SITE_SYMBOL_DATA_PROVIDER_H_
