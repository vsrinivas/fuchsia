// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <locale.h>

#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/raw_ast.h>
#include <fidl/source_file.h>
#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

// Test that an invalid compound identifier fails parsing. Regression
// test for fxbug.dev/7600.
TEST(ParsingTests, BadCompoundIdentifierTest) {
  // The leading 0 in the library name causes parsing an Identifier
  // to fail, and then parsing a CompoundIdentifier to fail.
  TestLibrary library(R"FIDL(
library 0fidl.test.badcompoundidentifier;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind);
}

// Test that library name formatting checks are done in the parser
TEST(ParsingTests, BadLibraryNameTest) {
  TestLibrary library(R"FIDL(
library a_b;
)FIDL");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidLibraryNameComponent);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "a_b");
}

// Test that otherwise reserved words can be appropriarely parsed when context
// is clear.
TEST(ParsingTests, GoodParsingReservedWordsInStructTest) {
  TestLibrary library(R"FIDL(
library example;

struct struct {
    bool field;
};

struct InStruct {
    struct foo;

    bool as;
    bool library;
    bool using;

    bool array;
    bool handle;
    bool request;
    bool string;
    bool vector;

    bool bool;
    bool int8;
    bool int16;
    bool int32;
    bool int64;
    bool uint8;
    bool uint16;
    bool uint32;
    bool uint64;
    bool float32;
    bool float64;

    bool true;
    bool false;

    bool reserved;
};
)FIDL");
  EXPECT_TRUE(library.Compile());
}

TEST(ParsingTests, GoodParsingHandlesInStructTest) {
  TestLibrary library(R"FIDL(
library example;

enum obj_type : uint32 {
    NONE = 0;
    PROCESS = 1;
    THREAD = 2;
    VMO = 3;
    CHANNEL = 4;
    EVENT = 5;
    PORT = 6;
    INTERRUPT = 9;
    PCI_DEVICE = 11;
    LOG = 12;
    SOCKET = 14;
    RESOURCE = 15;
    EVENTPAIR = 16;
    JOB = 17;
    VMAR = 18;
    FIFO = 19;
    GUEST = 20;
    VCPU = 21;
    TIMER = 22;
    IOMMU = 23;
    BTI = 24;
    PROFILE = 25;
    PMT = 26;
    SUSPEND_TOKEN = 27;
    PAGER = 28;
    EXCEPTION = 29;
    CLOCK = 30;
};

resource_definition handle : uint32 {
    properties {
        obj_type subtype;
    };
};

resource struct Handles {
    handle plain_handle;

    handle:BTI bti_handle;
    handle:CHANNEL channel_handle;
    handle:CLOCK clock_handle;
    handle:LOG debuglog_handle;
    handle:EVENT event_handle;
    handle:EVENTPAIR eventpair_handle;
    handle:EXCEPTION exception_handle;
    handle:FIFO fifo_handle;
    handle:GUEST guest_handle;
    handle:INTERRUPT interrupt_handle;
    handle:IOMMU iommu_handle;
    handle:JOB job_handle;
    handle:PAGER pager_handle;
    handle:PCI_DEVICE pcidevice_handle;
    handle:PMT pmt_handle;
    handle:PORT port_handle;
    handle:PROCESS process_handle;
    handle:PROFILE profile_handle;
    handle:RESOURCE resource_handle;
    handle:SOCKET socket_handle;
    handle:SUSPEND_TOKEN suspendtoken_handle;
    handle:THREAD thread_handle;
    handle:TIMER timer_handle;
    handle:VCPU vcpu_handle;
    handle:VMAR vmar_handle;
    handle:VMO vmo_handle;
};
)FIDL");

  EXPECT_TRUE(library.Compile());
}

TEST(ParsingTests, GoodParsingHandleConstraintTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

enum obj_type : uint32 {
    NONE = 0;
    VMO = 3;
};

bits rights : uint32 {
  TRANSFER = 1;
};

resource_definition handle : uint32 {
    properties {
        obj_type subtype;
        rights rights;
    };
};

resource struct Handles {
    handle plain_handle;
    handle:VMO subtype_handle;
    handle:<VMO, rights.TRANSFER> rights_handle;
};
)FIDL",
                      std::move(experimental_flags));

  EXPECT_TRUE(library.Compile());
}

