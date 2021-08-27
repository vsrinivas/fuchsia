// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "error_test.h"
#include "fidl/names.h"
#include "test_library.h"

namespace {

TEST(DirectDependenciesTests, GoodDirectDepsSimple) {
  for (const std::string& type_usage : {
           "dep2.Type",
           "vector<dep2.Type>",
           "array<dep2.Type, 1>",
           "box<dep2.Type>",
           "client_end:dep2.Protocol",
           "server_end:dep2.Protocol",
           "vector<uint32>:dep2.Constant",
           "array<uint32, dep2.Constant>",
       }) {
    SharedAmongstLibraries shared;
    TestLibrary dep2("dep2.fidl", R"FIDL(
  library dep2;

  const Constant uint32 = 50;
  type Type = struct {};
  protocol Protocol {};
  )FIDL",
                     &shared);
    ASSERT_COMPILED(dep2);

    TestLibrary dep1("dep1.fidl",
                     R"FIDL(
  library dep1;

  using dep2;

  protocol Foo {
    UsesDepType(resource struct { data )FIDL" +
                         type_usage + R"FIDL(; });
  };
  )FIDL",
                     &shared);
    ASSERT_TRUE(dep1.AddDependentLibrary(&dep2));
    ASSERT_COMPILED(dep1);

    TestLibrary lib("example.fidl", R"FIDL(
  library example;

  using dep1;

  protocol CapturesDependencyThroughCompose {
    compose dep1.Foo;
  };
  )FIDL",
                    &shared);
    lib.AddDependentLibrary(&dep1);
    ASSERT_COMPILED(lib);

    auto transitive_deps = lib.library()->DirectDependencies();
    ASSERT_EQ(transitive_deps.size(), 2);
    auto iter = transitive_deps.cbegin();
    EXPECT_EQ(fidl::NameLibrary((*iter++)->name()), "dep1");
    EXPECT_EQ(fidl::NameLibrary((*iter++)->name()), "dep2");
  }
}

TEST(DirectDependenciesTests, GoodDoesNotCaptureTransitiveDeps) {
  SharedAmongstLibraries shared;
  TestLibrary dep2("dep2.fidl", R"FIDL(
library dep2;

type Foo = struct {};
)FIDL",
                   &shared);
  ASSERT_COMPILED(dep2);

  TestLibrary dep1("dep1.fidl", R"FIDL(
library dep1;

using dep2;

alias Bar = dep2.Foo;

protocol Baz {
  UsesDepConst(struct { foo vector<Bar>; });
};
)FIDL",
                   &shared);
  ASSERT_TRUE(dep1.AddDependentLibrary(&dep2));
  ASSERT_COMPILED(dep1);

  TestLibrary lib("example.fidl", R"FIDL(
library example;

using dep1;

protocol CapturesDependencyThroughCompose {
  compose dep1.Baz;
};
)FIDL",
                  &shared);
  lib.AddDependentLibrary(&dep1);
  ASSERT_COMPILED(lib);

  auto transitive_deps = lib.library()->DirectDependencies();
  ASSERT_EQ(transitive_deps.size(), 1);
  auto iter = transitive_deps.cbegin();
  EXPECT_EQ(fidl::NameLibrary((*iter++)->name()), "dep1");
}

}  // namespace
