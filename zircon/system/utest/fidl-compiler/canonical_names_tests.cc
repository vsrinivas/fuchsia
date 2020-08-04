// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include <fidl/utils.h>
#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

TEST(CanonicalNamesTests, GoodTopLevel) {
  TestLibrary library(R"FIDL(
library example;

using foobar = bool;
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
  ASSERT_TRUE(library.Compile());
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
  ASSERT_TRUE(library.Compile());
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
  ASSERT_TRUE(library.Compile());
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
  ASSERT_TRUE(library.Compile());
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
  ASSERT_TRUE(library.Compile());
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
  ASSERT_TRUE(library.Compile());
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
  ASSERT_TRUE(library.Compile());
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
  ASSERT_TRUE(library.Compile());
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
  ASSERT_TRUE(library.Compile());
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
  ASSERT_TRUE(library.Compile());
}

TEST(CanonicalNamesTests, GoodUpperAcronym) {
  TestLibrary library(R"FIDL(
library example;

struct HTTPServer {};
struct httpserver {};
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(CanonicalNamesTests, GoodCurrentLibrary) {
  TestLibrary library(R"FIDL(
library example;

struct example {};
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(CanonicalNamesTests, GoodDependentLibrary) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("foobar.fidl", R"FIDL(
library foobar;

struct Something {};
)FIDL",
                         &shared);
  ASSERT_TRUE(dependency.Compile());

  TestLibrary library(R"FIDL(
library example;

using foobar;

using f_o_o_b_a_r = foobar.Something;
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
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_TRUE(library.Compile());
}

TEST(CanonicalNamesTests, BadTopLevel) {
  const auto lower = {
      "using fooBar = bool;",          // these comments prevent clang-format
      "const bool fooBar = true;",     // from packing multiple items per line
      "struct fooBar {};",             //
      "struct fooBar {};",             //
      "table fooBar {};",              //
      "union fooBar { 1: bool x; };",  //
      "enum fooBar { A = 1; };",       //
      "bits fooBar { A = 1; };",       //
      "protocol fooBar {};",           //
      "service fooBar {};",            //
  };
  const auto upper = {
      "using FooBar = bool;",          //
      "const bool FooBar = true;",     //
      "struct FooBar {};",             //
      "struct FooBar {};",             //
      "table FooBar {};",              //
      "union FooBar { 1: bool x; };",  //
      "enum FooBar { A = 1; };",       //
      "bits FooBar { A = 1; };",       //
      "protocol FooBar {};",           //
      "service FooBar {};",            //
  };

  for (const auto line1 : lower) {
    for (const auto line2 : upper) {
      std::ostringstream s;
      s << "library example;\n\n" << line1 << '\n' << line2 << '\n';
      const auto fidl = s.str();
      TestLibrary library(fidl);
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
  TestLibrary library(R"FIDL(
library example;

struct Example {
  bool fooBar;
  bool FooBar;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateStructMemberNameCanonical);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "fooBar");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "FooBar");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "foo_bar");
}

TEST(CanonicalNamesTests, BadTableMembers) {
  TestLibrary library(R"FIDL(
library example;

table Example {
  1: bool fooBar;
  2: bool FooBar;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateTableFieldNameCanonical);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "fooBar");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "FooBar");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "foo_bar");
}

TEST(CanonicalNamesTests, BadUnionMembers) {
  TestLibrary library(R"FIDL(
library example;

union Example {
  1: bool fooBar;
  2: bool FooBar;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateUnionMemberNameCanonical);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "fooBar");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "FooBar");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "foo_bar");
}

TEST(CanonicalNamesTests, BadEnumMembers) {
  TestLibrary library(R"FIDL(
library example;

enum Example {
  fooBar = 1;
  FooBar = 2;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateMemberNameCanonical);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "fooBar");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "FooBar");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "foo_bar");
}

TEST(CanonicalNamesTests, BadBitsMembers) {
  TestLibrary library(R"FIDL(
library example;

bits Example {
  fooBar = 1;
  FooBar = 2;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateMemberNameCanonical);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "fooBar");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "FooBar");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "foo_bar");
}

TEST(CanonicalNamesTests, BadProtocolMethods) {
  TestLibrary library(R"FIDL(
library example;

protocol Example {
  fooBar() -> ();
  FooBar() -> ();
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateMethodNameCanonical);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "fooBar");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "FooBar");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "foo_bar");
}

TEST(CanonicalNamesTests, BadMethodParameters) {
  TestLibrary library(R"FIDL(
library example;

protocol Example {
  example(bool fooBar, bool FooBar) -> ();
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateMethodParameterNameCanonical);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "fooBar");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "FooBar");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "foo_bar");
}

TEST(CanonicalNamesTests, BadMethodResults) {
  TestLibrary library(R"FIDL(
library example;

protocol Example {
  example() -> (bool fooBar, bool FooBar);
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateMethodParameterNameCanonical);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "fooBar");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "FooBar");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "foo_bar");
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
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateServiceMemberNameCanonical);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "fooBar");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "FooBar");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "foo_bar");
}

TEST(CanonicalNamesTests, BadUpperAcronym) {
  TestLibrary library(R"FIDL(
library example;

struct HTTPServer {};
struct HttpServer {};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrNameCollisionCanonical);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "HTTPServer");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "HttpServer");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "http_server");
}

