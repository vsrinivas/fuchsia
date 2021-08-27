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

// Test that otherwise reserved words can be appropriately parsed when context
// is clear.
TEST(ParsingTests, GoodParsingReservedWordsInStructTest) {
  TestLibrary library(R"FIDL(library example;

type struct = struct {
    field bool;
};

type flexible = struct {};
type strict = struct {};
type resource = struct {};

type InStruct = struct {
    foo struct;
    bar flexible;
    baz strict;
    qux resource;

    as bool;
    library bool;
    using bool;

    array bool;
    handle bool;
    request bool;
    string bool;
    vector bool;

    bool bool;
    int8 bool;
    int16 bool;
    int32 bool;
    int64 bool;
    uint8 bool;
    uint16 bool;
    uint32 bool;
    uint64 bool;
    float32 bool;
    float64 bool;

    true bool;
    false bool;

    reserved bool;
};
)FIDL");
  ASSERT_COMPILED(library);
}

// Test that otherwise reserved words can be appropriately parsed when context
// is clear.
TEST(ParsingTests, GoodParsingReservedWordsInConstraint) {
  TestLibrary library(R"FIDL(library example;

type Unshadowed = struct {};

// Keywords
const as uint16 = 1;
alias as_constraint = vector<Unshadowed>:as;
const library uint16 = 1;
alias library_constraint = vector<Unshadowed>:library;
const using uint16 = 1;
alias using_constraint = vector<Unshadowed>:using;
const alias uint16 = 1;
alias alias_constraint = vector<Unshadowed>:alias;
const type uint16 = 1;
alias type_constraint = vector<Unshadowed>:type;
const const uint16 = 1;
alias const_constraint = vector<Unshadowed>:const;
const protocol uint16 = 1;
alias protocol_constraint = vector<Unshadowed>:protocol;
const service uint16 = 1;
alias service_constraint = vector<Unshadowed>:service;
const compose uint16 = 1;
alias compose_constraint = vector<Unshadowed>:compose;
const reserved uint16 = 1;
alias reserved_constraint = vector<Unshadowed>:reserved;

// Layouts
const bits uint16 = 1;
alias bits_constraint = vector<Unshadowed>:bits;
const enum uint16 = 1;
alias enum_constraint = vector<Unshadowed>:enum;
const struct uint16 = 1;
alias struct_constraint = vector<Unshadowed>:struct;
const table uint16 = 1;
alias table_constraint = vector<Unshadowed>:table;
const union uint16 = 1;
alias union_constraint = vector<Unshadowed>:union;

// Builtins
const array uint16 = 1;
alias array_constraint = vector<Unshadowed>:array;
const handle uint16 = 1;
alias handle_constraint = vector<Unshadowed>:handle;
const request uint16 = 1;
alias request_constraint = vector<Unshadowed>:request;
const string uint16 = 1;
alias string_constraint = vector<Unshadowed>:string;
const optional uint16 = 1;
alias optional_constraint = vector<Unshadowed>:optional;

// Primitives
const bool uint16 = 1;
alias bool_constraint = vector<Unshadowed>:bool;
const int8 uint16 = 1;
alias int8_constraint = vector<Unshadowed>:int8;
const int16 uint16 = 1;
alias int16_constraint = vector<Unshadowed>:int16;
const int32 uint16 = 1;
alias int32_constraint = vector<Unshadowed>:int32;
const int64 uint16 = 1;
alias int64_constraint = vector<Unshadowed>:int64;
const uint8 uint16 = 1;
alias uint8_constraint = vector<Unshadowed>:uint8;
const uint32 uint16 = 1;
alias uint32_constraint = vector<Unshadowed>:uint32;
const uint64 uint16 = 1;
alias uint64_constraint = vector<Unshadowed>:uint64;
const float32 uint16 = 1;
alias float32_constraint = vector<Unshadowed>:float32;
const float64 uint16 = 1;
alias float64_constraint = vector<Unshadowed>:float64;

// Must go last so we don't overwrite uint16 for other tests.
const uint16 uint16 = 1;
alias uint16_constraint = vector<Unshadowed>:uint16;
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ParsingTests, GoodParsingHandlesInStructTest) {
  TestLibrary library(R"FIDL(library example;

type obj_type = strict enum : uint32 {
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
        subtype obj_type;
    };
};

type Handles = resource struct {
    plain_handle handle;

    bti_handle handle:BTI;
    channel_handle handle:CHANNEL;
    clock_handle handle:CLOCK;
    debuglog_handle handle:LOG;
    event_handle handle:EVENT;
    eventpair_handle handle:EVENTPAIR;
    exception_handle handle:EXCEPTION;
    fifo_handle handle:FIFO;
    guest_handle handle:GUEST;
    interrupt_handle handle:INTERRUPT;
    iommu_handle handle:IOMMU;
    job_handle handle:JOB;
    pager_handle handle:PAGER;
    pcidevice_handle handle:PCI_DEVICE;
    pmt_handle handle:PMT;
    port_handle handle:PORT;
    process_handle handle:PROCESS;
    profile_handle handle:PROFILE;
    resource_handle handle:RESOURCE;
    socket_handle handle:SOCKET;
    suspendtoken_handle handle:SUSPEND_TOKEN;
    thread_handle handle:THREAD;
    timer_handle handle:TIMER;
    vcpu_handle handle:VCPU;
    vmar_handle handle:VMAR;
    vmo_handle handle:VMO;
};
)FIDL");

  ASSERT_COMPILED(library);
}

