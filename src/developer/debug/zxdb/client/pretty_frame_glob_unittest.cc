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

  // Test a template. This needs to be a new function object because the name will be cached.
  auto template_func = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  template_func->set_assigned_name("MyFunction<int>");
  Location template_loc(0x1234, FileLine("file.cc", 23), 0, symbol_context, template_func);

  // This just tests that the templates and wildcards are hooked up. The globs are covered by the
  // IdentifierGlob tests.
  EXPECT_TRUE(PrettyFrameGlob::Func("MyFunction<int>").Matches(template_loc));
  EXPECT_FALSE(PrettyFrameGlob::Func("MyFunction<char>").Matches(template_loc));
  EXPECT_TRUE(PrettyFrameGlob::Func("MyFunction<*>").Matches(template_loc));
}

}  // namespace zxdb
