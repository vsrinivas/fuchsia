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

TEST(UsingTests, GoodUsing) {
  SharedAmongstLibraries shared;
  TestLibrary dependency(&shared, "dependent.fidl", R"FIDL(library dependent;

type Bar = struct {
    s int8;
};
)FIDL");
  ASSERT_COMPILED(dependency);

  TestLibrary library(&shared);
  library.AddFile("good/fi-0178.test.fidl");

  ASSERT_COMPILED(library);
}

TEST(UsingTests, GoodUsingAlias) {
  SharedAmongstLibraries shared;
  TestLibrary dependency(&shared, "dependent.fidl", R"FIDL(library dependent;

type Bar = struct {
    s int8;
};
)FIDL");
  ASSERT_COMPILED(dependency);

  TestLibrary library(&shared, "example.fidl", R"FIDL(
library example;

using dependent as the_alias;

type Foo = struct {
    dep1 the_alias.Bar;
};

)FIDL");
  ASSERT_COMPILED(library);
}

TEST(UsingTests, GoodUsingSwapNames) {
  SharedAmongstLibraries shared;
  TestLibrary dependency1(&shared, "dependent1.fidl", R"FIDL(library dependent1;

const C1 bool = false;
)FIDL");
  ASSERT_COMPILED(dependency1);
  TestLibrary dependency2(&shared, "dependent2.fidl", R"FIDL(library dependent2;

const C2 bool = false;
)FIDL");
  ASSERT_COMPILED(dependency2);

  TestLibrary library(&shared, "example.fidl", R"FIDL(
library example;

using dependent1 as dependent2;
using dependent2 as dependent1;

const C1 bool = dependent2.C1;
const C2 bool = dependent1.C2;
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(UsingTests, GoodDeclWithSameNameAsAliasedLibrary) {
  SharedAmongstLibraries shared;
  TestLibrary dependency(&shared, "dep.fidl", R"FIDL(library dep;

type A = struct{};
)FIDL");
  ASSERT_COMPILED(dependency);

  TestLibrary library(&shared, "lib.fidl",
                      R"FIDL(
library lib;

using dep as depnoconflict;

type dep = struct {};

type B = struct{a depnoconflict.A;}; // So the import is used.

)FIDL");

  ASSERT_COMPILED(library);
}

TEST(UsingTests, BadMissingUsing) {
  TestLibrary library(R"FIDL(
library example;

// missing using.

type Foo = struct {
    dep dependent.Bar;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameNotFound);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "'dependent' in library 'example'");
}

TEST(UsingTests, BadUnknownUsing) {
  TestLibrary library;
  library.AddFile("bad/fi-0046.test.fidl");
}

TEST(UsingTests, BadUsingAliasRefThroughFqn) {
  SharedAmongstLibraries shared;
  TestLibrary dependency(&shared, "dependent.fidl", R"FIDL(library dependent;

type Bar = struct {
    s int8;
};
)FIDL");
  ASSERT_COMPILED(dependency);

  TestLibrary library(&shared, "example.fidl", R"FIDL(
library example;

using dependent as the_alias;

type Foo = struct {
    dep1 dependent.Bar;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameNotFound);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "'dependent' in library 'example'");
}

TEST(UsingTests, BadDuplicateUsingNoAlias) {
  SharedAmongstLibraries shared;
  TestLibrary dependency(&shared);
  dependency.AddFile("bad/fi-0042-a.test.fidl");
  ASSERT_COMPILED(dependency);
  TestLibrary library(&shared);
  library.AddFile("bad/fi-0042-b.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateLibraryImport);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "fi0042a");
}

TEST(UsingTests, BadDuplicateUsingFirstAlias) {
  SharedAmongstLibraries shared;
  TestLibrary dependency(&shared, "dependent.fidl", R"FIDL(library dependent;
)FIDL");
  ASSERT_COMPILED(dependency);

  TestLibrary library(&shared, "example.fidl", R"FIDL(
library example;

using dependent as alias;
using dependent; // duplicated

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateLibraryImport);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dependent");
}

TEST(UsingTests, BadDuplicateUsingSecondAlias) {
  SharedAmongstLibraries shared;
  TestLibrary dependency(&shared, "dependent.fidl", R"FIDL(library dependent;
)FIDL");
  ASSERT_COMPILED(dependency);

  TestLibrary library(&shared, "example.fidl", R"FIDL(
library example;

using dependent;
using dependent as alias; // duplicated

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateLibraryImport);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dependent");
}

TEST(UsingTests, BadDuplicateUsingSameLibrarySameAlias) {
  SharedAmongstLibraries shared;
  TestLibrary dependency(&shared, "dependent.fidl", R"FIDL(library dependent;
)FIDL");
  ASSERT_COMPILED(dependency);

  TestLibrary library(&shared, "example.fidl", R"FIDL(
library example;

using dependent as alias;
using dependent as alias; // duplicated

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateLibraryImport);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dependent");
}

TEST(UsingTests, BadDuplicateUsingSameLibraryDifferentAlias) {
  SharedAmongstLibraries shared;
  TestLibrary dependency(&shared, "dependent.fidl", R"FIDL(library dependent;
)FIDL");
  ASSERT_COMPILED(dependency);

  TestLibrary library(&shared, "example.fidl", R"FIDL(
library example;

using dependent as alias1;
using dependent as alias2; // duplicated

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateLibraryImport);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dependent");
}

TEST(UsingTests, BadConflictingUsingLibraryAndAlias) {
  SharedAmongstLibraries shared;
  TestLibrary dependency1(&shared, "dependent1.fidl", R"FIDL(library dependent1;
)FIDL");
  ASSERT_COMPILED(dependency1);
  TestLibrary dependency2(&shared, "dependent2.fidl", R"FIDL(library dependent2;
)FIDL");
  ASSERT_COMPILED(dependency2);

  TestLibrary library(&shared, "example.fidl", R"FIDL(
library example;

using dependent1;
using dependent2 as dependent1; // conflict

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrConflictingLibraryImportAlias);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dependent2");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dependent1");
}

TEST(UsingTests, BadConflictingUsingAliasAndLibrary) {
  SharedAmongstLibraries shared;
  TestLibrary dependency1(&shared);
  dependency1.AddFile("bad/fi-0043-a.test.fidl");
  ASSERT_COMPILED(dependency1);
  TestLibrary dependency2(&shared);
  dependency2.AddFile("bad/fi-0043-b.test.fidl");
  ASSERT_COMPILED(dependency2);

  TestLibrary library(&shared);
  library.AddFile("bad/fi-0043-c.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrConflictingLibraryImport);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "fi0043b");
}

TEST(UsingTests, BadConflictingUsingAliasAndAlias) {
  SharedAmongstLibraries shared;
  TestLibrary dependency1(&shared);
  dependency1.AddFile("bad/fi-0044-a.test.fidl");
  ASSERT_COMPILED(dependency1);
  TestLibrary dependency2(&shared);
  dependency2.AddFile("bad/fi-0044-b.test.fidl");
  ASSERT_COMPILED(dependency2);

  TestLibrary library(&shared);
  library.AddFile("bad/fi-0044-c.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrConflictingLibraryImportAlias);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "fi0044b");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dep");
}

TEST(UsingTests, BadUnusedUsing) {
  SharedAmongstLibraries shared;
  TestLibrary dependency(&shared, "dependent.fidl", R"FIDL(library dependent;
)FIDL");
  ASSERT_COMPILED(dependency);

  TestLibrary library(&shared);
  library.AddFile("bad/fi-0178.test.fidl");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnusedImport);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dependent");
}

TEST(UsingTests, BadUnknownDependentLibrary) {
  TestLibrary library(R"FIDL(
library example;

const QUX foo.bar.baz = 0;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnknownDependentLibrary);
}

TEST(UsingTests, BadTooManyProvidedLibraries) {
  SharedAmongstLibraries shared;

  TestLibrary dependency(&shared, "notused.fidl", "library not.used;");
  ASSERT_COMPILED(dependency);

  TestLibrary library(&shared, "example.fidl", "library example;");
  ASSERT_COMPILED(library);

  auto unused = shared.all_libraries()->Unused();
  ASSERT_EQ(unused.size(), 1);
  ASSERT_EQ(fidl::NameLibrary((*unused.begin())->name), "not.used");
}

TEST(UsingTests, BadLibraryDeclarationNameCollision) {
  SharedAmongstLibraries shared;
  TestLibrary dependency(&shared);
  dependency.AddFile("bad/fi-0038-a.test.fidl");
  ASSERT_COMPILED(dependency);

  TestLibrary library(&shared);
  library.AddFile("bad/fi-0038-b.test.fidl");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDeclNameConflictsWithLibraryImport);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dependency");
}

TEST(UsingTests, BadAliasedLibraryDeclarationNameCollision) {
  SharedAmongstLibraries shared;
  TestLibrary dependency(&shared, "dep.fidl", R"FIDL(library dep;

type A = struct{};
)FIDL");
  ASSERT_COMPILED(dependency);

  TestLibrary library(&shared, "lib.fidl",
                      R"FIDL(
library lib;

using dep as x;

type x = struct{};

type B = struct{a dep.A;}; // So the import is used.

)FIDL");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDeclNameConflictsWithLibraryImport);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "x");
}

}  // namespace
