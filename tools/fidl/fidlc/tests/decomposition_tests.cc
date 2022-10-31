// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/tests/error_test.h"
#include "tools/fidl/fidlc/tests/test_library.h"

// This file tests the temporal decomposition algorithm by comparing the JSON IR
// resulting from a versioned library and its manually decomposed equivalents.
// See also versioning_tests.cc and availability_interleaving_tests.cc.

namespace {

// Returns true if str starts with prefix.
bool StartsWith(std::string_view str, std::string_view prefix) {
  return str.substr(0, prefix.size()) == prefix;
}

// If the line starts with whitespace followed by str, returns the whitespace.
std::optional<std::string> GetSpaceBefore(std::string_view line, std::string_view str) {
  size_t i = 0;
  while (i < line.size() && line[i] == ' ') {
    i++;
  }
  if (StartsWith(line.substr(i), str)) {
    return std::string(line.substr(0, i));
  }
  return std::nullopt;
}

// Erases all "location" and "maybe_attributes" fields from a JSON IR string.
// These are the only things can change when manually decomposing a library.
// Also removes all end-of-line commas since these can cause spurious diffs.
// Note that this means the returned string is not valid JSON.
std::string ScrubJson(const std::string& json) {
  // We scan the JSON line by line, filtering out the undesired lines. To do
  // this, we rely on JsonWriter emitting correct indentation and newlines.
  std::istringstream input(json);
  std::ostringstream output;
  std::string line;
  std::optional<std::string> skip_until;
  while (std::getline(input, line)) {
    if (skip_until) {
      if (StartsWith(line, skip_until.value())) {
        skip_until = std::nullopt;
      }
    } else {
      if ((skip_until = GetSpaceBefore(line, "\"location\": {"))) {
        skip_until.value().push_back('}');
      } else if ((skip_until = GetSpaceBefore(line, "\"maybe_attributes\": ["))) {
        skip_until.value().push_back(']');
      } else {
        if (line.back() == ',') {
          line.pop_back();
        }
        output << line << '\n';
      }
    }
  }
  return output.str();
}

// Platform name and library name for all test libraries in this file.
const std::string kPlatformName = "example";
const std::vector<std::string_view> kLibraryName = {kPlatformName};

// Helper function to implement ASSERT_EQUIVALENT.
void AssertEquivalent(const std::string& left_fidl, const std::string& right_fidl,
                      std::string_view version) {
  TestLibrary left_lib(left_fidl);
  left_lib.SelectVersion(kPlatformName, version);
  ASSERT_COMPILED(left_lib);
  ASSERT_EQ(left_lib.compilation()->library_name, kLibraryName);
  TestLibrary right_lib(right_fidl);
  right_lib.SelectVersion(kPlatformName, version);
  ASSERT_COMPILED(right_lib);
  ASSERT_EQ(right_lib.compilation()->library_name, kLibraryName);
  auto left_json = ScrubJson(left_lib.GenerateJSON());
  auto right_json = ScrubJson(right_lib.GenerateJSON());
  if (left_json != right_json) {
    std::ofstream output_left("decomposition_tests_left.txt");
    output_left << left_json;
    output_left.close();
    std::ofstream output_right("decomposition_tests_right.txt");
    output_right << right_json;
    output_right.close();
  }
  ASSERT_STREQ(left_json, right_json,
               "To compare results, run:\n\n"
               "diff $(cat $FUCHSIA_DIR/.fx-build-dir)/decomposition_tests_{left,right}.txt\n");
}

// Asserts that left_fidl and right_fidl compile to JSON IR that is identical
// after scrubbbing (see ScrubJson) for the given version. On failure, the
// ASSERT_NO_FAILURES ensures that we report the caller's line number.
#define ASSERT_EQUIVALENT(left_fidl, right_fidl, version) \
  ASSERT_NO_FAILURES(AssertEquivalent(left_fidl, right_fidl, version))

TEST(DecompositionTests, EquivalentToSelf) {
  auto fidl = R"FIDL(
@available(added=1)
library example;
)FIDL";

  ASSERT_EQUIVALENT(fidl, fidl, "1");
  ASSERT_EQUIVALENT(fidl, fidl, "2");
  ASSERT_EQUIVALENT(fidl, fidl, "HEAD");
  ASSERT_EQUIVALENT(fidl, fidl, "LEGACY");
}

TEST(DecompositionTests, DefaultAddedAtHead) {
  auto with_attribute = R"FIDL(
@available(added=HEAD)
library example;

type Foo = struct {};
)FIDL";

  auto without_attribute = R"FIDL(
library example;

