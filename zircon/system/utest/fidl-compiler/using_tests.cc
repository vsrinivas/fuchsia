// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/names.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>
#include <zxtest/zxtest.h>

#include "assert_strstr.h"
#include "error_test.h"
#include "test_library.h"

namespace {

TEST(UsingTests, valid_using) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(
library dependent;

struct Bar {
    int8 s;
};

)FIDL",
                         &shared);
  ASSERT_TRUE(dependency.Compile());

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent;

struct Foo {
    dependent.Bar dep;
};

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_TRUE(library.Compile());
}

TEST(UsingTests, valid_using_with_as_refs_through_both) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(
library dependent;

struct Bar {
    int8 s;
};

)FIDL",
                         &shared);
  ASSERT_TRUE(dependency.Compile());

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
  ASSERT_TRUE(library.Compile());
}

TEST(UsingTests, valid_using_with_as_ref_only_through_fqn) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(
library dependent;

struct Bar {
    int8 s;
};

)FIDL",
                         &shared);
  ASSERT_TRUE(dependency.Compile());

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent as the_alias;

struct Foo {
    dependent.Bar dep1;
};

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_TRUE(library.Compile());
}

TEST(UsingTests, valid_using_with_as_ref_only_through_alias) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(
library dependent;

struct Bar {
    int8 s;
};

)FIDL",
                         &shared);
  ASSERT_TRUE(dependency.Compile());

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent as the_alias;

struct Foo {
    the_alias.Bar dep1;
};

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_TRUE(library.Compile());
}

TEST(UsingTests, invalid_missing_using) {
  TestLibrary library(R"FIDL(
library example;

// missing using.

struct Foo {
    dependent.Bar dep;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(1, errors.size());
  ASSERT_ERR(errors[0], fidl::ErrUnknownType);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "dependent.Bar");
}

TEST(UsingTests, invalid_unknown_using) {
  TestLibrary library(R"FIDL(
library example;

using dependent; // unknown using.

struct Foo {
    dependent.Bar dep;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(1, errors.size());
  ASSERT_ERR(errors[0], fidl::ErrUnknownLibrary);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "dependent");
}

TEST(UsingTests, invalid_duplicate_using) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(
library dependent;

)FIDL",
                         &shared);
  ASSERT_TRUE(dependency.Compile());

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent;
using dependent; // duplicated

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(1, errors.size());
  ASSERT_ERR(errors[0], fidl::ErrDuplicateLibraryImport);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "dependent");
}

TEST(UsingTests, invalid_unused_using) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(
library dependent;

)FIDL",
                         &shared);
  ASSERT_TRUE(dependency.Compile());

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependent;

struct Foo {
    int64 does_not;
    int32 use_dependent;
};

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_FALSE(library.Compile());

  const auto& errors = library.errors();
  ASSERT_EQ(1, errors.size());
  ASSERT_ERR(errors[0], fidl::ErrUnusedImport);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "dependent");
}

TEST(UsingTests, invalid_unknown_dependent_library) {
  TestLibrary library("example.fidl", R"FIDL(
library example;

const foo.bar.baz QUX = 0;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(1, errors.size());
  ASSERT_ERR(errors[0], fidl::ErrUnknownDependentLibrary);
}

TEST(UsingTests, invalid_too_many_provided_libraries) {
  SharedAmongstLibraries shared;

  TestLibrary dependency("notused.fidl", "library not.used;", &shared);
  ASSERT_TRUE(dependency.Compile());

  TestLibrary library("example.fidl", "library example;", &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_TRUE(library.Compile());

  auto unused = shared.all_libraries.Unused(library.library());
  ASSERT_EQ(1, unused.size());
  ASSERT_STR_EQ("not.used", fidl::NameLibrary(*unused.begin()).c_str());
}

TEST(UsingTests, files_disagree_on_library_name) {
  TestLibrary library("lib_file1.fidl",
                      R"FIDL(
library lib;
)FIDL");
  library.AddSource("lib_file2.fidl",
                    R"FIDL(
library dib;
  )FIDL");

  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(1, errors.size());
  ASSERT_ERR(errors[0], fidl::ErrFilesDisagreeOnLibraryName);
}

TEST(UsingTests, library_declaration_name_collision) {
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

struct dep{};

struct B{dep.A a;}; // So the import is used.

)FIDL",
                      &shared);

  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(1, errors.size());
  ASSERT_ERR(errors[0], fidl::ErrDeclNameConflictsWithLibraryImport);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "dep");
}

TEST(UsingTests, aliased_library_declaration_name_collision) {
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

struct x{};

struct B{dep.A a;}; // So the import is used.

)FIDL",
                      &shared);

  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(1, errors.size());
  ASSERT_ERR(errors[0], fidl::ErrDeclNameConflictsWithLibraryImport);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "x");
}

TEST(UsingTests, aliased_library_nonaliased_declaration_name_collision) {
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

struct dep{};

struct B{depnoconflict.A a;}; // So the import is used.

)FIDL",
                      &shared);

  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(1, errors.size());
  ASSERT_ERR(errors[0], fidl::ErrDeclNameConflictsWithLibraryImport);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "dep");
}

}  // namespace