TEST(ParsingTests, GoodParsingHandleConstraintTest) {
  TestLibrary library(R"FIDL(library example;

type obj_type = strict enum : uint32 {
    NONE = 0;
    VMO = 3;
};

type rights = strict bits : uint32 {
    TRANSFER = 1;
};

resource_definition handle : uint32 {
    properties {
        subtype obj_type;
        rights rights;
    };
};

type Handles = resource struct {
    plain_handle handle;
    subtype_handle handle:VMO;
    rights_handle handle:<VMO, rights.TRANSFER>;
};
)FIDL");

  ASSERT_COMPILED(library);
}

// Test that otherwise reserved words can be appropriarely parsed when context
// is clear.
TEST(ParsingTests, GoodParsingReservedWordsInUnionTest) {
  TestLibrary library(R"FIDL(library example;

type struct = struct {
    field bool;
};

type InUnion = strict union {
    1: foo struct;

    2: as bool;
    3: library bool;
    4: using bool;

    5: array bool;
    6: handle bool;
    7: request bool;
    8: string bool;
    9: vector bool;

   10: bool bool;
   11: int8 bool;
   12: int16 bool;
   13: int32 bool;
   14: int64 bool;
   15: uint8 bool;
   16: uint16 bool;
   17: uint32 bool;
   18: uint64 bool;
   19: float32 bool;
   20: float64 bool;

   21: true bool;
   22: false bool;

   23: reserved bool;
};
)FIDL");
  ASSERT_COMPILED(library);
}

