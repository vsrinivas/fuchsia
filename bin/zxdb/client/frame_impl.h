// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/symbols/location.h"
#include "garnet/lib/debug_ipc/records.h"
#include "lib/fxl/memory/ref_counted.h"

namespace zxdb {

class FrameSymbolDataProvider;
class ThreadImpl;

// A frame is lazily symbolized.
class FrameImpl final : public Frame {
 public:
  FrameImpl(ThreadImpl* thread, const debug_ipc::StackFrame& stack_frame,
            Location location);
  ~FrameImpl() override;

  // Frame implementation.
  Thread* GetThread() const override;
  const Location& GetLocation() const override;
  uint64_t GetAddress() const override;
  uint64_t GetStackPointer() const override;
  fxl::RefPtr<SymbolDataProvider> GetSymbolDataProvider() override;

 private:
  void EnsureSymbolized();

  ThreadImpl* thread_;

  debug_ipc::StackFrame stack_frame_;
  Location location_;
  fxl::RefPtr<FrameSymbolDataProvider> symbol_data_provider_;  // Lazy.

  FXL_DISALLOW_COPY_AND_ASSIGN(FrameImpl);
};

}  // namespace zxdb
