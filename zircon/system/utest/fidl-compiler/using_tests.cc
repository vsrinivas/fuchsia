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
  TestLibrary dependency("dependent.fidl", R"FIDL(
library dependent;

struct Bar {
    int8 s;
};

)FIDL",
                         &shared);
  TestLibrary converted_dependency;
  ASSERT_COMPILED_AND_CONVERT_INTO(dependency, converted_dependency);

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent;

struct Foo {
    dependent.Bar dep;
};

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_COMPILED_AND_CONVERT_WITH_DEP(library, converted_dependency);
}

TEST(UsingTests, GoodUsingWithOldDep) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(
library dependent;

struct Bar {
    int8 s;
};

)FIDL",
                         &shared);
  TestLibrary cloned_dependency;
  ASSERT_COMPILED_AND_CLONE_INTO(dependency, cloned_dependency);

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent;

struct Foo {
    dependent.Bar dep;
};

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_COMPILED_AND_CONVERT_WITH_DEP(library, cloned_dependency);
}

TEST(UsingTests, GoodUsingWithAsRefsThroughBoth) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(
library dependent;

struct Bar {
    int8 s;
};

)FIDL",
                         &shared);
  TestLibrary converted_dependency;
  ASSERT_COMPILED_AND_CONVERT_INTO(dependency, converted_dependency);

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent as the_alias;

struct Foo {
    dependent.Bar dep1;
    the_alias.Bar dep2;
};

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_COMPILED_AND_CONVERT_WITH_DEP(library, converted_dependency);
}

TEST(UsingTests, GoodUsingWithAsRefsThroughBothWithOldDep) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(
library dependent;

struct Bar {
    int8 s;
};

)FIDL",
                         &shared);
  TestLibrary cloned_dependency;
  ASSERT_COMPILED_AND_CLONE_INTO(dependency, cloned_dependency);

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent as the_alias;

struct Foo {
    dependent.Bar dep1;
    the_alias.Bar dep2;
};

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_COMPILED_AND_CONVERT_WITH_DEP(library, cloned_dependency);
}

TEST(UsingTests, GoodUsingWithAsRefOnlyThroughFqn) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(
library dependent;

struct Bar {
    int8 s;
};

)FIDL",
                         &shared);
  TestLibrary converted_dependency;
  ASSERT_COMPILED_AND_CONVERT_INTO(dependency, converted_dependency);

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent as the_alias;

struct Foo {
    dependent.Bar dep1;
};

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_COMPILED_AND_CONVERT_WITH_DEP(library, converted_dependency);
}

TEST(UsingTests, GoodUsingWithAsRefOnlyThroughFqnWithOldDep) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(
library dependent;

struct Bar {
    int8 s;
};

)FIDL",
                         &shared);
  TestLibrary cloned_dependency;
  ASSERT_COMPILED_AND_CLONE_INTO(dependency, cloned_dependency);

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent as the_alias;

struct Foo {
    dependent.Bar dep1;
};

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_COMPILED_AND_CONVERT_WITH_DEP(library, cloned_dependency);
}

TEST(UsingTests, GoodUsingWithAsRefOnlyThroughAlias) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(
library dependent;

struct Bar {
    int8 s;
};

)FIDL",
                         &shared);
  TestLibrary converted_dependency;
  ASSERT_COMPILED_AND_CONVERT_INTO(dependency, converted_dependency);

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent as the_alias;

struct Foo {
    the_alias.Bar dep1;
};

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_COMPILED_AND_CONVERT_WITH_DEP(library, converted_dependency);
}

TEST(UsingTests, GoodUsingWithAsRefOnlyThroughAliasWithOldDep) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(
library dependent;

struct Bar {
    int8 s;
};

)FIDL",
                         &shared);
  TestLibrary cloned_dependency;
  ASSERT_COMPILED_AND_CLONE_INTO(dependency, cloned_dependency);

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent as the_alias;

struct Foo {
    the_alias.Bar dep1;
};

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_COMPILED_AND_CONVERT_WITH_DEP(library, cloned_dependency);
}

