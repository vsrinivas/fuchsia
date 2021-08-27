// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/names.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>
#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

TEST(UsingTests, GoodUsing) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(library dependent;

type Bar = struct {
    s int8;
};
)FIDL",
                         &shared);
  ASSERT_COMPILED(dependency);

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent;

type Foo = struct {
    dep dependent.Bar;
};

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_COMPILED(library);
}

TEST(UsingTests, GoodUsingWithAsRefsThroughBoth) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(library dependent;

type Bar = struct {
    s int8;
};
)FIDL",
                         &shared);
  ASSERT_COMPILED(dependency);

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent as the_alias;

type Foo = struct {
    dep1 dependent.Bar;
    dep2 the_alias.Bar;
};

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_COMPILED(library);
}

TEST(UsingTests, GoodUsingWithAsRefOnlyThroughFqn) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(library dependent;

type Bar = struct {
    s int8;
};
)FIDL",
                         &shared);
  ASSERT_COMPILED(dependency);

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent as the_alias;

type Foo = struct {
    dep1 dependent.Bar;
};

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_COMPILED(library);
}

TEST(UsingTests, GoodUsingWithAsRefOnlyThroughAlias) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(library dependent;

type Bar = struct {
    s int8;
};
)FIDL",
                         &shared);
  ASSERT_COMPILED(dependency);

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent as the_alias;

type Foo = struct {
    dep1 the_alias.Bar;
};

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
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
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnknownType);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dependent.Bar");
}

TEST(UsingTests, BadUnknownUsing) {
  TestLibrary library(R"FIDL(
library example;

using dependent; // unknown using.

type Foo = struct {
    dep dependent.Bar;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnknownLibrary);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dependent");
}

TEST(UsingTests, BadDuplicateUsing) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(library dependent;
)FIDL",
                         &shared);
  ASSERT_COMPILED(dependency);

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent;
using dependent; // duplicated

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateLibraryImport);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dependent");
}

TEST(UsingTests, BadUnusedUsing) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(library dependent;
)FIDL",
                         &shared);
  ASSERT_COMPILED(dependency);

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent;

type Foo = struct {
    does_not int64;
    use_dependent int32;
};

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnusedImport);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dependent");
}

TEST(UsingTests, BadUnknownDependentLibrary) {
  TestLibrary library("example.fidl", R"FIDL(
library example;

const QUX foo.bar.baz = 0;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnknownDependentLibrary);
}

TEST(UsingTests, WarnTooManyProvidedLibraries) {
  SharedAmongstLibraries shared;

  TestLibrary dependency("notused.fidl", "library not.used;", &shared);
  ASSERT_COMPILED(dependency);

  TestLibrary library("example.fidl", "library example;", &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_COMPILED(library);

  auto unused = shared.all_libraries.Unused(library.library());
  ASSERT_EQ(1, unused.size());
  ASSERT_STR_EQ("not.used", fidl::NameLibrary(*unused.begin()).c_str());
}

TEST(UsingTests, BadFilesDisagreeOnLibraryName) {
  TestLibrary library("lib_file1.fidl",
                      R"FIDL(
library lib;
)FIDL");
  library.AddSource("lib_file2.fidl",
                    R"FIDL(
library dib;
  )FIDL");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrFilesDisagreeOnLibraryName);
}

TEST(UsingTests, BadLibraryDeclarationNameCollision) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("dep.fidl", R"FIDL(library dep;

type A = struct{};
)FIDL",
                         &shared);
  ASSERT_COMPILED(dependency);

  TestLibrary library("lib.fidl",
                      R"FIDL(
library lib;

using dep;

type dep = struct{};

type B = struct {a dep.A;}; // So the import is used.

)FIDL",
                      &shared);

  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDeclNameConflictsWithLibraryImport);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dep");
}

TEST(UsingTests, BadAliasedLibraryDeclarationNameCollision) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("dep.fidl", R"FIDL(library dep;

type A = struct{};
)FIDL",
                         &shared);
  ASSERT_COMPILED(dependency);

  TestLibrary library("lib.fidl",
                      R"FIDL(
library lib;

using dep as x;

type x = struct{};

type B = struct{a dep.A;}; // So the import is used.

)FIDL",
                      &shared);

  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDeclNameConflictsWithLibraryImport);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "x");
}

TEST(UsingTests, BadAliasedLibraryNonaliasedDeclarationNameCollision) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("dep.fidl", R"FIDL(library dep;

type A = struct{};
)FIDL",
                         &shared);
  ASSERT_COMPILED(dependency);

  TestLibrary library("lib.fidl",
                      R"FIDL(
library lib;

using dep as depnoconflict;

type dep = struct {};

type B = struct{a depnoconflict.A;}; // So the import is used.

)FIDL",
                      &shared);

  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDeclNameConflictsWithLibraryImport);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dep");
}

}  // namespace
