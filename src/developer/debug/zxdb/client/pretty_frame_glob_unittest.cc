// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/pretty_frame_glob.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/location.h"

namespace zxdb {

TEST(PrettyFrameGlob, Matches) {
  auto func = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  func->set_assigned_name("MyFunction");

  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();

  // Wildcard matches any location, even unsymbolized.
  EXPECT_TRUE(PrettyFrameGlob::Wildcard().Matches(Location(Location::State::kAddress, 0x23723)));

  // File/function exact matches:
  Location loc(0x1234, FileLine("file.cc", 23), 0, symbol_context, func);
  EXPECT_TRUE(PrettyFrameGlob::File("file.cc").Matches(loc));
  EXPECT_FALSE(PrettyFrameGlob::File("otherfile.cc").Matches(loc));

  EXPECT_TRUE(PrettyFrameGlob::Func("MyFunction").Matches(loc));
  EXPECT_FALSE(PrettyFrameGlob::Func("OtherFunction").Matches(loc));
}

}  // namespace zxdb
