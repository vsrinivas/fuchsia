// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include <fidl/utils.h>
#include <unittest/unittest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

const fidl::ExperimentalFlags FLAGS(fidl::ExperimentalFlags::Flag::kUniqueCanonicalNames);

bool GoodTopLevel() {
  BEGIN_TEST;

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
)FIDL",
                      FLAGS);
  ASSERT_TRUE(library.Compile());

  END_TEST;
}

bool GoodStructMembers() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct Example {
  bool foobar;
  bool foo_bar;
  bool f_o_o_b_a_r;
};
)FIDL",
                      FLAGS);
  ASSERT_TRUE(library.Compile());

  END_TEST;
}

bool GoodTableMembers() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

table Example {
  1: bool foobar;
  2: bool foo_bar;
  3: bool f_o_o_b_a_r;
};
)FIDL",
                      FLAGS);
  ASSERT_TRUE(library.Compile());

  END_TEST;
}

bool GoodUnionMembers() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

union Example {
  1: bool foobar;
  2: bool foo_bar;
  3: bool f_o_o_b_a_r;
};
)FIDL",
                      FLAGS);
  ASSERT_TRUE(library.Compile());

  END_TEST;
}

bool GoodEnumMembers() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

enum Example {
  foobar = 1;
  foo_bar = 2;
  f_o_o_b_a_r = 3;
};
)FIDL",
                      FLAGS);
  ASSERT_TRUE(library.Compile());

  END_TEST;
}

bool GoodBitsMembers() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

bits Example {
  foobar = 1;
  foo_bar = 2;
  f_o_o_b_a_r = 4;
};
)FIDL",
                      FLAGS);
  ASSERT_TRUE(library.Compile());

  END_TEST;
}

bool GoodProtocolMethods() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

protocol Example {
  foobar() -> ();
  foo_bar() -> ();
  f_o_o_b_a_r() -> ();
};
)FIDL",
                      FLAGS);
  ASSERT_TRUE(library.Compile());

  END_TEST;
}

bool GoodMethodParameters() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

protocol Example {
  example(
    bool foobar,
    bool foo_bar,
    bool f_o_o_b_a_r
  ) -> ();
};
)FIDL",
                      FLAGS);
  ASSERT_TRUE(library.Compile());

  END_TEST;
}

bool GoodMethodResults() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

protocol Example {
  example() -> (
    bool foobar,
    bool foo_bar,
    bool f_o_o_b_a_r
  );
};
)FIDL",
                      FLAGS);
  ASSERT_TRUE(library.Compile());

  END_TEST;
}

bool GoodServiceMembers() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

protocol P {};
service Example {
  P foobar;
  P foo_bar;
  P f_o_o_b_a_r;
};
)FIDL",
                      FLAGS);
  ASSERT_TRUE(library.Compile());

  END_TEST;
}

bool GoodUpperAcronym() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct HTTPServer {};
struct httpserver {};
)FIDL",
                      FLAGS);
  ASSERT_TRUE(library.Compile());

  END_TEST;
}

bool GoodCurrentLibrary() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct example {};
)FIDL",
                      FLAGS);
  ASSERT_TRUE(library.Compile());

  END_TEST;
}

bool GoodDependentLibrary() {
  BEGIN_TEST;

  SharedAmongstLibraries shared;
  TestLibrary dependency("foobar.fidl", R"FIDL(
library foobar;

struct Something {};
)FIDL",
                         &shared, FLAGS);
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
)FIDL",
                      FLAGS);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_TRUE(library.Compile());

  END_TEST;
}

bool BadTopLevel() {
  BEGIN_TEST;

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
      TestLibrary library(fidl, FLAGS);
      // TODO(fxb/49994): Add the `<< fidl` when this is using gtest.
      ASSERT_FALSE(library.Compile());  // << fidl;
      const auto& errors = library.errors();
      ASSERT_EQ(errors.size(), 1);                             // << fidl;
      ASSERT_ERR(errors[0], fidl::ErrNameCollisionCanonical);  // << fidl;
      ASSERT_STR_STR(errors[0]->msg.c_str(), "fooBar");        // << fidl;
      ASSERT_STR_STR(errors[0]->msg.c_str(), "FooBar");        // << fidl;
      ASSERT_STR_STR(errors[0]->msg.c_str(), "foo_bar");       // << fidl;
    }
  }

  END_TEST;
}

bool BadStructMembers() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct Example {
  bool fooBar;
  bool FooBar;
};
)FIDL",
                      FLAGS);
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateStructMemberNameCanonical);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "fooBar");
  ASSERT_STR_STR(errors[0]->msg.c_str(), "FooBar");
  ASSERT_STR_STR(errors[0]->msg.c_str(), "foo_bar");

  END_TEST;
}

bool BadTableMembers() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