TEST(CanonicalNamesTests, BadDependentLibrary) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("foobar.fidl", R"FIDL(
library foobar;

struct Something {};
)FIDL",
                         &shared);
  ASSERT_TRUE(dependency.Compile());

  TestLibrary library(R"FIDL(
library example;

using foobar;

using FOOBAR = foobar.Something;
)FIDL");
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDeclNameConflictsWithLibraryImportCanonical);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "FOOBAR");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "foobar");
}

TEST(CanonicalNamesTests, BadVariousCollisions) {
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
        s << "library example;\n\nstruct " << name1 << " {};\nstruct " << name2 << " {};\n";
        const auto fidl = s.str();
        TestLibrary library(fidl);
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
  TestLibrary library(R"FIDL(
library example;

struct it_is_the_same {};
struct it__is___the____same {};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrNameCollisionCanonical);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "it_is_the_same");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "it__is___the____same");
}

TEST(CanonicalNamesTests, BadInconsistentTypeSpelling) {
  const auto decl_templates = {
      "using %s = bool;",          //
      "struct %s {};",             //
      "struct %s {};",             //
      "table %s {};",              //
      "union %s { 1: bool x; };",  //
      "enum %s { A = 1; };",       //
      "bits %s { A = 1; };",       //
  };
  const auto use_template = "struct Example { %s val; };";

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
      TestLibrary library(fidl);
      ASSERT_FALSE(library.Compile(), "%s", fidl.c_str());
      const auto& errors = library.errors();
      ASSERT_EQ(errors.size(), 1, "%s", fidl.c_str());
      ASSERT_ERR(errors[0], fidl::ErrUnknownType, "%s", fidl.c_str());
      ASSERT_SUBSTR(errors[0]->msg.c_str(), use_name, "%s", fidl.c_str());
    }
  }
}

TEST(CanonicalNamesTests, BadInconsistentConstSpelling) {
  const auto names = {
      std::make_pair("foo_bar", "FOO_BAR"),
      std::make_pair("FOO_BAR", "foo_bar"),
      std::make_pair("fooBar", "FooBar"),
  };

  for (const auto [decl_name, use_name] : names) {
    std::ostringstream s;
    s << "library example;\n\n"
      << "const bool " << decl_name << " = false;\n"
      << "const bool EXAMPLE = " << use_name << ";\n";
    const auto fidl = s.str();
    TestLibrary library(fidl);
    ASSERT_FALSE(library.Compile(), "%s", fidl.c_str());
    const auto& errors = library.errors();
    ASSERT_EQ(errors.size(), 1, "%s", fidl.c_str());
    ASSERT_ERR(errors[0], fidl::ErrFailedConstantLookup, "%s", fidl.c_str());
    ASSERT_SUBSTR(errors[0]->msg.c_str(), use_name, "%s", fidl.c_str());
  }
}

TEST(CanonicalNamesTests, BadInconsistentEnumMemberSpelling) {
  const auto names = {
      std::make_pair("foo_bar", "FOO_BAR"),
      std::make_pair("FOO_BAR", "foo_bar"),
      std::make_pair("fooBar", "FooBar"),
  };

  for (const auto [decl_name, use_name] : names) {
    std::ostringstream s;
    s << "library example;\n\n"
      << "enum Enum { " << decl_name << " = 1; };\n"
      << "const Enum EXAMPLE = Enum." << use_name << ";\n";
    const auto fidl = s.str();
    TestLibrary library(fidl);
    ASSERT_FALSE(library.Compile(), "%s", fidl.c_str());
    const auto& errors = library.errors();
    ASSERT_EQ(errors.size(), 2, "%s", fidl.c_str());
    ASSERT_ERR(errors[0], fidl::ErrUnknownEnumMember, "%s", fidl.c_str());
    ASSERT_SUBSTR(errors[0]->msg.c_str(), use_name, "%s", fidl.c_str());
    ASSERT_ERR(errors[1], fidl::ErrCannotResolveConstantValue, "%s", fidl.c_str());
  }
}

TEST(CanonicalNamesTests, BadInconsistentBitsMemberSpelling) {
  const auto names = {
      std::make_pair("foo_bar", "FOO_BAR"),
      std::make_pair("FOO_BAR", "foo_bar"),
      std::make_pair("fooBar", "FooBar"),
  };

  for (const auto [decl_name, use_name] : names) {
    std::ostringstream s;
    s << "library example;\n\n"
      << "bits Bits { " << decl_name << " = 1; };\n"
      << "const Bits EXAMPLE = Bits." << use_name << ";\n";
    const auto fidl = s.str();
    TestLibrary library(fidl);
    ASSERT_FALSE(library.Compile(), "%s", fidl.c_str());
    const auto& errors = library.errors();
    ASSERT_EQ(errors.size(), 2, "%s", fidl.c_str());
    ASSERT_ERR(errors[0], fidl::ErrUnknownBitsMember, "%s", fidl.c_str());
    ASSERT_SUBSTR(errors[0]->msg.c_str(), use_name, "%s", fidl.c_str());
    ASSERT_ERR(errors[1], fidl::ErrCannotResolveConstantValue, "%s", fidl.c_str());
  }
}

}  // namespace
