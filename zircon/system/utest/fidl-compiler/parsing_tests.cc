// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <locale.h>

#include <fidl/attributes.h>
#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/raw_ast.h>
#include <fidl/source_file.h>
#include <unittest/unittest.h>

#include "test_library.h"

namespace {

// Test that an invalid compound identifier fails parsing. Regression
// test for FIDL-263.
bool bad_compound_identifier_test() {
  BEGIN_TEST;

  // The leading 0 in the library name causes parsing an Identifier
  // to fail, and then parsing a CompoundIdentifier to fail.
  TestLibrary library(R"FIDL(
library 0fidl.test.badcompoundidentifier;
)FIDL");
  EXPECT_FALSE(library.Compile());
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "unexpected token");

  END_TEST;
}

// Test that library name formatting checks are done in the parser
bool bad_library_name_test() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library a_b;
)FIDL");

  std::unique_ptr<fidl::raw::File> ast;
  library.Parse(&ast);
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "Invalid library name component a_b");

  END_TEST;
}

// Test that otherwise reserved words can be appropriarely parsed when context
// is clear.
bool parsing_reserved_words_in_struct_test() {
  BEGIN_TEST;

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

  END_TEST;
}

bool parsing_handles_in_struct_test() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct Handles {
    handle plain_handle;

    handle<bti> bti_handle;
    handle<channel> channel_handle;
    handle<debuglog> debuglog_handle;
    handle<event> event_handle;
    handle<eventpair> eventpair_handle;
    handle<exception> exception_handle;
    handle<fifo> fifo_handle;
    handle<guest> guest_handle;
    handle<interrupt> interrupt_handle;
    handle<iommu> iommu_handle;
    handle<job> job_handle;
    handle<pager> pager_handle;
    handle<pcidevice> pcidevice_handle;
    handle<pmt> pmt_handle;
    handle<port> port_handle;
    handle<process> process_handle;
    handle<profile> profile_handle;
    handle<resource> resource_handle;
    handle<socket> socket_handle;
    handle<suspendtoken> suspendtoken_handle;
    handle<thread> thread_handle;
    handle<timer> timer_handle;
    handle<vcpu> vcpu_handle;
    handle<vmar> vmar_handle;
    handle<vmo> vmo_handle;
};
)FIDL");

  EXPECT_TRUE(library.Compile());

  END_TEST;
}

// Test that otherwise reserved words can be appropriarely parsed when context
// is clear.
bool parsing_reserved_words_in_union_test() {
  BEGIN_TEST;

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

  END_TEST;
}

// Test that otherwise reserved words can be appropriarely parsed when context
// is clear.
bool parsing_reserved_words_in_protocol_test() {
  BEGIN_TEST;

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

  END_TEST;
}

bool bad_char_at_sign_test() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library test;

struct Test {
    uint8 @uint8;
};
)FIDL");
  EXPECT_FALSE(library.Compile());
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "invalid character '@'");

  END_TEST;
}

bool bad_char_slash_test() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library test;

struct Test / {
    uint8 uint8;
};
)FIDL");
  EXPECT_FALSE(library.Compile());
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "invalid character '/'");

  END_TEST;
}

bool bad_identifier_test() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library test;

struct test_ {
    uint8 uint8;
};
)FIDL");
  EXPECT_FALSE(library.Compile());
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "invalid identifier 'test_'");

  END_TEST;
}

class LocaleSwapper {
 public:
  explicit LocaleSwapper(const char* new_locale) { old_locale_ = setlocale(LC_ALL, new_locale); }
  ~LocaleSwapper() { setlocale(LC_ALL, old_locale_); }

 private:
  const char* old_locale_;
};

static bool invalid_character_test(void) {
  BEGIN_TEST;

  LocaleSwapper swapper("de_DE.iso88591");
  TestLibrary test_library("invalid.character.fidl", R"FIDL(
library fidl.test.maxbytes;

// This is all alphanumeric in the appropriate locale, but not a valid
// identifier.
struct ß {
    int32 x;
};

)FIDL");
  ASSERT_FALSE(test_library.Compile());

  const auto& errors = test_library.errors();
  EXPECT_NE(errors.size(), 0);
  ASSERT_STR_STR(errors[0].data(), "invalid character");

  END_TEST;
}

bool empty_struct_test() {
  BEGIN_TEST;

  TestLibrary library("empty_struct.fidl", R"FIDL(
library fidl.test.emptystruct;

struct Empty {
};

)FIDL");
  EXPECT_TRUE(library.Compile());

  END_TEST;
}

bool warn_on_type_alias_before_imports() {
  BEGIN_TEST;

  SharedAmongstLibraries shared;
  TestLibrary dependency("dependent.fidl", R"FIDL(
library dependent;

struct Something {};
)FIDL",
                         &shared);
  ASSERT_TRUE(dependency.Compile());

  TestLibrary library("example.fidl", R"FIDL(
library example;

using foo = int16;
using dependent;

struct UseDependent {
    dependent.Something field;
};
)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_TRUE(library.Compile());

  const auto& warnings = library.warnings();
  ASSERT_EQ(warnings.size(), 1);
  ASSERT_STR_STR(warnings[0].data(), "library imports must be grouped at top-of-file");

  END_TEST;
}