type Foo = struct {};
)FIDL";

  ASSERT_EQUIVALENT(with_attribute, without_attribute, "1");
  ASSERT_EQUIVALENT(with_attribute, without_attribute, "2");
  ASSERT_EQUIVALENT(with_attribute, without_attribute, "HEAD");
  ASSERT_EQUIVALENT(with_attribute, without_attribute, "LEGACY");
}

TEST(DecompositionTests, AbsentLibraryIsEmpty) {
  auto fidl = R"FIDL(
@available(added=2, removed=3)
library example;

type Foo = struct {};
)FIDL";

  auto v1 = R"FIDL(
@available(added=1, removed=2)
library example;
)FIDL";

  auto v2 = R"FIDL(
@available(added=2, removed=3)
library example;

type Foo = struct {};
)FIDL";

  auto v3_onward = R"FIDL(
@available(added=3)
library example;
)FIDL";

  ASSERT_EQUIVALENT(fidl, v1, "1");
  ASSERT_EQUIVALENT(fidl, v2, "2");
  ASSERT_EQUIVALENT(fidl, v3_onward, "3");
  ASSERT_EQUIVALENT(fidl, v3_onward, "HEAD");
  ASSERT_EQUIVALENT(fidl, v3_onward, "LEGACY");
}

TEST(DecompositionTests, SplitByMembership) {
  auto fidl = R"FIDL(
@available(added=1)
library example;

type TopLevel = struct {
    @available(added=2)
    first uint32;
};
)FIDL";

  auto v1 = R"FIDL(
@available(added=1, removed=2)
library example;

type TopLevel = struct {};
)FIDL";

  auto v2_onward = R"FIDL(
@available(added=2)
library example;

type TopLevel = struct {
    first uint32;
};
)FIDL";

  ASSERT_EQUIVALENT(fidl, v1, "1");
  ASSERT_EQUIVALENT(fidl, v2_onward, "2");
  ASSERT_EQUIVALENT(fidl, v2_onward, "HEAD");
  ASSERT_EQUIVALENT(fidl, v2_onward, "LEGACY");
}

TEST(DecompositionTests, SplitByReference) {
  auto fidl = R"FIDL(
@available(added=1)
library example;

type This = struct {
    this_member That;
};

type That = struct {
    @available(added=2)
    that_member uint32;
};
)FIDL";

  auto v1 = R"FIDL(
@available(added=1, removed=2)
library example;

type This = struct {
    this_member That;
};

type That = struct {};
)FIDL";

  auto v2_onward = R"FIDL(
@available(added=2)
library example;

type This = struct {
    this_member That;
};

type That = struct {
    that_member uint32;
};
)FIDL";

  ASSERT_EQUIVALENT(fidl, v1, "1");
  ASSERT_EQUIVALENT(fidl, v2_onward, "2");
  ASSERT_EQUIVALENT(fidl, v2_onward, "HEAD");
  ASSERT_EQUIVALENT(fidl, v2_onward, "LEGACY");
}

TEST(DecompositionTests, SplitByTwoMembers) {
  auto fidl = R"FIDL(
@available(added=1)
library example;

type This = struct {
    @available(added=2)
    first That;
    @available(added=3)
    second That;
};

type That = struct {};
)FIDL";

  auto v1 = R"FIDL(
@available(added=1, removed=2)
library example;

type This = struct {};

type That = struct {};
)FIDL";

  auto v2 = R"FIDL(
@available(added=2, removed=3)
library example;

type This = struct {
    first That;
};

type That = struct {};
)FIDL";

  auto v3_onward = R"FIDL(
@available(added=3)
library example;

type This = struct {
    first That;
    second That;
};

type That = struct {};
)FIDL";

  ASSERT_EQUIVALENT(fidl, v1, "1");
  ASSERT_EQUIVALENT(fidl, v2, "2");
  ASSERT_EQUIVALENT(fidl, v3_onward, "3");
  ASSERT_EQUIVALENT(fidl, v3_onward, "HEAD");
  ASSERT_EQUIVALENT(fidl, v3_onward, "LEGACY");
}

TEST(DecompositionTests, Recursion) {
  auto fidl = R"FIDL(
@available(added=1)
library example;

type Expr = flexible union {
    1: num int64;

    @available(removed=3)
    2: add struct {
        left Expr:optional;
        right Expr:optional;
    };

    @available(added=2, removed=3)
    3: mul struct {
        left Expr:optional;
        right Expr:optional;
    };

    @available(added=3)
    2: reserved;
    @available(added=3)
    3: reserved;
    @available(added=3)
    4: bin struct {
        kind flexible enum {
            ADD = 1;
            MUL = 2;
            DIV = 3;

            @available(added=4)
            MOD = 4;
        };
        left Expr:optional;
        right Expr:optional;
    };
};
)FIDL";

  auto v1 = R"FIDL(
