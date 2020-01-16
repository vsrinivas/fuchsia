// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MOCK_BREAKPOINT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MOCK_BREAKPOINT_H_

#include <memory>

#include "src/developer/debug/zxdb/client/breakpoint.h"
#include "src/developer/debug/zxdb/client/breakpoint_location.h"

namespace zxdb {

class MockBreakpoint : public Breakpoint {
 public:
  explicit MockBreakpoint(Session* session) : Breakpoint(session) {}

  void set_is_internal(bool internal) { is_internal_ = internal; }
  void set_locations(std::vector<std::unique_ptr<BreakpointLocation>> locs) {
    locations_ = std::move(locs);
  }

  // Breakpoint implementation.
  BreakpointSettings GetSettings() const override { return settings_; }
  void SetSettings(const BreakpointSettings& settings) override;
  bool IsInternal() const override { return is_internal_; }
  std::vector<const BreakpointLocation*> GetLocations() const override;
  std::vector<BreakpointLocation*> GetLocations() override;

 public:
  BreakpointSettings settings_;
  bool is_internal_ = false;
  std::vector<std::unique_ptr<BreakpointLocation>> locations_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MOCK_BREAKPOINT_H_