// Test that otherwise reserved words can be appropriarely parsed when context
// is clear.
TEST(ParsingTests, GoodParsingReservedWordsInUnionTest) {
  TestLibrary library(R"FIDL(
library example;

struct struct {
    bool field;
};

union InUnion {
    1:  struct foo;

    2:  bool as;
    3:  bool library;
    4:  bool using;

    5:  bool array;
    6:  bool handle;
    7:  bool request;
    8:  bool string;
    9:  bool vector;

    10: bool bool;
    11: bool int8;
    12: bool int16;
    13: bool int32;
    14: bool int64;
    15: bool uint8;
    16: bool uint16;
    17: bool uint32;
    18: bool uint64;
    19: bool float32;
    20: bool float64;

    21: bool true;
    22: bool false;

    23: bool reserved;
};
)FIDL");
  EXPECT_TRUE(library.Compile());
}

// Test that otherwise reserved words can be appropriarely parsed when context
// is clear.
TEST(ParsingTests, GoodParsingReservedWordsInProtocolTest) {
  TestLibrary library(R"FIDL(
library example;

struct struct {
    bool field;
};

protocol InProtocol {
    as(bool as);
    library(bool library);
    using(bool using);

    array(bool array);
    handle(bool handle);
    request(bool request);
    string(bool string);
    vector(bool vector);

    bool(bool bool);
    int8(bool int8);
    int16(bool int16);
    int32(bool int32);
    int64(bool int64);
    uint8(bool uint8);
    uint16(bool uint16);
    uint32(bool uint32);
    uint64(bool uint64);
    float32(bool float32);
    float64(bool float64);

    true(bool true);
    false(bool false);

    reserved(bool reserved);

    foo(struct arg, int32 arg2, struct arg3);
};
)FIDL");
  EXPECT_TRUE(library.Compile());
}

TEST(ParsingTests, BadCharAtSignTest) {
  TestLibrary library(R"FIDL(
library test;

struct Test {
    uint8 @uint8;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidCharacter);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "@");
}

TEST(ParsingTests, BadCharSlashTest) {
  TestLibrary library(R"FIDL(
library test;

struct Test / {
    uint8 uint8;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidCharacter);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "/");
}

TEST(ParsingTests, BadIdentifierTest) {
  TestLibrary library(R"FIDL(
library test;

struct test_ {
    uint8 uint8;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidIdentifier);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "test_");
}

class LocaleSwapper {
 public:
  explicit LocaleSwapper(const char* new_locale) { old_locale_ = setlocale(LC_ALL, new_locale); }
  ~LocaleSwapper() { setlocale(LC_ALL, old_locale_); }

 private:
  const char* old_locale_;
};

TEST(ParsingTests, BadInvalidCharacterTest) {
  LocaleSwapper swapper("de_DE.iso88591");
  TestLibrary library("invalid.character.fidl", R"FIDL(
library fidl.test.maxbytes;

// This is all alphanumeric in the appropriate locale, but not a valid
// identifier.
struct ß {
    int32 x;
};

)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrInvalidCharacter,
                                      fidl::ErrInvalidCharacter);
}

TEST(ParsingTests, GoodEmptyStructTest) {
  TestLibrary library("empty_struct.fidl", R"FIDL(
library fidl.test.emptystruct;

struct Empty {
};

)FIDL");
  EXPECT_TRUE(library.Compile());
}

TEST(ParsingTests, BadErrorOnTypeAliasBeforeImports) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(
library dependent;

struct Something {};
)FIDL",
                         &shared);
  ASSERT_TRUE(dependency.Compile());

  TestLibrary library("example.fidl", R"FIDL(
library example;

alias foo = int16;
using dependent;

struct UseDependent {
    dependent.Something field;
};
)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrLibraryImportsMustBeGroupedAtTopOfFile);
}

TEST(ParsingTests, GoodAttributeValueHasCorrectContents) {
  TestLibrary library("example.fidl", R"FIDL(
  library example;

  [Foo="Bar"]
  struct Empty{};
  )FIDL");

  std::unique_ptr<fidl::raw::File> ast;
  ASSERT_TRUE(library.Parse(&ast));

  fidl::raw::Attribute attribute =
      std::move(ast->struct_declaration_list.front()->attributes->attributes.front());
  ASSERT_STR_EQ(attribute.name.c_str(), "Foo");
  ASSERT_STR_EQ(static_cast<fidl::raw::StringLiteral*>(attribute.value.get())->MakeContents(),
                "Bar");
}