@available(added=1, removed=2)
library example;

type Expr = flexible union {
    1: num int64;
    2: add struct {
        left Expr:optional;
        right Expr:optional;
    };
};
)FIDL";

  auto v2 = R"FIDL(
@available(added=2, removed=3)
library example;

type Expr = flexible union {
    1: num int64;
    2: add struct {
        left Expr:optional;
        right Expr:optional;
    };
    3: mul struct {
        left Expr:optional;
        right Expr:optional;
    };
};
)FIDL";

  auto v3 = R"FIDL(
@available(added=3, removed=4)
library example;

type Expr = flexible union {
    1: num int64;
    2: reserved;
    3: reserved;
    4: bin struct {
        kind flexible enum {
            ADD = 1;
            MUL = 2;
            DIV = 3;
        };
        left Expr:optional;
        right Expr:optional;
    };
};
)FIDL";

  auto v4_onward = R"FIDL(
@available(added=4)
library example;

type Expr = flexible union {
    1: num int64;
    2: reserved;
    3: reserved;
    4: bin struct {
        kind flexible enum {
            ADD = 1;
            MUL = 2;
            DIV = 3;
            MOD = 4;
        };
        left Expr:optional;
        right Expr:optional;
    };
};
)FIDL";

  ASSERT_EQUIVALENT(fidl, v1, "1");
  ASSERT_EQUIVALENT(fidl, v2, "2");
  ASSERT_EQUIVALENT(fidl, v3, "3");
  ASSERT_EQUIVALENT(fidl, v4_onward, "4");
  ASSERT_EQUIVALENT(fidl, v4_onward, "HEAD");
  ASSERT_EQUIVALENT(fidl, v4_onward, "LEGACY");
}

TEST(DecompositionTests, MutualRecursion) {
  auto fidl = R"FIDL(
@available(added=1)
library example;

@available(added=2)
type Foo = struct {
    str string;
    @available(added=3)
    bars vector<box<Bar>>;
};

@available(added=2)
type Bar = struct {
    @available(removed=5)
    foo box<Foo>;
    @available(added=4)
    str string;
};
)FIDL";

  auto v1 = R"FIDL(
@available(added=1, removed=2)
library example;
)FIDL";

  auto v2 = R"FIDL(
@available(added=2, removed=3)
library example;

type Foo = struct {
    str string;
};

type Bar = struct {
    foo box<Foo>;
};
)FIDL";

  auto v3 = R"FIDL(
@available(added=3, removed=4)
library example;

type Foo = struct {
    str string;
    bars vector<box<Bar>>;
};

type Bar = struct {
    foo box<Foo>;
};
)FIDL";

  auto v4 = R"FIDL(
@available(added=4, removed=5)
library example;

type Foo = struct {
    str string;
    bars vector<box<Bar>>;
};

type Bar = struct {
    foo box<Foo>;
    str string;
};
)FIDL";

  auto v5_onward = R"FIDL(
@available(added=5)
library example;

type Foo = struct {
    str string;
    bars vector<box<Bar>>;
};

type Bar = struct {
    str string;
};
)FIDL";

  ASSERT_EQUIVALENT(fidl, v1, "1");
  ASSERT_EQUIVALENT(fidl, v2, "2");
  ASSERT_EQUIVALENT(fidl, v3, "3");
  ASSERT_EQUIVALENT(fidl, v4, "4");
  ASSERT_EQUIVALENT(fidl, v5_onward, "5");
  ASSERT_EQUIVALENT(fidl, v5_onward, "HEAD");
  ASSERT_EQUIVALENT(fidl, v5_onward, "LEGACY");
}

TEST(DecompositionTests, MisalignedSwapping) {
  auto fidl = R"FIDL(
@available(added=1)
library example;

@available(removed=4)
const LEN uint64 = 16;
@available(added=4)
const LEN uint64 = 32;

@available(added=2)
type Foo = table {
    @available(removed=3)
    1: bar string;
    @available(added=3)
    1: bar string:LEN;
};
)FIDL";

  auto v1 = R"FIDL(
@available(added=1, removed=2)
library example;

const LEN uint64 = 16;
)FIDL";

  auto v2 = R"FIDL(
@available(added=2, removed=3)
library example;

const LEN uint64 = 16;
type Foo = table {
    1: bar string;
};
)FIDL";

  auto v3 = R"FIDL(
