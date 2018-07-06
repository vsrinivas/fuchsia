// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/symbols/location.h"
#include "garnet/lib/debug_ipc/records.h"

namespace zxdb {

// Provides a MockFrame implementation that just returns constant values for
// everything. Tests can override this to implement the subset of functionality
// they need.
class MockFrame : public Frame {
 public:
  // Session and can be null as long as no code that uses this object needs it.
  MockFrame(Session* session, Thread* thread,
            const debug_ipc::StackFrame& stack_frame, const Location& location);

  ~MockFrame() override;

  // Frame implementation.
  Thread* GetThread() const override;
  const Location& GetLocation() const override;
  uint64_t GetAddress() const override;
  uint64_t GetStackPointer() const override;

 private:
  Thread* thread_;

  debug_ipc::StackFrame stack_frame_;
  Location location_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MockFrame);
};

}  // namespace zxdb
