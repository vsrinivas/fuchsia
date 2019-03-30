// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/breakpoint_location.h"

namespace zxdb {

class BreakpointImpl;

class BreakpointLocationImpl final : public BreakpointLocation {
 public:
  BreakpointLocationImpl(BreakpointImpl* bp, Process* process,
                         uint64_t address);
  ~BreakpointLocationImpl() override;

  // Non-virtual inline getter that doesn't force a symbol lookup.
  uint64_t address() const { return address_; }

  // BreakpointLocation implementation.
  Process* GetProcess() const override;
  Location GetLocation() const override;
  bool IsEnabled() const override;
  void SetEnabled(bool enabled) override;

 private:
  BreakpointImpl* breakpoint_;  // Non-owning.
  Process* process_;            // Non-owning.
  uint64_t address_;
  bool enabled_ = true;

  FXL_DISALLOW_COPY_AND_ASSIGN(BreakpointLocationImpl);
};

}  // namespace zxdb