@available(added=3, removed=4)
library example;

const LEN uint64 = 16;
type Foo = table {
    1: bar string:LEN;
};
)FIDL";

  auto v4_onward = R"FIDL(
@available(added=4)
library example;

const LEN uint64 = 32;
type Foo = table {
    1: bar string:LEN;
};
)FIDL";

  ASSERT_EQUIVALENT(fidl, v1, "1");
  ASSERT_EQUIVALENT(fidl, v2, "2");
  ASSERT_EQUIVALENT(fidl, v3, "3");
  ASSERT_EQUIVALENT(fidl, v4_onward, "4");
  ASSERT_EQUIVALENT(fidl, v4_onward, "HEAD");
  ASSERT_EQUIVALENT(fidl, v4_onward, "LEGACY");
}

TEST(DecompositionTests, StrictToFlexible) {
  auto fidl = R"FIDL(
@available(added=1)
library example;

type X = struct {
    @available(added=2, removed=4)
    y Y;
};

@available(added=2, removed=3)
type Y = strict enum { A = 1; };

@available(added=3)
type Y = flexible enum { A = 1; };
)FIDL";

  auto v1 = R"FIDL(
@available(added=1, removed=2)
library example;

type X = struct {};
)FIDL";

  auto v2 = R"FIDL(
@available(added=2, removed=3)
library example;

type X = struct {
    y Y;
};

type Y = strict enum { A = 1; };
)FIDL";

  auto v3 = R"FIDL(
@available(added=3, removed=4)
library example;

type X = struct {
    y Y;
};

type Y = flexible enum { A = 1; };
)FIDL";

  auto v4_onward = R"FIDL(
@available(added=4)
library example;

type X = struct {};

type Y = flexible enum { A = 1; };
)FIDL";

  ASSERT_EQUIVALENT(fidl, v1, "1");
  ASSERT_EQUIVALENT(fidl, v2, "2");
  ASSERT_EQUIVALENT(fidl, v3, "3");
  ASSERT_EQUIVALENT(fidl, v4_onward, "4");
  ASSERT_EQUIVALENT(fidl, v4_onward, "HEAD");
  ASSERT_EQUIVALENT(fidl, v4_onward, "LEGACY");
}

TEST(DecompositionTests, NameReuse) {
  auto fidl = R"FIDL(
@available(added=1)
library example;

@available(added=2, removed=3)
type Foo = struct {
    bar Bar;
};
@available(added=1, removed=4)
type Bar = struct {};

@available(added=4, removed=7)
type Foo = struct {};
@available(added=4, removed=6)
type Bar = struct {
    foo Foo;
};
)FIDL";

  auto v1 = R"FIDL(
@available(added=1, removed=2)
library example;

type Bar = struct {};
)FIDL";

  auto v2 = R"FIDL(
@available(added=2, removed=3)
library example;

type Foo = struct {
    bar Bar;
};
type Bar = struct {};
)FIDL";

  auto v3 = R"FIDL(
@available(added=3, removed=4)
library example;

type Bar = struct {};
)FIDL";

  auto v4_to_5 = R"FIDL(
@available(added=4, removed=6)
library example;

type Foo = struct {};
type Bar = struct {
    foo Foo;
};
)FIDL";

  auto v6 = R"FIDL(
@available(added=6, removed=7)
library example;

type Foo = struct {};
)FIDL";

  auto v7_onward = R"FIDL(
@available(added=7)
library example;
)FIDL";

  ASSERT_EQUIVALENT(fidl, v1, "1");
  ASSERT_EQUIVALENT(fidl, v2, "2");
  ASSERT_EQUIVALENT(fidl, v3, "3");
  ASSERT_EQUIVALENT(fidl, v4_to_5, "4");
  ASSERT_EQUIVALENT(fidl, v4_to_5, "5");
  ASSERT_EQUIVALENT(fidl, v6, "6");
  ASSERT_EQUIVALENT(fidl, v7_onward, "7");
  ASSERT_EQUIVALENT(fidl, v7_onward, "HEAD");
  ASSERT_EQUIVALENT(fidl, v7_onward, "LEGACY");
}

TEST(DecompositionTests, ConstsAndConstraints) {
  auto fidl = R"FIDL(
@available(added=1)
library example;

@available(removed=4)
const LEN uint64 = 10;

type Foo = table {
    @available(removed=3)
    1: bar Bar;
    @available(added=3, removed=4)
    1: bar string:LEN;
    @available(added=4, removed=5)
    1: bar Bar;
};

@available(removed=2)
type Bar = struct {};
@available(added=2)
type Bar = table {};
)FIDL";

  auto v1 = R"FIDL(
