// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/target_symbols.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/process_symbols_test_setup.h"

namespace zxdb {

TEST(TargetSymbols, GetShortestUniqueName) {
  ProcessSymbolsTestSetup setup;

  // No input and no files.
  EXPECT_EQ("", setup.target().GetShortestUniqueFileName({}));

  // Valid input but no indexed files means no match, so name part.
  EXPECT_EQ("baz.cc",
            setup.target().GetShortestUniqueFileName("foo/bar/baz.cc"));

  // Add some file names.
  const char kUnique[] = "a/b/unique.cc";

  const char kAbsolute1[] = "/absolute.cc";
  const char kAbsolute2[] = "/other/absolute.cc";

  const char kFile1[] = "foo/bar/baz/file.cc";
  const char kFile2[] = "foo/bar/something_else/file.cc";
  const char kFile3[] = "foo/really_something/baz/file.cc";

  // Disambiguate when there's no path.
  const char kNoPath1[] = "short/name.cc";
  const char kNoPath2[] = "name.cc";

  auto mod_sym = std::make_unique<MockModuleSymbols>("foo.so");
  mod_sym->AddFileName(kUnique);
  mod_sym->AddFileName(kAbsolute1);
  mod_sym->AddFileName(kAbsolute2);
  mod_sym->AddFileName(kFile1);
  mod_sym->AddFileName(kFile2);
  mod_sym->AddFileName(kFile3);
  mod_sym->AddFileName(kNoPath1);
  mod_sym->AddFileName(kNoPath2);

  setup.InjectModule("foo.so", "0", 0, std::move(mod_sym));

  // Unique names and not found names get just the name part.
  EXPECT_EQ("unique.cc", setup.target().GetShortestUniqueFileName(kUnique));
  EXPECT_EQ("random.cc",
            setup.target().GetShortestUniqueFileName("something/random.cc"));

  // Disambiguation with various parts being the same.
  EXPECT_EQ("bar/baz/file.cc",
            setup.target().GetShortestUniqueFileName(kFile1));
  EXPECT_EQ("something_else/file.cc",
            setup.target().GetShortestUniqueFileName(kFile2));
  EXPECT_EQ("really_something/baz/file.cc",
            setup.target().GetShortestUniqueFileName(kFile3));

  // Disambiguation when there's one with no path.
  EXPECT_EQ("short/name.cc",
            setup.target().GetShortestUniqueFileName(kNoPath1));
  EXPECT_EQ("name.cc", setup.target().GetShortestUniqueFileName(kNoPath2));

  // Same as the "short name" cases but with an absolute path.
  EXPECT_EQ("other/absolute.cc",
            setup.target().GetShortestUniqueFileName(kAbsolute2));
  EXPECT_EQ("/absolute.cc",
            setup.target().GetShortestUniqueFileName(kAbsolute1));
}

}  // namespace zxdb
