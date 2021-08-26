// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include <fidl/utils.h>
#include <zxtest/zxtest.h>

#include "error_test.h"
#include "fidl/diagnostics.h"
#include "fidl/experimental_flags.h"
#include "test_library.h"

namespace {

TEST(CanonicalNamesTests, GoodTopLevel) {
  TestLibrary library(R"FIDL(
library example;

alias foobar = bool;
const bool f_oobar = true;
struct fo_obar {};
struct foo_bar {};
table foob_ar {};
union fooba_r { 1: bool x; };
enum FoObAr { A = 1; };
bits FooBaR { A = 1; };
protocol FoObaR {};
service FOoBAR {};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(CanonicalNamesTests, GoodStructMembers) {
  TestLibrary library(R"FIDL(
library example;

struct Example {
  bool foobar;
  bool foo_bar;
  bool f_o_o_b_a_r;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(CanonicalNamesTests, GoodTableMembers) {
  TestLibrary library(R"FIDL(
library example;

table Example {
  1: bool foobar;
  2: bool foo_bar;
  3: bool f_o_o_b_a_r;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(CanonicalNamesTests, GoodUnionMembers) {
  TestLibrary library(R"FIDL(
library example;

union Example {
  1: bool foobar;
  2: bool foo_bar;
  3: bool f_o_o_b_a_r;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(CanonicalNamesTests, GoodEnumMembers) {
  TestLibrary library(R"FIDL(
library example;

enum Example {
  foobar = 1;
  foo_bar = 2;
  f_o_o_b_a_r = 3;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(CanonicalNamesTests, GoodBitsMembers) {
  TestLibrary library(R"FIDL(
library example;

bits Example {
  foobar = 1;
  foo_bar = 2;
  f_o_o_b_a_r = 4;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(CanonicalNamesTests, GoodProtocolMethods) {
  TestLibrary library(R"FIDL(
library example;

protocol Example {
  foobar() -> ();
  foo_bar() -> ();
  f_o_o_b_a_r() -> ();
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(CanonicalNamesTests, GoodMethodParameters) {
  TestLibrary library(R"FIDL(
library example;

protocol Example {
  example(
    bool foobar,
    bool foo_bar,
    bool f_o_o_b_a_r
  ) -> ();
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(CanonicalNamesTests, GoodMethodResults) {
  TestLibrary library(R"FIDL(
library example;

protocol Example {
  example() -> (
    bool foobar,
    bool foo_bar,
    bool f_o_o_b_a_r
  );
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(CanonicalNamesTests, GoodServiceMembers) {
  TestLibrary library(R"FIDL(
library example;

protocol P {};
service Example {
  P foobar;
  P foo_bar;
  P f_o_o_b_a_r;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(CanonicalNamesTests, GoodUpperAcronym) {
  TestLibrary library(R"FIDL(
library example;

struct HTTPServer {};
struct httpserver {};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(CanonicalNamesTests, GoodCurrentLibrary) {
  TestLibrary library(R"FIDL(
library example;

struct example {};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(CanonicalNamesTests, GoodDependentLibrary) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("foobar.fidl", R"FIDL(
library foobar;

struct Something {};
)FIDL",
                         &shared);
  TestLibrary converted_dependency;
  ASSERT_COMPILED_AND_CONVERT_INTO(dependency, converted_dependency);

  TestLibrary library("example.fidl", R"FIDL(
library example;

using foobar;

alias f_o_o_b_a_r = foobar.Something;
const bool f_oobar = true;
struct fo_obar {};
struct foo_bar {};
table foob_ar {};
union fooba_r { 1: bool x; };
enum FoObAr { A = 1; };
bits FooBaR { A = 1; };
protocol FoObaR {};
service FOoBAR {};
)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_COMPILED_AND_CONVERT_WITH_DEP(library, converted_dependency);
}

TEST(CanonicalNamesTests, GoodDependentLibraryWithOldDep) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("foobar.fidl", R"FIDL(
library foobar;

struct Something {};
)FIDL",
                         &shared);
  TestLibrary cloned_dependency;
  ASSERT_COMPILED_AND_CLONE_INTO(dependency, cloned_dependency);

  TestLibrary library("example.fidl", R"FIDL(
library example;

using foobar;

alias f_o_o_b_a_r = foobar.Something;
const bool f_oobar = true;
struct fo_obar {};
struct foo_bar {};
table foob_ar {};
union fooba_r { 1: bool x; };
enum FoObAr { A = 1; };
bits FooBaR { A = 1; };
protocol FoObaR {};
service FOoBAR {};
)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_COMPILED_AND_CONVERT_WITH_DEP(library, cloned_dependency);
}

TEST(CanonicalNamesTests, BadTopLevel) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  const auto lower = {
      "alias fooBar = bool;",                 // these comments prevent clang-format
      "const fooBar bool = true;",            // from packing multiple items per line
      "type fooBar = struct {};",             //
      "type fooBar = struct {};",             //
      "type fooBar = table {};",              //
      "type fooBar = union { 1: x bool; };",  //
      "type fooBar = enum { A = 1; };",       //
      "type fooBar = bits { A = 1; };",       //
      "protocol fooBar {};",                  //
      "service fooBar {};",                   //
  };
  const auto upper = {
      "alias FooBar = bool;",                 //
      "const FooBar bool = true;",            //
      "type FooBar = struct {};",             //
      "type FooBar = struct {};",             //
      "type FooBar = table {};",              //
      "type FooBar = union { 1: x bool; };",  //
      "type FooBar = enum { A = 1; };",       //
      "type FooBar = bits { A = 1; };",       //
      "protocol FooBar {};",                  //
      "service FooBar {};",                   //
  };

  for (const auto line1 : lower) {
    for (const auto line2 : upper) {
      std::ostringstream s;
      s << "library example;\n\n" << line1 << '\n' << line2 << '\n';
      const auto fidl = s.str();
      TestLibrary library(fidl, experimental_flags);
      ASSERT_FALSE(library.Compile(), "%s", fidl.c_str());
      const auto& errors = library.errors();
      ASSERT_EQ(errors.size(), 1, "%s", fidl.c_str());
      ASSERT_ERR(errors[0], fidl::ErrNameCollisionCanonical, "%s", fidl.c_str());
      ASSERT_SUBSTR(errors[0]->msg.c_str(), "fooBar", "%s", fidl.c_str());
      ASSERT_SUBSTR(errors[0]->msg.c_str(), "FooBar", "%s", fidl.c_str());
      ASSERT_SUBSTR(errors[0]->msg.c_str(), "foo_bar", "%s", fidl.c_str());
    }
  }
}

TEST(CanonicalNamesTests, BadStructMembers) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Example = struct {
  fooBar bool;
  FooBar bool;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateStructMemberNameCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "fooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "FooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "foo_bar");
}

TEST(CanonicalNamesTests, BadTableMembers) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Example = table {
  1: fooBar bool;
  2: FooBar bool;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateTableFieldNameCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "fooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "FooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "foo_bar");
}

TEST(CanonicalNamesTests, BadUnionMembers) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Example = union {
  1: fooBar bool;
  2: FooBar bool;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateUnionMemberNameCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "fooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "FooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "foo_bar");
}

TEST(CanonicalNamesTests, BadEnumMembers) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Example = enum {
  fooBar = 1;
  FooBar = 2;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateMemberNameCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "fooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "FooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "foo_bar");
}

TEST(CanonicalNamesTests, BadBitsMembers) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Example = bits {
  fooBar = 1;
  FooBar = 2;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateMemberNameCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "fooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "FooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "foo_bar");
}

TEST(CanonicalNamesTests, BadProtocolMethods) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol Example {
  fooBar() -> ();
  FooBar() -> ();
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateMethodNameCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "fooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "FooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "foo_bar");
}

TEST(CanonicalNamesTests, BadMethodParameters) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol Example {
  example(struct { fooBar bool; FooBar bool; }) -> ();
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateMethodParameterNameCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "fooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "FooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "foo_bar");
}

TEST(CanonicalNamesTests, BadMethodResults) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol Example {
  example() -> (struct { fooBar bool; FooBar bool; });
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateMethodParameterNameCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "fooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "FooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "foo_bar");
}

TEST(CanonicalNamesTests, BadServiceMembers) {
  TestLibrary library(R"FIDL(
library example;

protocol P {};
service Example {
  P fooBar;
  P FooBar;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateServiceMemberNameCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "fooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "FooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "foo_bar");
}

TEST(CanonicalNamesTests, BadUpperAcronym) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type HTTPServer = struct {};
type HttpServer = struct {};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameCollisionCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "HTTPServer");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "HttpServer");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "http_server");
}

TEST(CanonicalNamesTests, BadDependentLibrary) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  SharedAmongstLibraries shared;
  TestLibrary dependency("foobar.fidl", R"FIDL(
library foobar;

struct Something {};
)FIDL",
                         &shared);
  TestLibrary converted_dependency;
  ASSERT_COMPILED_AND_CONVERT_INTO(dependency, converted_dependency);

  TestLibrary library("lib.fidl", R"FIDL(
library example;

using foobar;

alias FOOBAR = foobar.Something;
)FIDL",
                      &shared, experimental_flags);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(converted_dependency)));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDeclNameConflictsWithLibraryImportCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "FOOBAR");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "foobar");
}

TEST(CanonicalNamesTests, BadDependentLibraryWithOldDep) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  SharedAmongstLibraries shared;
  TestLibrary dependency("foobar.fidl", R"FIDL(
library foobar;

struct Something {};
)FIDL",
                         &shared);
  ASSERT_TRUE(dependency.Compile());

  TestLibrary library("lib.fidl", R"FIDL(
library example;

using foobar;

alias FOOBAR = foobar.Something;
)FIDL",
                      &shared, experimental_flags);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDeclNameConflictsWithLibraryImportCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "FOOBAR");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "foobar");
}

TEST(CanonicalNamesTests, BadVariousCollisions) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  const auto base_names = {
      "a", "a1", "x_single_start", "single_end_x", "x_single_both_x", "single_x_middle",
  };
  const auto functions = {
      fidl::utils::to_lower_snake_case,
      fidl::utils::to_upper_snake_case,
      fidl::utils::to_lower_camel_case,
      fidl::utils::to_upper_camel_case,
  };

  for (const auto base_name : base_names) {
    for (const auto f1 : functions) {
      for (const auto f2 : functions) {
        std::ostringstream s;
        const auto name1 = f1(base_name);
        const auto name2 = f2(base_name);
        s << "library example;\n\ntype " << name1 << " = struct {};\ntype " << name2
          << " = struct {};\n";
        const auto fidl = s.str();
        TestLibrary library(fidl, experimental_flags);
        ASSERT_FALSE(library.Compile(), "%s", fidl.c_str());
        const auto& errors = library.errors();
        ASSERT_EQ(errors.size(), 1, "%s", fidl.c_str());
        if (name1 == name2) {
          ASSERT_ERR(errors[0], fidl::ErrNameCollision, "%s", fidl.c_str());
          ASSERT_SUBSTR(errors[0]->msg.c_str(), name1.c_str(), "%s", fidl.c_str());
        } else {
          ASSERT_ERR(errors[0], fidl::ErrNameCollisionCanonical, "%s", fidl.c_str());
          ASSERT_SUBSTR(errors[0]->msg.c_str(), name1.c_str(), "%s", fidl.c_str());
          ASSERT_SUBSTR(errors[0]->msg.c_str(), name2.c_str(), "%s", fidl.c_str());
          ASSERT_SUBSTR(errors[0]->msg.c_str(), fidl::utils::canonicalize(name1).c_str(), "%s",
                        fidl.c_str());
        }
      }
    }
  }
}

TEST(CanonicalNamesTests, BadConsecutiveUnderscores) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type it_is_the_same = struct {};
type it__is___the____same = struct {};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameCollisionCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "it_is_the_same");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "it__is___the____same");
}

TEST(CanonicalNamesTests, BadInconsistentTypeSpelling) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  const auto decl_templates = {
      "alias %s = bool;",                 //
      "type %s = struct {};",             //
      "type %s = struct {};",             //
      "type %s = table {};",              //
      "type %s = union { 1: x bool; };",  //
      "type %s = enum { A = 1; };",       //
      "type %s = bits { A = 1; };",       //
  };
  const auto use_template = "type Example = struct { val %s; };";

  const auto names = {
      std::make_pair("foo_bar", "FOO_BAR"),
      std::make_pair("FOO_BAR", "foo_bar"),
      std::make_pair("fooBar", "FooBar"),
  };

  for (const auto decl_template : decl_templates) {
    for (const auto [decl_name, use_name] : names) {
      std::string decl(decl_template), use(use_template);
      decl.replace(decl.find("%s"), 2, decl_name);
      use.replace(use.find("%s"), 2, use_name);
      std::ostringstream s;
      s << "library example;\n\n" << decl << '\n' << use << '\n';
      const auto fidl = s.str();
      TestLibrary library(fidl, experimental_flags);
      ASSERT_FALSE(library.Compile(), "%s", fidl.c_str());
      const auto& errors = library.errors();
      ASSERT_EQ(errors.size(), 1, "%s", fidl.c_str());
      ASSERT_ERR(errors[0], fidl::ErrUnknownType, "%s", fidl.c_str());
      ASSERT_SUBSTR(errors[0]->msg.c_str(), use_name, "%s", fidl.c_str());
    }
  }
}

TEST(CanonicalNamesTests, BadInconsistentConstSpelling) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  const auto names = {
      std::make_pair("foo_bar", "FOO_BAR"),
      std::make_pair("FOO_BAR", "foo_bar"),
      std::make_pair("fooBar", "FooBar"),
  };