// Test that otherwise reserved words can be appropriately parsed when context
// is clear.
TEST(ParsingTests, GoodParsingReservedWordsInProtocolTest) {
  TestLibrary library(R"FIDL(library example;

type struct = struct {
    field bool;
};

protocol InProtocol {
    as(struct {
        as bool;
    });
    library(struct {
        library bool;
    });
    using(struct {
        using bool;
    });

    array(struct {
        array bool;
    });
    handle(struct {
        handle bool;
    });
    request(struct {
        request bool;
    });
    string(struct {
        string bool;
    });
    vector(struct {
        vector bool;
    });

    bool(struct {
        bool bool;
    });
    int8(struct {
        int8 bool;
    });
    int16(struct {
        int16 bool;
    });
    int32(struct {
        int32 bool;
    });
    int64(struct {
        int64 bool;
    });
    uint8(struct {
        uint8 bool;
    });
    uint16(struct {
        uint16 bool;
    });
    uint32(struct {
        uint32 bool;
    });
    uint64(struct {
        uint64 bool;
    });
    float32(struct {
        float32 bool;
    });
    float64(struct {
        float64 bool;
    });

    true(struct {
        true bool;
    });
    false(struct {
        false bool;
    });

    reserved(struct {
        reserved bool;
    });

    foo(struct {
        arg struct;
        arg2 int32;
        arg3 struct;
    });
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ParsingTests, BadCharPoundSignTest) {
  TestLibrary library(R"FIDL(
library test;

type Test = struct {
    #uint8 uint8;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidCharacter);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "#");
}

TEST(ParsingTests, BadCharSlashTest) {
  TestLibrary library(R"FIDL(
library test;

type Test = struct / {
    uint8 uint8;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidCharacter);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "/");
}

TEST(ParsingTests, BadIdentifierTest) {
  TestLibrary library(R"FIDL(
library test;

type test_ = struct {
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
type ß = struct {
    x int32;
};

)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrInvalidCharacter,
                                      fidl::ErrInvalidCharacter);
}

TEST(ParsingTests, GoodEmptyStructTest) {
  TestLibrary library("empty_struct.fidl", R"FIDL(library fidl.test.emptystruct;

type Empty = struct {};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ParsingTests, BadErrorOnTypeAliasBeforeImports) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(library dependent;

type Something = struct {};
)FIDL",
                         &shared);
  ASSERT_COMPILED(dependency);

  TestLibrary library("example.fidl", R"FIDL(
library example;

alias foo = int16;
using dependent;

type UseDependent = struct {
    field dependent.Something;
};
)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrLibraryImportsMustBeGroupedAtTopOfFile);
}

TEST(ParsingTests, GoodAttributeValueHasCorrectContents) {
  TestLibrary library("example.fidl", R"FIDL(
  library example;

  @foo("Bar")
  type Empty = struct{};
)FIDL");

  std::unique_ptr<fidl::raw::File> ast;
  ASSERT_TRUE(library.Parse(&ast));

  fidl::raw::AttributeNew attribute =
      std::move(ast->type_decls.front()->attributes->attributes.front());
  ASSERT_STR_EQ(attribute.name.c_str(), "foo");
  ASSERT_TRUE(attribute.args.size() == 1);

  fidl::raw::AttributeArg arg = std::move(attribute.args[0]);
  ASSERT_STR_EQ(static_cast<fidl::raw::StringLiteral*>(arg.value.get())->MakeContents(), "Bar");
}

TEST(ParsingTests, GoodMultilineCommentHasCorrectContents) {
  TestLibrary library("example.fidl", R"FIDL(
  library example;

  /// A
  /// multiline
  /// comment!
  type Empty = struct {};
)FIDL");

  std::unique_ptr<fidl::raw::File> ast;
  ASSERT_TRUE(library.Parse(&ast));

  fidl::raw::AttributeNew attribute =
      std::move(ast->type_decls.front()->attributes->attributes.front());
  ASSERT_STR_EQ(attribute.name.c_str(), "doc");
  ASSERT_TRUE(attribute.args.size() == 1);

  fidl::raw::AttributeArg arg = std::move(attribute.args[0]);
  ASSERT_STR_EQ(static_cast<fidl::raw::DocCommentLiteral*>(arg.value.get())->MakeContents(),
                " A\n multiline\n comment!\n");
}

TEST(ParsingTests, WarnDocCommentBlankLineTest) {
  TestLibrary library("example.fidl", R"FIDL(
library example;

/// start

/// end
type Empty = struct {};
)FIDL");

  ASSERT_COMPILED(library);
  const auto& warnings = library.warnings();
  ASSERT_EQ(warnings.size(), 1);
  ASSERT_ERR(warnings[0], fidl::WarnBlankLinesWithinDocCommentBlock);
}

TEST(NewSyntaxTests, WarnCommentInsideDocCommentTest) {
  TestLibrary library("example.fidl", R"FIDL(
library example;

/// start
// middle
/// end
type Empty = struct {};
)FIDL");

  ASSERT_TRUE(library.Compile());
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
type Empty = struct {};
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
         struct { b bool; });
};
)FIDL");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDocCommentOnParameters);
}