table Example {
  1: bool fooBar;
  2: bool FooBar;
};
)FIDL",
                      FLAGS);
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateTableFieldNameCanonical);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "fooBar");
  ASSERT_STR_STR(errors[0]->msg.c_str(), "FooBar");
  ASSERT_STR_STR(errors[0]->msg.c_str(), "foo_bar");

  END_TEST;
}

bool BadUnionMembers() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

union Example {
  1: bool fooBar;
  2: bool FooBar;
};
)FIDL",
                      FLAGS);
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateUnionMemberNameCanonical);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "fooBar");
  ASSERT_STR_STR(errors[0]->msg.c_str(), "FooBar");
  ASSERT_STR_STR(errors[0]->msg.c_str(), "foo_bar");

  END_TEST;
}

bool BadEnumMembers() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

enum Example {
  fooBar = 1;
  FooBar = 2;
};
)FIDL",
                      FLAGS);
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateMemberNameCanonical);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "fooBar");
  ASSERT_STR_STR(errors[0]->msg.c_str(), "FooBar");
  ASSERT_STR_STR(errors[0]->msg.c_str(), "foo_bar");

  END_TEST;
}

bool BadBitsMembers() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

bits Example {
  fooBar = 1;
  FooBar = 2;
};
)FIDL",
                      FLAGS);
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateMemberNameCanonical);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "fooBar");
  ASSERT_STR_STR(errors[0]->msg.c_str(), "FooBar");
  ASSERT_STR_STR(errors[0]->msg.c_str(), "foo_bar");

  END_TEST;
}

bool BadProtocolMethods() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

protocol Example {
  fooBar() -> ();
  FooBar() -> ();
};
)FIDL",
                      FLAGS);
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateMethodNameCanonical);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "fooBar");
  ASSERT_STR_STR(errors[0]->msg.c_str(), "FooBar");
  ASSERT_STR_STR(errors[0]->msg.c_str(), "foo_bar");

  END_TEST;
}

bool BadMethodParameters() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

protocol Example {
  example(bool fooBar, bool FooBar) -> ();
};
)FIDL",
                      FLAGS);
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateMethodParameterNameCanonical);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "fooBar");
  ASSERT_STR_STR(errors[0]->msg.c_str(), "FooBar");
  ASSERT_STR_STR(errors[0]->msg.c_str(), "foo_bar");

  END_TEST;
}

bool BadMethodResults() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

protocol Example {
  example() -> (bool fooBar, bool FooBar);
};
)FIDL",
                      FLAGS);
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateMethodParameterNameCanonical);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "fooBar");
  ASSERT_STR_STR(errors[0]->msg.c_str(), "FooBar");
  ASSERT_STR_STR(errors[0]->msg.c_str(), "foo_bar");
  END_TEST;
}

bool BadServiceMembers() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

protocol P {};
service Example {
  P fooBar;
  P FooBar;
};
)FIDL",
                      FLAGS);
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateServiceMemberNameCanonical);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "fooBar");
  ASSERT_STR_STR(errors[0]->msg.c_str(), "FooBar");
  ASSERT_STR_STR(errors[0]->msg.c_str(), "foo_bar");

  END_TEST;
}

bool BadUpperAcronym() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct HTTPServer {};
struct HttpServer {};
)FIDL",
                      FLAGS);
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrNameCollisionCanonical);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "HTTPServer");
  ASSERT_STR_STR(errors[0]->msg.c_str(), "HttpServer");
  ASSERT_STR_STR(errors[0]->msg.c_str(), "http_server");

  END_TEST;
}

bool BadDependentLibrary() {
  BEGIN_TEST;

  SharedAmongstLibraries shared;
  TestLibrary dependency("foobar.fidl", R"FIDL(
library foobar;

struct Something {};
)FIDL",
                         &shared, FLAGS);
  ASSERT_TRUE(dependency.Compile());

  TestLibrary library(R"FIDL(
library example;

using foobar;

using FOOBAR = foobar.Something;
)FIDL",
                      FLAGS);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDeclNameConflictsWithLibraryImportCanonical);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "FOOBAR");
  ASSERT_STR_STR(errors[0]->msg.c_str(), "foobar");

  END_TEST;
}

bool BadVariousCollisions() {
  BEGIN_TEST;

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
        TestLibrary library(fidl, FLAGS);
        // TODO(fxb/49994): Add the `<< fidl` when this is using gtest.
        ASSERT_FALSE(library.Compile());  // << fidl;
        const auto& errors = library.errors();
        ASSERT_EQ(errors.size(), 1);  // << fidl;
        if (name1 == name2) {
          ASSERT_ERR(errors[0], fidl::ErrNameCollision);          // << fidl;
          ASSERT_STR_STR(errors[0]->msg.c_str(), name1.c_str());  // << fidl;
        } else {
          ASSERT_ERR(errors[0], fidl::ErrNameCollisionCanonical);  // << fidl;
          ASSERT_STR_STR(errors[0]->msg.c_str(), name1.c_str());   // << fidl;
          ASSERT_STR_STR(errors[0]->msg.c_str(), name2.c_str());   // << fidl;
          ASSERT_STR_STR(errors[0]->msg.c_str(),
                         fidl::utils::canonicalize(name1).c_str());  // << fidl;
        }
      }
    }
  }

  END_TEST;
}