TEST(UsingTests, BadMissingUsing) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

// missing using.

type Foo = struct {
    dep dependent.Bar;
};

)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnknownType);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dependent.Bar");
}

TEST(UsingTests, BadUnknownUsing) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

using dependent; // unknown using.

type Foo = struct {
    dep dependent.Bar;
};

)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnknownLibrary);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dependent");
}

TEST(UsingTests, BadDuplicateUsing) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(
library dependent;

)FIDL",
                         &shared);
  TestLibrary converted_dependency;
  ASSERT_COMPILED_AND_CONVERT_INTO(dependency, converted_dependency);

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent;
using dependent; // duplicated

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_ERRORED_DURING_COMPILE_WITH_DEP(library, converted_dependency,
                                         fidl::ErrDuplicateLibraryImport);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dependent");
}

TEST(UsingTests, BadUnusedUsing) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(
library dependent;

)FIDL",
                         &shared);
  TestLibrary converted_dependency;
  ASSERT_COMPILED_AND_CONVERT_INTO(dependency, converted_dependency);

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent;

type Foo = struct {
    does_not int64;
    use_dependent int32;
};

)FIDL",
                      &shared, experimental_flags);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_ERRORED_DURING_COMPILE_WITH_DEP(library, converted_dependency, fidl::ErrUnusedImport);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dependent");
}

TEST(UsingTests, BadUnusedUsingWithOldDep) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(
library dependent;

)FIDL",
                         &shared);
  TestLibrary cloned_dependency;
  ASSERT_COMPILED_AND_CLONE_INTO(dependency, cloned_dependency);

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent;

type Foo = struct {
    does_not int64;
    use_dependent int32;
};

)FIDL",
                      &shared, experimental_flags);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_ERRORED_DURING_COMPILE_WITH_DEP(library, cloned_dependency, fidl::ErrUnusedImport);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dependent");
}

TEST(UsingTests, BadUnknownDependentLibrary) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library("example.fidl", R"FIDL(
library example;

const QUX foo.bar.baz = 0;
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnknownDependentLibrary);
}

TEST(UsingTests, WarnTooManyProvidedLibraries) {
  SharedAmongstLibraries shared;

  TestLibrary dependency("notused.fidl", "library not.used;", &shared);
  TestLibrary converted_dependency;
  ASSERT_COMPILED_AND_CONVERT_INTO(dependency, converted_dependency);

  TestLibrary library("example.fidl", "library example;", &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_COMPILED_AND_CONVERT_WITH_DEP(library, converted_dependency);

  auto unused = shared.all_libraries.Unused(library.library());
  ASSERT_EQ(1, unused.size());
  ASSERT_STR_EQ("not.used", fidl::NameLibrary(*unused.begin()).c_str());
}

TEST(UsingTests, WarnTooManyProvidedLibrariesWithOldDep) {
  SharedAmongstLibraries shared;

  TestLibrary dependency("notused.fidl", "library not.used;", &shared);
  TestLibrary cloned_dependency;
  ASSERT_COMPILED_AND_CLONE_INTO(dependency, cloned_dependency);

  TestLibrary library("example.fidl", "library example;", &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_COMPILED_AND_CONVERT_WITH_DEP(library, cloned_dependency);

  auto unused = shared.all_libraries.Unused(library.library());
  ASSERT_EQ(1, unused.size());
  ASSERT_STR_EQ("not.used", fidl::NameLibrary(*unused.begin()).c_str());
}

TEST(UsingTests, BadFilesDisagreeOnLibraryName) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library("lib_file1.fidl",
                      R"FIDL(
library lib;
)FIDL",
                      experimental_flags);
  library.AddSource("lib_file2.fidl",
                    R"FIDL(
library dib;
  )FIDL");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrFilesDisagreeOnLibraryName);
}

