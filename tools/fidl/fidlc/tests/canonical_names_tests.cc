// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/include/fidl/diagnostics.h"
#include "tools/fidl/fidlc/include/fidl/experimental_flags.h"
#include "tools/fidl/fidlc/include/fidl/utils.h"
#include "tools/fidl/fidlc/tests/error_test.h"
#include "tools/fidl/fidlc/tests/test_library.h"

namespace {

TEST(CanonicalNamesTests, BadCollision) {
  TestLibrary library;
  library.AddFile("bad/fi-0035.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameCollisionCanonical);
}

TEST(CanonicalNamesTests, GoodCollisionFixRename) {
  TestLibrary library;
  library.AddFile("good/fi-0035.test.fidl");
  ASSERT_COMPILED(library);
}

TEST(CanonicalNamesTests, GoodTopLevel) {
  TestLibrary library(R"FIDL(library example;

alias foobar = bool;
const f_oobar bool = true;
type fo_obar = struct {};
type foo_bar = struct {};
type foob_ar = table {};
type fooba_r = strict union {
    1: x bool;
};
type FoObAr = strict enum {
    A = 1;
};
type FooBaR = strict bits {
    A = 1;
};
protocol FoObaR {};
service FOoBAR {};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(CanonicalNamesTests, GoodAttributes) {
  TestLibrary library(R"FIDL(library example;

@foobar
@foo_bar
@f_o_o_b_a_r
type Example = struct {};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(CanonicalNamesTests, GoodAttributeArguments) {
  TestLibrary library(R"FIDL(library example;

@some_attribute(foobar="", foo_bar="", f_o_o_b_a_r="")
type Example = struct {};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(CanonicalNamesTests, GoodStructMembers) {
  TestLibrary library(R"FIDL(library example;

type Example = struct {
    foobar bool;
    foo_bar bool;
    f_o_o_b_a_r bool;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(CanonicalNamesTests, GoodTableMembers) {
  TestLibrary library(R"FIDL(library example;

type Example = table {
    1: foobar bool;
    2: foo_bar bool;
    3: f_o_o_b_a_r bool;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(CanonicalNamesTests, GoodUnionMembers) {
  TestLibrary library(R"FIDL(library example;

type Example = strict union {
    1: foobar bool;
    2: foo_bar bool;
    3: f_o_o_b_a_r bool;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(CanonicalNamesTests, GoodEnumMembers) {
  TestLibrary library(R"FIDL(library example;

type Example = strict enum {
    foobar = 1;
    foo_bar = 2;
    f_o_o_b_a_r = 3;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(CanonicalNamesTests, GoodBitsMembers) {
  TestLibrary library(R"FIDL(library example;

type Example = strict bits {
    foobar = 1;
    foo_bar = 2;
    f_o_o_b_a_r = 4;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(CanonicalNamesTests, GoodProtocolMethods) {
  TestLibrary library(R"FIDL(library example;

protocol Example {
    foobar() -> ();
    foo_bar() -> ();
    f_o_o_b_a_r() -> ();
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(CanonicalNamesTests, GoodMethodParameters) {
  TestLibrary library(R"FIDL(library example;

protocol Example {
    example(struct {
        foobar bool;
        foo_bar bool;
        f_o_o_b_a_r bool;
    }) -> ();
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(CanonicalNamesTests, GoodMethodResults) {
  TestLibrary library(R"FIDL(library example;

protocol Example {
    example() -> (struct {
        foobar bool;
        foo_bar bool;
        f_o_o_b_a_r bool;
    });
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(CanonicalNamesTests, GoodServiceMembers) {
  TestLibrary library(R"FIDL(library example;

protocol P {};
service Example {
    foobar client_end:P;
    foo_bar client_end:P;
    f_o_o_b_a_r client_end:P;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(CanonicalNamesTests, GoodResourceProperties) {
  TestLibrary library(R"FIDL(library example;

resource_definition Example {
    properties {
        foobar uint32;
        foo_bar uint32;
        f_o_o_b_a_r uint32;
    };
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(CanonicalNamesTests, GoodUpperAcronym) {
  TestLibrary library(R"FIDL(library example;

type HTTPServer = struct {};
type httpserver = struct {};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(CanonicalNamesTests, GoodCurrentLibrary) {
  TestLibrary library(R"FIDL(library example;

type example = struct {};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(CanonicalNamesTests, GoodDependentLibrary) {
  SharedAmongstLibraries shared;
  TestLibrary dependency(&shared, "foobar.fidl", R"FIDL(library foobar;

type Something = struct {};
)FIDL");
  ASSERT_COMPILED(dependency);

  TestLibrary library(&shared, "example.fidl", R"FIDL(
library example;

using foobar;

alias f_o_o_b_a_r = foobar.Something;
const f_oobar bool = true;
type fo_obar = struct {};
type foo_bar = struct {};
type foob_ar = table {};
type fooba_r = union { 1: x bool; };
type FoObAr = enum { A = 1; };
type FooBaR = bits { A = 1; };
protocol FoObaR {};
service FOoBAR {};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(CanonicalNamesTests, BadTopLevel) {
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

TEST(CanonicalNamesTests, BadAttributes) {
  TestLibrary library(R"FIDL(
library example;

@fooBar
@FooBar
type Example = struct {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateAttributeCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "fooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "FooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "foo_bar");
}

TEST(CanonicalNamesTests, BadAttributeArguments) {
  TestLibrary library(R"FIDL(
library example;

@some_attribute(fooBar="", FooBar="")
type Example = struct {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateAttributeArgCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "fooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "FooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "foo_bar");
}

TEST(CanonicalNamesTests, BadStructMembers) {
  TestLibrary library(R"FIDL(
library example;

type Example = struct {
  fooBar bool;
  FooBar bool;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateStructMemberNameCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "fooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "FooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "foo_bar");
}

TEST(CanonicalNamesTests, BadTableMembers) {
  TestLibrary library;
  library.AddFile("bad/fi-0096.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateTableFieldNameCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "myField");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "MyField");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "my_field");
}

TEST(CanonicalNamesTests, BadUnionMembers) {
  TestLibrary library;
  library.AddFile("bad/fi-0099.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateUnionMemberNameCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "myVariant");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "MyVariant");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "my_variant");
}

TEST(CanonicalNamesTests, BadEnumMembers) {
  TestLibrary library(R"FIDL(
library example;

type Example = enum {
  fooBar = 1;
  FooBar = 2;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateMemberNameCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "fooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "FooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "foo_bar");
}

TEST(CanonicalNamesTests, BadBitsMembers) {
  TestLibrary library;
  library.AddFile("bad/fi-0106.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateMemberNameCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "fooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "FooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "foo_bar");
}

TEST(CanonicalNamesTests, BadProtocolMethods) {
  TestLibrary library(R"FIDL(
library example;

protocol Example {
  fooBar() -> ();
  FooBar() -> ();
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateMethodNameCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "fooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "FooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "foo_bar");
}

TEST(CanonicalNamesTests, BadMethodParameters) {
  TestLibrary library(R"FIDL(
library example;

protocol Example {
  example(struct { fooBar bool; FooBar bool; }) -> ();
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateStructMemberNameCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "fooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "FooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "foo_bar");
}

TEST(CanonicalNamesTests, BadMethodResults) {
  TestLibrary library(R"FIDL(
library example;

protocol Example {
  example() -> (struct { fooBar bool; FooBar bool; });
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateStructMemberNameCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "fooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "FooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "foo_bar");
}

TEST(CanonicalNamesTests, BadServiceMembers) {
  TestLibrary library(R"FIDL(
library example;

protocol P {};
service Example {
  fooBar client_end:P;
  FooBar client_end:P;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateServiceMemberNameCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "fooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "FooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "foo_bar");
}

TEST(CanonicalNamesTests, BadResourceProperties) {
  TestLibrary library(R"FIDL(
library example;

resource_definition Example {
    properties {
        fooBar uint32;
        FooBar uint32;
    };
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateResourcePropertyNameCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "fooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "FooBar");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "foo_bar");
}

TEST(CanonicalNamesTests, BadUpperAcronym) {
  TestLibrary library(R"FIDL(
library example;

type HTTPServer = struct {};
type HttpServer = struct {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameCollisionCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "HTTPServer");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "HttpServer");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "http_server");
}

TEST(CanonicalNamesTests, BadDependentLibrary) {
  SharedAmongstLibraries shared;
  TestLibrary dependency(&shared, "foobar.fidl", R"FIDL(library foobar;

type Something = struct {};
)FIDL");
  ASSERT_COMPILED(dependency);

  TestLibrary library(&shared, "lib.fidl", R"FIDL(
library example;

using foobar;

alias FOOBAR = foobar.Something;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDeclNameConflictsWithLibraryImportCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "FOOBAR");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "foobar");
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
        s << "library example;\n\ntype " << name1 << " = struct {};\ntype " << name2
          << " = struct {};\n";
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

type it_is_the_same = struct {};
type it__is___the____same = struct {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameCollisionCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "it_is_the_same");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "it__is___the____same");
}

TEST(CanonicalNamesTests, BadInconsistentTypeSpelling) {
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
    for (const auto& [decl_name, use_name] : names) {
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
      ASSERT_ERR(errors[0], fidl::ErrNameNotFound, "%s", fidl.c_str());
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

  for (const auto& [decl_name, use_name] : names) {
    std::ostringstream s;
    s << "library example;\n\n"
      << "const " << decl_name << " bool = false;\n"
      << "const EXAMPLE bool = " << use_name << ";\n";
    const auto fidl = s.str();
    TestLibrary library(fidl);
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameNotFound);
  }
}

TEST(CanonicalNamesTests, BadInconsistentEnumMemberSpelling) {
  const auto names = {
      std::make_pair("foo_bar", "FOO_BAR"),
      std::make_pair("FOO_BAR", "foo_bar"),
      std::make_pair("fooBar", "FooBar"),
  };

  for (const auto& [decl_name, use_name] : names) {
    std::ostringstream s;
    s << "library example;\n\n"
      << "type Enum = enum { " << decl_name << " = 1; };\n"
      << "const EXAMPLE Enum = Enum." << use_name << ";\n";
    const auto fidl = s.str();
    TestLibrary library(fidl);
    ASSERT_FALSE(library.Compile(), "%s", fidl.c_str());
    const auto& errors = library.errors();
    ASSERT_EQ(errors.size(), 1, "%s", fidl.c_str());
    ASSERT_ERR(errors[0], fidl::ErrMemberNotFound, "%s", fidl.c_str());
  }
}

TEST(CanonicalNamesTests, BadInconsistentBitsMemberSpelling) {
  const auto names = {
      std::make_pair("foo_bar", "FOO_BAR"),
      std::make_pair("FOO_BAR", "foo_bar"),
      std::make_pair("fooBar", "FooBar"),
  };

  for (const auto& [decl_name, use_name] : names) {
    std::ostringstream s;
    s << "library example;\n\n"
      << "type Bits = bits { " << decl_name << " = 1; };\n"
      << "const EXAMPLE Bits = Bits." << use_name << ";\n";
    const auto fidl = s.str();
    TestLibrary library(fidl);
    ASSERT_FALSE(library.Compile(), "%s", fidl.c_str());
    const auto& errors = library.errors();
    ASSERT_EQ(errors.size(), 1, "%s", fidl.c_str());
    ASSERT_ERR(errors[0], fidl::ErrMemberNotFound, "%s", fidl.c_str());
  }
}

}  // namespace
