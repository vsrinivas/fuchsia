// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MOCK_BREAKPOINT_LOCATION_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MOCK_BREAKPOINT_LOCATION_H_

#include "src/developer/debug/zxdb/client/breakpoint_location.h"
#include "src/developer/debug/zxdb/symbols/location.h"

namespace zxdb {

class Breakpoint;

class MockBreakpointLocation final : public BreakpointLocation {
 public:
  explicit MockBreakpointLocation(Process* process) : process_(process) {}

  void set_process(Process* process) { process_ = process; }
  void set_location(Location loc) { location_ = std::move(loc); }

  // BreakpointLocation implementation.
  Process* GetProcess() const override { return process_; }
  Location GetLocation() const override { return location_; }
  bool IsEnabled() const override { return is_enabled_; }
  void SetEnabled(bool enabled) override { is_enabled_ = enabled; }

 private:
  Process* process_;  // Non-owning.
  Location location_;
  bool is_enabled_ = true;

  FXL_DISALLOW_COPY_AND_ASSIGN(MockBreakpointLocation);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MOCK_BREAKPOINT_LOCATION_H_