@available(added=1, removed=2)
library example;

const LEN uint64 = 10;
type Foo = table {
    1: bar Bar;
};
type Bar = struct {};
)FIDL";

  auto v2 = R"FIDL(
@available(added=2, removed=3)
library example;

const LEN uint64 = 10;
type Foo = table {
    1: bar Bar;
};
type Bar = table {};
)FIDL";

  auto v3 = R"FIDL(
@available(added=3, removed=4)
library example;

const LEN uint64 = 10;
type Foo = table {
    1: bar string:LEN;
};
type Bar = table {};
)FIDL";

  auto v4 = R"FIDL(
@available(added=4, removed=5)
library example;

type Foo = table {
    1: bar Bar;
};
type Bar = table {};
)FIDL";

  auto v5_onward = R"FIDL(
@available(added=5)
library example;

type Foo = table {};
type Bar = table {};
)FIDL";

  ASSERT_EQUIVALENT(fidl, v1, "1");
  ASSERT_EQUIVALENT(fidl, v2, "2");
  ASSERT_EQUIVALENT(fidl, v3, "3");
  ASSERT_EQUIVALENT(fidl, v4, "4");
  ASSERT_EQUIVALENT(fidl, v5_onward, "5");
  ASSERT_EQUIVALENT(fidl, v5_onward, "HEAD");
  ASSERT_EQUIVALENT(fidl, v5_onward, "LEGACY");
}

TEST(DecompositionTests, AllElementsSplitByMembership) {
  auto fidl = R"FIDL(
@available(added=1)
library example;

@available(added=2, removed=5)
type Bits = bits {
    FIRST = 1;
    @available(added=3, removed=4)
    SECOND = 2;
};

@available(added=2, removed=5)
type Enum = enum {
    FIRST = 1;
    @available(added=3, removed=4)
    SECOND = 2;
};

@available(added=2, removed=5)
type Struct = struct {
    first string;
    @available(added=3, removed=4)
    second string;
};

@available(added=2, removed=5)
type Table = table {
    1: first string;
    @available(added=3, removed=4)
    2: second string;
};

@available(added=2, removed=5)
type Union = union {
    1: first string;
    @available(added=3, removed=4)
    2: second string;
};

@available(added=2, removed=5)
protocol TargetProtocol {};

@available(added=2, removed=5)
protocol ProtocolComposition {
    @available(added=3, removed=4)
    compose TargetProtocol;
};

@available(added=2, removed=5)
protocol ProtocolMethods {
    @available(added=3, removed=4)
    Method() -> ();
};

@available(added=2, removed=5)
service Service {
    first client_end:TargetProtocol;
    @available(added=3, removed=4)
    second client_end:TargetProtocol;
};

@available(added=2, removed=5)
resource_definition Resource : uint32 {
    properties {
        first uint32;
        @available(added=3, removed=4)
        second uint32;
        // This property is required for compilation, but is not otherwise under test.
        subtype flexible enum : uint32 {};
    };
};
)FIDL";

  auto v1 = R"FIDL(
@available(added=1, removed=2)
library example;
)FIDL";

  auto v2 = R"FIDL(
@available(added=2, removed=3)
library example;

type Bits = bits {
    FIRST = 1;
};

type Enum = enum {
    FIRST = 1;
};

type Struct = struct {
    first string;
};

type Table = table {
    1: first string;
};

type Union = union {
    1: first string;
};

protocol TargetProtocol {};

protocol ProtocolComposition {};

protocol ProtocolMethods {};

service Service {
    first client_end:TargetProtocol;
};

resource_definition Resource : uint32 {
    properties {
        first uint32;
        // This property is required for compilation, but is not otherwise under test.
        subtype flexible enum : uint32 {};
    };
};
)FIDL";

  auto v3 = R"FIDL(
@available(added=3, removed=4)
library example;

type Bits = bits {
    FIRST = 1;
    SECOND = 2;
};

type Enum = enum {
    FIRST = 1;
    SECOND = 2;
};

type Struct = struct {
    first string;
    second string;
};

type Table = table {
    1: first string;
    2: second string;
};

type Union = union {
    1: first string;
    2: second string;
};

protocol TargetProtocol {};

protocol ProtocolComposition {
    compose TargetProtocol;
};

protocol ProtocolMethods {
    Method() -> ();
};

service Service {
    first client_end:TargetProtocol;
    second client_end:TargetProtocol;
};

resource_definition Resource : uint32 {
    properties {
        first uint32;
        second uint32;
        // This property is required for compilation, but is not otherwise under test.
        subtype flexible enum : uint32 {};
    };
};
)FIDL";

  auto v4 = R"FIDL(