TEST(ParsingTests, GoodCommentsSurroundingDocCommentTest) {
  TestLibrary library("example.fidl", R"FIDL(library example;

// some comments above,
// maybe about the doc comment
/// A
/// multiline
/// comment!
// another comment about the struct
type Empty = struct{};
)FIDL");

  library.set_warnings_as_errors(true);
  ASSERT_COMPILED(library);
}

TEST(ParsingTests, GoodBlankLinesAfterDocCommentTest) {
  TestLibrary library("example.fidl", R"FIDL(library example;

/// doc comment
type Empty = struct {};
)FIDL");

  library.set_warnings_as_errors(true);
  ASSERT_COMPILED(library);
}

TEST(ParsingTests, GoodBlankLinesAfterDocCommentWithCommentTest) {
  TestLibrary library("example.fidl", R"FIDL(library example;

/// doc comment


// regular comment

type Empty = struct {};
)FIDL");

  library.set_warnings_as_errors(true);
  ASSERT_COMPILED(library);
}

TEST(ParsingTests, WarnTrailingDocCommentTest) {
  TestLibrary library("example.fidl", R"FIDL(
library example;

type Empty = struct {};
/// bad
)FIDL");

  ASSERT_COMPILED(library);
  const auto& warnings = library.warnings();
  ASSERT_EQ(warnings.size(), 1);
  ASSERT_ERR(warnings[0], fidl::WarnDocCommentMustBeFollowedByDeclaration);
}

TEST(ParsingTests, BadTrailingDocCommentInDeclTest) {
  TestLibrary library("example.fidl", R"FIDL(
library example;

type Empty = struct {
   a = int8;
   /// bad
};
)FIDL");

  ASSERT_FALSE(library.Compile());

  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 3);
  ASSERT_ERR(errors[0], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[1], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[2], fidl::ErrUnexpectedTokenOfKind);
}

TEST(ParsingTests, BadFinalMemberMissingSemicolon) {
  TestLibrary library(R"FIDL(
library example;

type Struct = struct {
    uint_value uint8;
    foo string // error: missing semicolon
};
)FIDL");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind);
}

// NOTE(fxbug.dev/72924): this test is slightly different from the old syntax
// one that it replaces, in that the "missing" portion of the struct member is a
// type, not a name.
TEST(ParsingTests, BadFinalMemberMissingTypeAndSemicolon) {
  TestLibrary library(R"FIDL(
library example;

type Struct = struct {
    uint_value uint8;
    string_value
}; // error: want type, got "}"
   // error: want "}", got EOF
)FIDL");

  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind,
                                      fidl::ErrUnexpectedTokenOfKind);
}

TEST(ParsingTests, BadMissingConstraintBrackets) {
  TestLibrary library(R"FIDL(
library example;

type Foo = struct {
    bad_no_brackets vector<uint8>:10,optional;
};
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind,
                                      fidl::ErrUnexpectedTokenOfKind);
}

TEST(ParsingTests, GoodSingleConstraint) {
  TestLibrary library(R"FIDL(
library example;

type Foo = struct {
  with_brackets vector<int32>:<10>;
  without_brackets vector<int32>:10;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ParsingTests, BadSubtypeCtor) {
  TestLibrary library(R"FIDL(
library example;

type Foo = struct : uint32 {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotSpecifySubtype);
}

TEST(ParsingTests, BadLayoutClass) {
  TestLibrary library(R"FIDL(
library example;

type Foo = foobar {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidLayoutClass);
}

TEST(ParsingTests, BadIdentifierModifiers) {
  TestLibrary library(R"FIDL(
library example;

type Foo = struct {
  data strict uint32;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotSpecifyModifier);
}

TEST(ParsingTests, BadIdentifierWithConstraintsModifiers) {
  TestLibrary library(R"FIDL(
library example;

type Bar = table {};

type Foo = struct {
  data strict Bar:optional;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotSpecifyModifier);
}

}  // namespace
