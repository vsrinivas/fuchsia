// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/breakpoint_settings.h"
#include "garnet/bin/zxdb/console/verbs_breakpoint.h"
#include "gtest/gtest.h"

namespace zxdb {

TEST(VerbsBreakpoint, ParseLocation) {
  BreakpointSettings settings;

  // Valid symbol (including colons).
  Err err = ParseBreakpointLocation("Foo::Bar", &settings);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(BreakpointSettings::LocationType::kSymbol, settings.location_type);
  EXPECT_EQ("Foo::Bar", settings.location_symbol);

  // Valid file/line.
  settings = BreakpointSettings();
  err = ParseBreakpointLocation("foo/bar.cc:123", &settings);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(BreakpointSettings::LocationType::kLine, settings.location_type);
  EXPECT_EQ("foo/bar.cc", settings.location_line.file());
  EXPECT_EQ(123, settings.location_line.line());

  // Invalid file/line.
  settings = BreakpointSettings();
  err = ParseBreakpointLocation("foo/bar.cc:123x", &settings);
  EXPECT_TRUE(err.has_error());

  // Valid address.
  settings = BreakpointSettings();
  err = ParseBreakpointLocation("*0x12345f", &settings);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(BreakpointSettings::LocationType::kAddress, settings.location_type);
  EXPECT_EQ(0x12345fu, settings.location_address);

  // Invalid address.
  settings = BreakpointSettings();
  err = ParseBreakpointLocation("*2134x", &settings);
  EXPECT_TRUE(err.has_error());
}

}  // namespace zxdb
