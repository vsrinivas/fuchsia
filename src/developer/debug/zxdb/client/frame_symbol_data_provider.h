// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/developer/debug/zxdb/client/process_symbol_data_provider.h"

namespace zxdb {

class Frame;

// Implementation of SymbolDataProvider that links it to a frame. On top of the
// process' general memory read/write, this adds stack information and the
// instruction pointer.
class FrameSymbolDataProvider : public ProcessSymbolDataProvider {
 public:
  // ProcessSymbolDataProvider overrides:
  void Disown() override;

  // SymbolDataProvider implementation:
  std::optional<uint64_t> GetRegister(debug_ipc::RegisterID id) override;
  void GetRegisterAsync(debug_ipc::RegisterID id,
                        GetRegisterCallback callback) override;
  std::optional<uint64_t> GetFrameBase() override;
  void GetFrameBaseAsync(GetRegisterCallback callback) override;

 private:
  FRIEND_MAKE_REF_COUNTED(FrameSymbolDataProvider);
  FRIEND_REF_COUNTED_THREAD_SAFE(FrameSymbolDataProvider);

  explicit FrameSymbolDataProvider(Frame* frame);
  ~FrameSymbolDataProvider() override;

  // Returns true if the associated frame is the top frame, or it is an inline
  // frame of the topmost physical frame. This means the thread registers are
  // be valid for it.
  bool IsInTopPhysicalFrame() const;

  // The associated frame, possibly null if the frame has been disowned.
  Frame* frame_;
};

}  // namespace zxdb