bool multiline_comment_has_correct_source_span() {
  BEGIN_TEST;

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
      ast->struct_declaration_list.front()->attributes->attributes.front();
  ASSERT_STR_EQ(attribute.name.c_str(), "Doc");
  ASSERT_STR_EQ(std::string(attribute.span().data()).c_str(),
                R"EXPECTED(/// A
  /// multiline
  /// comment!)EXPECTED");

  END_TEST;
}

bool doc_comment_blank_line_test() {
  BEGIN_TEST;

  TestLibrary library("example.fidl", R"FIDL(
library example;

/// start

/// end
struct Empty{};
)FIDL");

  std::unique_ptr<fidl::raw::File> ast;
  library.set_warnings_as_errors(true);
  library.Parse(&ast);
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "cannot have blank lines within doc comment block");

  END_TEST;
}

bool comment_inside_doc_comment_test() {
  BEGIN_TEST;

  TestLibrary library("example.fidl", R"FIDL(
library example;

/// start
// middle
/// end
struct Empty{};
)FIDL");

  std::unique_ptr<fidl::raw::File> ast;
  library.Parse(&ast);
  auto warnings = library.warnings();
  ASSERT_GE(warnings.size(), 1);
  ASSERT_STR_STR(warnings[0].c_str(), "cannot have comment within doc comment block");

  END_TEST;
}

bool doc_comment_with_comment_blank_line_test() {
  BEGIN_TEST;

  TestLibrary library("example.fidl", R"FIDL(
library example;

/// start
// middle

/// end
struct Empty{};
)FIDL");

  std::unique_ptr<fidl::raw::File> ast;
  library.Parse(&ast);
  auto warnings = library.warnings();
  ASSERT_EQ(warnings.size(), 2);
  ASSERT_STR_STR(warnings[0].c_str(), "cannot have comment within doc comment block");
  ASSERT_STR_STR(warnings[1].c_str(), "cannot have blank lines within doc comment block");

  END_TEST;
}

bool doc_comment_not_allowed_on_params() {
  BEGIN_TEST;

  TestLibrary library("example.fidl", R"FIDL(
library example;

protocol Example {
  Method(/// Doc comment
         Bool b);
};
)FIDL");

  std::unique_ptr<fidl::raw::File> ast;
  library.Parse(&ast);
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "cannot have doc comment on parameters");

  END_TEST;
}

bool comments_surrounding_doc_comment_test() {
  BEGIN_TEST;

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

  END_TEST;
}

bool blank_lines_after_doc_comment_test() {
  BEGIN_TEST;

  TestLibrary library("example.fidl", R"FIDL(
library example;

/// doc comment



struct Empty{};
)FIDL");

  std::unique_ptr<fidl::raw::File> ast;
  library.set_warnings_as_errors(true);
  ASSERT_TRUE(library.Parse(&ast));

  END_TEST;
}

bool blank_lines_after_doc_comment_with_comment_test() {
  BEGIN_TEST;

  TestLibrary library("example.fidl", R"FIDL(
library example;

/// doc comment


// regular comment

struct Empty{};
)FIDL");

  std::unique_ptr<fidl::raw::File> ast;
  library.set_warnings_as_errors(true);
  ASSERT_TRUE(library.Parse(&ast));

  END_TEST;
}

bool trailing_doc_comment_test() {
  BEGIN_TEST;

  TestLibrary library("example.fidl", R"FIDL(
library example;

struct Empty{};
/// bad
)FIDL");

  std::unique_ptr<fidl::raw::File> ast;
  library.Parse(&ast);
  auto warnings = library.warnings();
  ASSERT_EQ(warnings.size(), 1);
  ASSERT_STR_STR(warnings[0].c_str(), "doc comment must be followed by a declaration");

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(parsing_tests)
RUN_TEST(bad_compound_identifier_test)
RUN_TEST(bad_library_name_test)
RUN_TEST(parsing_reserved_words_in_struct_test)
RUN_TEST(parsing_handles_in_struct_test);
RUN_TEST(parsing_reserved_words_in_union_test)
RUN_TEST(parsing_reserved_words_in_protocol_test)
RUN_TEST(bad_char_at_sign_test)
RUN_TEST(bad_char_slash_test)
RUN_TEST(bad_identifier_test)
RUN_TEST(invalid_character_test)
RUN_TEST(empty_struct_test)
RUN_TEST(warn_on_type_alias_before_imports)
RUN_TEST(multiline_comment_has_correct_source_span)
RUN_TEST(doc_comment_blank_line_test)
RUN_TEST(doc_comment_with_comment_blank_line_test)
RUN_TEST(doc_comment_not_allowed_on_params)
RUN_TEST(comment_inside_doc_comment_test)
RUN_TEST(comments_surrounding_doc_comment_test)
RUN_TEST(blank_lines_after_doc_comment_test)
RUN_TEST(blank_lines_after_doc_comment_with_comment_test)
RUN_TEST(trailing_doc_comment_test)
END_TEST_CASE(parsing_tests)
