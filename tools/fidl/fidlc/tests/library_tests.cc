// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/include/fidl/flat_ast.h"
#include "tools/fidl/fidlc/include/fidl/lexer.h"
#include "tools/fidl/fidlc/include/fidl/names.h"
#include "tools/fidl/fidlc/include/fidl/parser.h"
#include "tools/fidl/fidlc/include/fidl/source_file.h"
#include "tools/fidl/fidlc/tests/error_test.h"
#include "tools/fidl/fidlc/tests/test_library.h"

namespace {

TEST(LibraryTests, GoodLibraryMultipleFiles) {
  TestLibrary library;
  library.AddFile("good/fi-0040-a.test.fidl");
  library.AddFile("good/fi-0040-b.test.fidl");

  ASSERT_COMPILED(library);
}

TEST(LibraryTests, BadFilesDisagreeOnLibraryName) {
  TestLibrary library;
  library.AddFile("bad/fi-0040-a.test.fidl");
  library.AddFile("bad/fi-0040-b.test.fidl");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrFilesDisagreeOnLibraryName);
}

}  // namespace