TEST(UsingTests, BadLibraryDeclarationNameCollision) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  SharedAmongstLibraries shared;
  TestLibrary dependency("dep.fidl", R"FIDL(
library dep;

struct A{};

)FIDL",
                         &shared);
  TestLibrary converted_dependency;
  ASSERT_COMPILED_AND_CONVERT_INTO(dependency, converted_dependency);

  TestLibrary library("lib.fidl",
                      R"FIDL(
library lib;

using dep;

type dep = struct{};

type B = struct {a dep.A;}; // So the import is used.

)FIDL",
                      &shared, experimental_flags);

  ASSERT_TRUE(library.AddDependentLibrary(std::move(converted_dependency)));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDeclNameConflictsWithLibraryImport);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dep");
}

TEST(UsingTests, BadLibraryDeclarationNameCollisionWithOldDep) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  SharedAmongstLibraries shared;
  TestLibrary dependency("dep.fidl", R"FIDL(
library dep;

struct A{};

)FIDL",
                         &shared);
  ASSERT_TRUE(dependency.Compile());

  TestLibrary library("lib.fidl",
                      R"FIDL(
library lib;

using dep;

type dep = struct{};

type B = struct {a dep.A;}; // So the import is used.

)FIDL",
                      &shared, experimental_flags);

  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDeclNameConflictsWithLibraryImport);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dep");
}

TEST(UsingTests, BadAliasedLibraryDeclarationNameCollision) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  SharedAmongstLibraries shared;
  TestLibrary dependency("dep.fidl", R"FIDL(
library dep;

struct A{};

)FIDL",
                         &shared);
  TestLibrary converted_dependency;
  ASSERT_COMPILED_AND_CONVERT_INTO(dependency, converted_dependency);

  TestLibrary library("lib.fidl",
                      R"FIDL(
library lib;

using dep as x;

type x = struct{};

type B = struct{a dep.A;}; // So the import is used.

)FIDL",
                      &shared, experimental_flags);

  ASSERT_TRUE(library.AddDependentLibrary(std::move(converted_dependency)));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDeclNameConflictsWithLibraryImport);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "x");
}

TEST(UsingTests, BadAliasedLibraryDeclarationNameCollisionWithOldDep) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  SharedAmongstLibraries shared;
  TestLibrary dependency("dep.fidl", R"FIDL(
library dep;

struct A{};

)FIDL",
                         &shared);
  ASSERT_TRUE(dependency.Compile());

  TestLibrary library("lib.fidl",
                      R"FIDL(
library lib;

using dep as x;

type x = struct{};

type B = struct{a dep.A;}; // So the import is used.

)FIDL",
                      &shared, experimental_flags);

  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDeclNameConflictsWithLibraryImport);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "x");
}

TEST(UsingTests, BadAliasedLibraryNonaliasedDeclarationNameCollision) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  SharedAmongstLibraries shared;
  TestLibrary dependency("dep.fidl", R"FIDL(
library dep;

struct A{};

)FIDL",
                         &shared);
  TestLibrary converted_dependency;
  ASSERT_COMPILED_AND_CONVERT_INTO(dependency, converted_dependency);

  TestLibrary library("lib.fidl",
                      R"FIDL(
library lib;

using dep as depnoconflict;

type dep = struct {};

type B = struct{a depnoconflict.A;}; // So the import is used.

)FIDL",
                      &shared, experimental_flags);

  ASSERT_TRUE(library.AddDependentLibrary(std::move(converted_dependency)));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDeclNameConflictsWithLibraryImport);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dep");
}

TEST(UsingTests, BadAliasedLibraryNonaliasedDeclarationNameCollisionWithOldDep) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  SharedAmongstLibraries shared;
  TestLibrary dependency("dep.fidl", R"FIDL(
library dep;

struct A{};

)FIDL",
                         &shared);
  ASSERT_TRUE(dependency.Compile());

  TestLibrary library("lib.fidl",
                      R"FIDL(
library lib;

using dep as depnoconflict;

type dep = struct {};

type B = struct{a depnoconflict.A;}; // So the import is used.

)FIDL",
                      &shared, experimental_flags);

  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDeclNameConflictsWithLibraryImport);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dep");
}

}  // namespace