bool BadConsecutiveUnderscores() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct it_is_the_same {};
struct it__is___the____same {};
)FIDL",
                      FLAGS);
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrNameCollisionCanonical);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "it_is_the_same");
  ASSERT_STR_STR(errors[0]->msg.c_str(), "it__is___the____same");

  END_TEST;
}

bool BadInconsistentTypeSpelling() {
  BEGIN_TEST;

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
      TestLibrary library(fidl, FLAGS);
      // TODO(fxb/49994): Add the `<< fidl` when this is using gtest.
      ASSERT_FALSE(library.Compile());  // << fidl;
      const auto& errors = library.errors();
      ASSERT_EQ(errors.size(), 1);                       // << fidl;
      ASSERT_ERR(errors[0], fidl::ErrUnknownType);       // << fidl;
      ASSERT_STR_STR(errors[0]->msg.c_str(), use_name);  // << fidl;
    }
  }

  END_TEST;
}

bool BadInconsistentConstSpelling() {
  BEGIN_TEST;

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
    TestLibrary library(fidl, FLAGS);
    // TODO(fxb/49994): Add the `<< fidl` when this is using gtest.
    ASSERT_FALSE(library.Compile());  // << fidl;
    const auto& errors = library.errors();
    ASSERT_EQ(errors.size(), 1);                           // << fidl;
    ASSERT_ERR(errors[0], fidl::ErrFailedConstantLookup);  // << fidl;
    ASSERT_STR_STR(errors[0]->msg.c_str(), use_name);      // << fidl;
  }

  END_TEST;
}

bool BadInconsistentEnumMemberSpelling() {
  BEGIN_TEST;

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
    TestLibrary library(fidl, FLAGS);
    // TODO(fxb/49994): Add the `<< fidl` when this is using gtest.
    ASSERT_FALSE(library.Compile());  // << fidl;
    const auto& errors = library.errors();
    ASSERT_EQ(errors.size(), 2);                                 // << fidl;
    ASSERT_ERR(errors[0], fidl::ErrUnknownEnumMember);           // << fidl;
    ASSERT_STR_STR(errors[0]->msg.c_str(), use_name);            // << fidl;
    ASSERT_ERR(errors[1], fidl::ErrCannotResolveConstantValue);  // << fidl;
  }

  END_TEST;
}

bool BadInconsistentBitsMemberSpelling() {
  BEGIN_TEST;

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
    TestLibrary library(fidl, FLAGS);
    // TODO(fxb/49994): Add the `<< fidl` when this is using gtest.
    ASSERT_FALSE(library.Compile());  // << fidl;
    const auto& errors = library.errors();
    ASSERT_EQ(errors.size(), 2);                                 // << fidl;
    ASSERT_ERR(errors[0], fidl::ErrUnknownBitsMember);           // << fidl;
    ASSERT_STR_STR(errors[0]->msg.c_str(), use_name);            // << fidl;
    ASSERT_ERR(errors[1], fidl::ErrCannotResolveConstantValue);  // << fidl;
  }

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(canonical_names_tests)

RUN_TEST(GoodTopLevel)
RUN_TEST(GoodStructMembers)
RUN_TEST(GoodTableMembers)
RUN_TEST(GoodUnionMembers)
RUN_TEST(GoodEnumMembers)
RUN_TEST(GoodBitsMembers)
RUN_TEST(GoodProtocolMethods)
RUN_TEST(GoodMethodParameters)
RUN_TEST(GoodMethodResults)
RUN_TEST(GoodServiceMembers)
RUN_TEST(GoodUpperAcronym)
RUN_TEST(GoodCurrentLibrary)
RUN_TEST(GoodDependentLibrary)

RUN_TEST(BadTopLevel)
RUN_TEST(BadStructMembers)
RUN_TEST(BadTableMembers)
RUN_TEST(BadUnionMembers)
RUN_TEST(BadEnumMembers)
RUN_TEST(BadBitsMembers)
RUN_TEST(BadProtocolMethods)
RUN_TEST(BadMethodParameters)
RUN_TEST(BadMethodResults)
RUN_TEST(BadServiceMembers)
RUN_TEST(BadUpperAcronym)
RUN_TEST(BadDependentLibrary)
RUN_TEST(BadVariousCollisions)
RUN_TEST(BadConsecutiveUnderscores)
RUN_TEST(BadInconsistentTypeSpelling)
RUN_TEST(BadInconsistentConstSpelling)
RUN_TEST(BadInconsistentEnumMemberSpelling)
RUN_TEST(BadInconsistentBitsMemberSpelling)

END_TEST_CASE(canonical_names_tests)