@available(added=4, removed=5)
library example;

type Bits = bits {
    FIRST = 1;
};

type Enum = enum {
    FIRST = 1;
};

type Struct = struct {
    first string;
};


type Table = table {
    1: first string;
};

type Union = union {
    1: first string;
};

protocol TargetProtocol {};

protocol ProtocolComposition {};

protocol ProtocolMethods {};

service Service {
    first client_end:TargetProtocol;
};

resource_definition Resource : uint32 {
    properties {
        first uint32;
        // This property is required for compilation, but is not otherwise under test.
        subtype flexible enum : uint32 {};
    };
};
)FIDL";

  auto v5_onward = R"FIDL(
@available(added=5)
library example;
)FIDL";

  ASSERT_EQUIVALENT(fidl, v1, "1");
  ASSERT_EQUIVALENT(fidl, v2, "2");
  ASSERT_EQUIVALENT(fidl, v3, "3");
  ASSERT_EQUIVALENT(fidl, v4, "4");
  ASSERT_EQUIVALENT(fidl, v5_onward, "5");
  ASSERT_EQUIVALENT(fidl, v5_onward, "HEAD");
  ASSERT_EQUIVALENT(fidl, v5_onward, "LEGACY");
}

TEST(DecompositionTests, AllElementsSplitByReference) {
  auto fidl_prefix = R"FIDL(
@available(added=1)
library example;

@available(removed=2)
const VALUE uint32 = 1;
@available(added=2)
const VALUE uint32 = 2;

@available(removed=2)
type Type = struct {
    value bool;
};
@available(added=2)
type Type = table {
    1: value bool;
};

// Need unsigned integers for bits underlying type.
@available(removed=2)
alias IntegerType = uint32;
@available(added=2)
alias IntegerType = uint64;

// Need uint32/int32 for error type.
@available(removed=2)
alias ErrorIntegerType = uint32;
@available(added=2)
alias ErrorIntegerType = int32;

@available(removed=2)
protocol TargetProtocol {};
@available(added=2)
protocol TargetProtocol {
    Method();
};
)FIDL";

  auto v1_prefix = R"FIDL(
@available(added=1, removed=2)
library example;

const VALUE uint32 = 1;

type Type = struct {
    value bool;
};

alias IntegerType = uint32;

alias ErrorIntegerType = uint32;

protocol TargetProtocol {};
)FIDL";

  auto v2_onward_prefix = R"FIDL(
@available(added=2)
library example;

const VALUE uint32 = 2;

type Type = table {
    1: value bool;
};

alias IntegerType = uint64;

alias ErrorIntegerType = int32;

protocol TargetProtocol { Method(); };
)FIDL";

  auto common_suffix = R"FIDL(
const CONST uint32 = VALUE;

alias Alias = Type;

// TODO(fxbug.dev/7807): Uncomment.
// type Newtype = Type;

type BitsUnderlying = bits : IntegerType {
    MEMBER = 1;
};

type BitsMemberValue = bits {
    MEMBER = VALUE;
};

type EnumUnderlying = enum : IntegerType {
    MEMBER = 1;
};

type EnumMemberValue = enum {
    MEMBER = VALUE;
};

type StructMemberType = struct {
    member Type;
};

type StructMemberDefault = struct {
    @allow_deprecated_struct_defaults
    member uint32 = VALUE;
};

type Table = table {
    1: member Type;
};

type Union = union {
    1: member Type;
};

protocol ProtocolComposition {
    compose TargetProtocol;
};

protocol ProtocolMethodRequest {
    Method(Type);
};

protocol ProtocolMethodResponse {
    Method() -> (Type);
};

protocol ProtocolEvent {
    -> Event(Type);
};

protocol ProtocolSuccess {
    Method() -> (Type) error uint32;
};

protocol ProtocolError {
    Method() -> (struct {}) error ErrorIntegerType;
};

service Service {
    member client_end:TargetProtocol;
};

resource_definition Resource : uint32 {
    properties {
        first IntegerType;
        // This property is required for compilation, but is not otherwise under test.
        subtype flexible enum : uint32 {};
    };
};

type NestedTypes = struct {
    first vector<Type>;
    second vector<array<Type, 3>>;
};

type LayoutParameters = struct {
    member array<bool, VALUE>;
};

type Constraints = struct {
    member vector<bool>:VALUE;
};

type AnonymousLayouts = struct {
    first_member table {
        1: second_member union {
            1: third_member Type;
        };
    };
};

