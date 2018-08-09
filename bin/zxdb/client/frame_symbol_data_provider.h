// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/symbols/symbol_data_provider.h"

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
  bool GetRegister(int dwarf_register_number, uint64_t* output) override;
  void GetRegisterAsync(
      int dwarf_register_number,
      std::function<void(bool success, uint64_t value)> callback) override;
  void GetMemoryAsync(
      uint64_t address, uint32_t size,
      std::function<void(const uint8_t* data)> callback) override;

 private:
  FRIEND_MAKE_REF_COUNTED(FrameSymbolDataProvider);
  FRIEND_REF_COUNTED_THREAD_SAFE(FrameSymbolDataProvider);

  explicit FrameSymbolDataProvider(Frame* frame);
  ~FrameSymbolDataProvider() override;

  // Returns true if the associated frame is the top frame, meaning the thread
  // registers are be valid for it.
  bool IsTopFrame() const;

  // The associated frame, possibly null if the frame has been disowned.
  Frame* frame_;
};

}  // namespace zxdb
