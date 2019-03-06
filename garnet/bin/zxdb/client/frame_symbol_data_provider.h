// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/symbols/symbol_data_provider.h"

namespace zxdb {

class Frame;

// Implementation of SymbolDataProvider that links it to a frame. The
// frame provides stack information, the instruction pointer, and access to
// process memory for the purposes of evaluating symbolic data.
class FrameSymbolDataProvider : public SymbolDataProvider {
 public:
  // Called by the frame when it's being destroyed. This will remove the
  // back-pointer to the frame and all future requests for data will fail.
  //
  // This is necessary because this class is reference counted and may outlive
  // the frame due to in-progress operations.
  void DisownFrame();

  // SymbolDataProvider implementation:
  debug_ipc::Arch GetArch() override;
  std::optional<uint64_t> GetRegister(debug_ipc::RegisterID id) override;
  void GetRegisterAsync(debug_ipc::RegisterID id,
                        GetRegisterCallback callback) override;
  std::optional<uint64_t> GetFrameBase() override;
  void GetFrameBaseAsync(GetRegisterCallback callback) override;
  void GetMemoryAsync(uint64_t address, uint32_t size,
                      GetMemoryCallback callback) override;
  void WriteMemory(uint64_t address, std::vector<uint8_t> data,
                   std::function<void(const Err&)> cb) override;

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
  debug_ipc::Arch arch_;
};

}  // namespace zxdb