protocol AnonymousLayoutsInProtocol {
    Request(struct { member Type; });
    Response() -> (struct { member Type; });
    -> Event(struct { member Type; });
    Success() -> (struct { member Type; }) error uint32;
    Error() -> (struct {}) error ErrorIntegerType;
};
)FIDL";

  auto fidl = std::string(fidl_prefix) + common_suffix;
  auto v1 = std::string(v1_prefix) + common_suffix;
  auto v2_onward = std::string(v2_onward_prefix) + common_suffix;

  ASSERT_EQUIVALENT(fidl, v1, "1");
  ASSERT_EQUIVALENT(fidl, v2_onward, "2");
  ASSERT_EQUIVALENT(fidl, v2_onward, "HEAD");
  ASSERT_EQUIVALENT(fidl, v2_onward, "LEGACY");
}

TEST(DecompositionTests, Complicated) {
  auto fidl = R"FIDL(
@available(added=1)
library example;

type X = resource struct {
    @available(removed=7)
    x1 bool;
    @available(added=3)
    x2 Y;
    @available(added=4)
    x3 Z;
};

@available(added=3)
type Y = resource union {
    1: y1 client_end:A;
    @available(added=4, removed=5)
    2: y2 client_end:B;
};

@available(added=3)
type Z = resource struct {
    z1 Y:optional;
    z2 vector<W>:optional;
};

@available(added=3)
type W = resource table {
    1: w1 X;
};

protocol A {
    A1(X);
    @available(added=7)
    A2(resource struct { y Y; });
};

@available(added=3)
protocol B {
    @available(removed=5)
    B1(X);
    @available(added=5)
    B2(resource struct {
      x X;
      y Y;
    });
};

@available(removed=6)
protocol AB {
    compose A;
    @available(added=4)
    compose B;
};
)FIDL";

  auto v1_to_2 = R"FIDL(
@available(added=1, removed=3)
library example;

type X = resource struct {
    x1 bool;
};

protocol A {
    A1(X);
};

protocol AB {
    compose A;
};
)FIDL";

  auto v3 = R"FIDL(
@available(added=3, removed=4)
library example;

type X = resource struct {
    x1 bool;
    x2 Y;
};

type Y = resource union {
    1: y1 client_end:A;
};

type Z = resource struct {
    z1 Y:optional;
    z2 vector<W>:optional;
};

type W = resource table {
    1: w1 X;
};

protocol A {
    A1(X);
};

protocol B {
    B1(X);
};

protocol AB {
    compose A;
};
)FIDL";

  auto v4 = R"FIDL(
@available(added=4, removed=5)
library example;

type X = resource struct {
    x1 bool;
    x2 Y;
    x3 Z;
};

type Y = resource union {
    1: y1 client_end:A;
    2: y2 client_end:B;
};

type Z = resource struct {
    z1 Y:optional;
    z2 vector<W>:optional;
};

type W = resource table {
    1: w1 X;
};

protocol A {
    A1(X);
};

protocol B {
    B1(X);
};

protocol AB {
    compose A;
    compose B;
};
)FIDL";

  auto v5 = R"FIDL(
@available(added=5, removed=6)
library example;

type X = resource struct {
    x1 bool;
    x2 Y;
    x3 Z;
};

type Y = resource union {
    1: y1 client_end:A;
};

type Z = resource struct {
    z1 Y:optional;
    z2 vector<W>:optional;
};

type W = resource table {
    1: w1 X;
};

protocol A {
    A1(X);
};

protocol B {
    B2(resource struct {
      x X;
      y Y;
    });
};

protocol AB {
    compose A;
    compose B;
};
)FIDL";

  auto v6 = R"FIDL(
@available(added=6, removed=7)
library example;

type X = resource struct {
    x1 bool;
    x2 Y;
    x3 Z;
};

type Y = resource union {
    1: y1 client_end:A;
};

type Z = resource struct {
    z1 Y:optional;
    z2 vector<W>:optional;
};

type W = resource table {
    1: w1 X;
};

protocol A {
    A1(X);
};

protocol B {
    B2(resource struct {
      x X;
      y Y;
    });
};
)FIDL";

  auto v7_onward = R"FIDL(
@available(added=7)
library example;

type X = resource struct {
    x2 Y;
    x3 Z;
};

type Y = resource union {
    1: y1 client_end:A;
};

type Z = resource struct {
    z1 Y:optional;
    z2 vector<W>:optional;
};

type W = resource table {
    1: w1 X;
};

protocol A {
    A1(X);
    A2(resource struct { y Y; });
};