  for (const auto [decl_name, use_name] : names) {
    std::ostringstream s;
    s << "library example;\n\n"
      << "const " << decl_name << " bool = false;\n"
      << "const EXAMPLE bool = " << use_name << ";\n";
    const auto fidl = s.str();
    TestLibrary library(fidl, experimental_flags);
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotResolveConstantValue);
  }
}

TEST(CanonicalNamesTests, BadInconsistentEnumMemberSpelling) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  const auto names = {
      std::make_pair("foo_bar", "FOO_BAR"),
      std::make_pair("FOO_BAR", "foo_bar"),
      std::make_pair("fooBar", "FooBar"),
  };

  for (const auto [decl_name, use_name] : names) {
    std::ostringstream s;
    s << "library example;\n\n"
      << "type Enum = enum { " << decl_name << " = 1; };\n"
      << "const EXAMPLE Enum = Enum." << use_name << ";\n";
    const auto fidl = s.str();
    TestLibrary library(fidl, experimental_flags);
    ASSERT_FALSE(library.Compile(), "%s", fidl.c_str());
    const auto& errors = library.errors();
    ASSERT_EQ(errors.size(), 2, "%s", fidl.c_str());
    ASSERT_ERR(errors[0], fidl::ErrUnknownEnumMember, "%s", fidl.c_str());
    ASSERT_SUBSTR(errors[0]->msg.c_str(), use_name, "%s", fidl.c_str());
    ASSERT_ERR(errors[1], fidl::ErrCannotResolveConstantValue, "%s", fidl.c_str());
  }
}

TEST(CanonicalNamesTests, BadInconsistentBitsMemberSpelling) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  const auto names = {
      std::make_pair("foo_bar", "FOO_BAR"),
      std::make_pair("FOO_BAR", "foo_bar"),
      std::make_pair("fooBar", "FooBar"),
  };

  for (const auto [decl_name, use_name] : names) {
    std::ostringstream s;
    s << "library example;\n\n"
      << "type Bits = bits { " << decl_name << " = 1; };\n"
      << "const EXAMPLE Bits = Bits." << use_name << ";\n";
    const auto fidl = s.str();
    TestLibrary library(fidl, experimental_flags);
    ASSERT_FALSE(library.Compile(), "%s", fidl.c_str());
    const auto& errors = library.errors();
    ASSERT_EQ(errors.size(), 2, "%s", fidl.c_str());
    ASSERT_ERR(errors[0], fidl::ErrUnknownBitsMember, "%s", fidl.c_str());
    ASSERT_SUBSTR(errors[0]->msg.c_str(), use_name, "%s", fidl.c_str());
    ASSERT_ERR(errors[1], fidl::ErrCannotResolveConstantValue, "%s", fidl.c_str());
  }
}

}  // namespace
