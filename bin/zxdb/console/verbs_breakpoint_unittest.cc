// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/verbs_breakpoint.h"
#include "garnet/bin/zxdb/client/breakpoint_settings.h"
#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/symbols/location.h"
#include "gtest/gtest.h"

namespace zxdb {

namespace {

// Implements only the location getter since that's all that's needed for
// breakpoint resolving.
class DummyFrame : public Frame {
 public:
  explicit DummyFrame(const Location& loc) : Frame(nullptr), location_(loc) {}
  ~DummyFrame() override {}

  Thread* GetThread() const override { return nullptr; }
  const Location& GetLocation() const override { return location_; }
  uint64_t GetAddress() const override { return location_.address(); }

 private:
  Location location_;
};

}  // namespace

TEST(VerbsBreakpoint, ParseLocation) {
  BreakpointSettings settings;

  // Valid symbol (including colons).
  Err err = ParseBreakpointLocation(nullptr, "Foo::Bar", &settings);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(BreakpointSettings::LocationType::kSymbol, settings.location_type);
  EXPECT_EQ("Foo::Bar", settings.location_symbol);

  // Valid file/line.
  settings = BreakpointSettings();
  err = ParseBreakpointLocation(nullptr, "foo/bar.cc:123", &settings);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(BreakpointSettings::LocationType::kLine, settings.location_type);
  EXPECT_EQ("foo/bar.cc", settings.location_line.file());
  EXPECT_EQ(123, settings.location_line.line());

  // Invalid file/line.
  settings = BreakpointSettings();
  err = ParseBreakpointLocation(nullptr, "foo/bar.cc:123x", &settings);
  EXPECT_TRUE(err.has_error());

  // Valid address.
  settings = BreakpointSettings();
  err = ParseBreakpointLocation(nullptr, "*0x12345f", &settings);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(BreakpointSettings::LocationType::kAddress, settings.location_type);
  EXPECT_EQ(0x12345fu, settings.location_address);

  // Invalid address.
  settings = BreakpointSettings();
  err = ParseBreakpointLocation(nullptr, "*2134x", &settings);
  EXPECT_TRUE(err.has_error());

  // Line number with no Frame for context.
  settings = BreakpointSettings();
  err = ParseBreakpointLocation(nullptr, "21", &settings);
  EXPECT_TRUE(err.has_error());

  // Implicit file name and valid frame but the location has no file name.
  DummyFrame frame_no_file(Location(0x1234, FileLine(), 0, "Func"));
  settings = BreakpointSettings();
  err = ParseBreakpointLocation(&frame_no_file, "21", &settings);
  EXPECT_TRUE(err.has_error());

  // Valid implicit file name.
  std::string file = "foo.cc";
  DummyFrame frame_valid(Location(0x1234, FileLine(file, 12), 0, "Func"));
  settings = BreakpointSettings();
  err = ParseBreakpointLocation(&frame_valid, "21", &settings);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(file, settings.location_line.file());
  EXPECT_EQ(21, settings.location_line.line());
}

}  // namespace zxdb