protocol B {
    B2(resource struct {
      x X;
      y Y;
    });
};
)FIDL";

  ASSERT_EQUIVALENT(fidl, v1_to_2, "1");
  ASSERT_EQUIVALENT(fidl, v1_to_2, "2");
  ASSERT_EQUIVALENT(fidl, v3, "3");
  ASSERT_EQUIVALENT(fidl, v4, "4");
  ASSERT_EQUIVALENT(fidl, v5, "5");
  ASSERT_EQUIVALENT(fidl, v6, "6");
  ASSERT_EQUIVALENT(fidl, v7_onward, "7");
  ASSERT_EQUIVALENT(fidl, v7_onward, "HEAD");
  ASSERT_EQUIVALENT(fidl, v7_onward, "LEGACY");
}

TEST(DecompositionTests, Legacy) {
  auto fidl = R"FIDL(
@available(added=1)
library example;

protocol NeverRemoved {
    @available(removed=3)
    RemovedAt3();

    @available(removed=3, legacy=false)
    RemovedAt3LegacyFalse();

    @available(removed=3, legacy=true)
    RemovedAt3LegacyTrue();

    @available(removed=2)
    SwappedAt2();

    @available(added=2)
    SwappedAt2(struct { b bool; });
};

@available(removed=3)
protocol RemovedAt3 {
    Default();

    @available(legacy=false)
    LegacyFalse();

    @available(removed=2)
    RemovedAt2();

    @available(removed=2)
    SwappedAt2();

    @available(added=2)
    SwappedAt2(struct { b bool; });
};

@available(removed=3, legacy=false)
protocol RemovedAt3LegacyFalse {
    Default();

    @available(legacy=false)
    LegacyFalse();

    @available(removed=2)
    RemovedAt2();

    @available(removed=2)
    SwappedAt2();

    @available(added=2)
    SwappedAt2(struct { b bool; });
};

@available(removed=3, legacy=true)
protocol RemovedAt3LegacyTrue {
    Default();

    @available(legacy=false)
    LegacyFalse();

    @available(legacy=true)
    LegacyTrue();

    @available(removed=2)
    RemovedAt2();

    @available(removed=2)
    SwappedAt2();

    @available(added=2)
    SwappedAt2(struct { b bool; });
};
)FIDL";

  auto v1 = R"FIDL(
@available(added=1, removed=2)
library example;

protocol NeverRemoved {
    RemovedAt3();
    RemovedAt3LegacyFalse();
    RemovedAt3LegacyTrue();
    SwappedAt2();
};

protocol RemovedAt3 {
    Default();
    LegacyFalse();
    RemovedAt2();
    SwappedAt2();
};

protocol RemovedAt3LegacyFalse {
    Default();
    LegacyFalse();
    RemovedAt2();
    SwappedAt2();
};

protocol RemovedAt3LegacyTrue {
    Default();
    LegacyFalse();
    LegacyTrue();
    RemovedAt2();
    SwappedAt2();
};
)FIDL";

  auto v2 = R"FIDL(
@available(added=2, removed=3)
library example;

protocol NeverRemoved {
    RemovedAt3();
    RemovedAt3LegacyFalse();
    RemovedAt3LegacyTrue();
    SwappedAt2(struct { b bool; });
};

protocol RemovedAt3 {
    Default();
    LegacyFalse();
    SwappedAt2(struct { b bool; });
};

protocol RemovedAt3LegacyFalse {
    Default();
    LegacyFalse();
    SwappedAt2(struct { b bool; });
};

protocol RemovedAt3LegacyTrue {
    Default();
    LegacyFalse();
    LegacyTrue();
    SwappedAt2(struct { b bool; });
};
)FIDL";

  auto v3_to_head = R"FIDL(
@available(added=3)
library example;

protocol NeverRemoved {
    SwappedAt2(struct { b bool; });
};
)FIDL";

  auto legacy = R"FIDL(
// This is the closest we can get to making the library only available at LEGACY.
@available(added=1, removed=2, legacy=true)
library example;

protocol NeverRemoved {
    RemovedAt3LegacyTrue();
    SwappedAt2(struct { b bool; });
};

protocol RemovedAt3LegacyTrue {
    Default();
    LegacyTrue();
    SwappedAt2(struct { b bool; });
};
)FIDL";

  ASSERT_EQUIVALENT(fidl, v1, "1");
  ASSERT_EQUIVALENT(fidl, v2, "2");
  ASSERT_EQUIVALENT(fidl, v3_to_head, "3");
  ASSERT_EQUIVALENT(fidl, v3_to_head, "HEAD");
  ASSERT_EQUIVALENT(fidl, legacy, "LEGACY");
}

}  // namespace
