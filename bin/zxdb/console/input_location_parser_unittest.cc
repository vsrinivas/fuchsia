// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/input_location_parser.h"
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
  uint64_t GetStackPointer() const override { return 0x12345678; }

 private:
  Location location_;
};

}  // namespace

TEST(InputLocationParser, Parse) {
  InputLocation location;

  // Valid symbol (including colons).
  Err err = ParseInputLocation(nullptr, "Foo::Bar", &location);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(InputLocation::Type::kSymbol, location.type);
  EXPECT_EQ("Foo::Bar", location.symbol);

  // Valid file/line.
  location = InputLocation();
  err = ParseInputLocation(nullptr, "foo/bar.cc:123", &location);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(InputLocation::Type::kLine, location.type);
  EXPECT_EQ("foo/bar.cc", location.line.file());
  EXPECT_EQ(123, location.line.line());

  // Invalid file/line.
  location = InputLocation();
  err = ParseInputLocation(nullptr, "foo/bar.cc:123x", &location);
  EXPECT_TRUE(err.has_error());

  // Valid address.
  location = InputLocation();
  err = ParseInputLocation(nullptr, "*0x12345f", &location);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(InputLocation::Type::kAddress, location.type);
  EXPECT_EQ(0x12345fu, location.address);

  // Invalid address.
  location = InputLocation();
  err = ParseInputLocation(nullptr, "*2134x", &location);
  EXPECT_TRUE(err.has_error());

  // Line number with no Frame for context.
  location = InputLocation();
  err = ParseInputLocation(nullptr, "21", &location);
  EXPECT_TRUE(err.has_error());

  // Implicit file name and valid frame but the location has no file name.
  DummyFrame frame_no_file(Location(0x1234, FileLine(), 0, "Func"));
  location = InputLocation();
  err = ParseInputLocation(&frame_no_file, "21", &location);
  EXPECT_TRUE(err.has_error());

  // Valid implicit file name.
  std::string file = "foo.cc";
  DummyFrame frame_valid(Location(0x1234, FileLine(file, 12), 0, "Func"));
  location = InputLocation();
  err = ParseInputLocation(&frame_valid, "21", &location);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(file, location.line.file());
  EXPECT_EQ(21, location.line.line());
}

}  // namespace zxdb