TEST(ParsingTests, GoodMultilineCommentHasCorrectContents) {
  TestLibrary library("example.fidl", R"FIDL(
  library example;

  /// A
  /// multiline
  /// comment!
  struct Empty{};
  )FIDL");

  std::unique_ptr<fidl::raw::File> ast;
  ASSERT_TRUE(library.Parse(&ast));

  fidl::raw::Attribute attribute =
      std::move(ast->struct_declaration_list.front()->attributes->attributes.front());
  ASSERT_STR_EQ(attribute.name.c_str(), "Doc");
  ASSERT_STR_EQ(static_cast<fidl::raw::DocCommentLiteral*>(attribute.value.get())->MakeContents(),
                " A\n multiline\n comment!\n");
}

TEST(ParsingTests, WarnDocCommentBlankLineTest) {
  TestLibrary library("example.fidl", R"FIDL(
library example;

/// start

/// end
struct Empty{};
)FIDL");

  ASSERT_COMPILED(library);
  const auto& warnings = library.warnings();
  ASSERT_GE(warnings.size(), 1);
  ASSERT_ERR(warnings[0], fidl::WarnBlankLinesWithinDocCommentBlock);
}

TEST(ParsingTests, WarnCommentInsideDocCommentTest) {
  TestLibrary library("example.fidl", R"FIDL(
library example;

/// start
// middle
/// end
struct Empty{};
)FIDL");

  ASSERT_COMPILED(library);
  const auto& warnings = library.warnings();
  ASSERT_GE(warnings.size(), 1);
  ASSERT_ERR(warnings[0], fidl::WarnCommentWithinDocCommentBlock);
}

TEST(ParsingTests, WarnDocCommentWithCommentBlankLineTest) {
  TestLibrary library("example.fidl", R"FIDL(
library example;

/// start
// middle

/// end
struct Empty{};
)FIDL");

  ASSERT_COMPILED(library);
  const auto& warnings = library.warnings();
  ASSERT_EQ(warnings.size(), 2);
  ASSERT_ERR(warnings[0], fidl::WarnCommentWithinDocCommentBlock);
  ASSERT_ERR(warnings[1], fidl::WarnBlankLinesWithinDocCommentBlock);
}

TEST(ParsingTests, BadDocCommentNotAllowedOnParams) {
  TestLibrary library("example.fidl", R"FIDL(
library example;

protocol Example {
  Method(/// Doc comment
         Bool b);
};
)FIDL");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDocCommentOnParameters);
}

TEST(ParsingTests, GoodCommentsSurroundingDocCommentTest) {
  TestLibrary library("example.fidl", R"FIDL(
library example;

// some comments above,
// maybe about the doc comment
/// A
/// multiline
/// comment!
// another comment about the struct
struct Empty{};
)FIDL");

  std::unique_ptr<fidl::raw::File> ast;
  library.set_warnings_as_errors(true);
  ASSERT_TRUE(library.Parse(&ast));
}

TEST(ParsingTests, GoodBlankLinesAfterDocCommentTest) {
  TestLibrary library("example.fidl", R"FIDL(
library example;

/// doc comment



struct Empty{};
)FIDL");

  std::unique_ptr<fidl::raw::File> ast;
  library.set_warnings_as_errors(true);
  ASSERT_TRUE(library.Parse(&ast));
}

TEST(ParsingTests, GoodBlankLinesAfterDocCommentWithCommentTest) {
  TestLibrary library("example.fidl", R"FIDL(
library example;

/// doc comment


// regular comment

struct Empty{};
)FIDL");

  std::unique_ptr<fidl::raw::File> ast;
  library.set_warnings_as_errors(true);
  ASSERT_TRUE(library.Parse(&ast));
}

TEST(ParsingTests, WarnTrailingDocCommentTest) {
  TestLibrary library("example.fidl", R"FIDL(
library example;

struct Empty{};
/// bad
)FIDL");

  ASSERT_COMPILED(library);
  const auto& warnings = library.warnings();
  ASSERT_EQ(warnings.size(), 1);
  ASSERT_ERR(warnings[0], fidl::WarnDocCommentMustBeFollowedByDeclaration);
}

TEST(ParsingTests, BadFinalMemberMissingSemicolon) {
  TestLibrary library(R"FIDL(
library example;

struct Struct {
    uint8 uint_value;
    string foo // error: missing semicolon
};
)FIDL");

  std::unique_ptr<fidl::raw::File> ast;
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind);
}

TEST(ParsingTests, BadFinalMemberMissingNameAndSemicolon) {
  TestLibrary library(R"FIDL(
library example;

struct Struct {
    uint8 uint_value;
    string
}; // error: want field name, got "}"
   // error: want "}", got EOF
)FIDL");

  std::unique_ptr<fidl::raw::File> ast;
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind,
                                      fidl::ErrUnexpectedTokenOfKind);
}

}  // namespace
