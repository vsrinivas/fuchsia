// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/include/fidl/source_file.h"
#include "tools/fidl/fidlc/include/fidl/tree_visitor.h"
#include "tools/fidl/fidlc/tests/test_library.h"

namespace {

// A TreeVisitor that reads in a file and spits back out the same file
class NoopTreeVisitor : public fidl::raw::DeclarationOrderTreeVisitor {
 public:
  void OnSourceElementStart(const fidl::raw::SourceElement& element) override {
    OnSourceElementShared(element.start_);
  }

  void OnSourceElementEnd(const fidl::raw::SourceElement& element) override {
    OnSourceElementShared(element.end_);
  }
  void OnSourceElementShared(const fidl::Token& current_token) {
    const char* ws_location = current_token.previous_end().data().data();
    // Printed code must increase in monotonic order, for two reasons.
    // First of all, we don't reorder anything.  Second of all, the start
    // token for an identifier list (for example) is the same as the start
    // token for the first identifier, so we need to make sure we don't
    // print that token twice.
    if (ws_location > last_location_) {
      auto size = static_cast<int>(current_token.data().data() -
                                   current_token.previous_end().data().data());
      std::string gap(ws_location, size);
      std::string content(current_token.data().data(), current_token.data().size());
      output_ += gap + content;
      last_location_ = ws_location;
    }
  }
  std::string& output() { return output_; }

 private:
  std::string output_;
  const char* last_location_ = nullptr;
};

// Provides more useful context for string diff than EXPECT_STREQ, which shows
// a limited prefix.  When the string is long, and the difference is buried
// past the limited prefix, the limited prefix doesn't give useful information.
std::string targeted_diff(const char* expected, const char* actual, size_t size) {
  // We want two lines of useful context:
  size_t i = 0;
  size_t last_nl = 0;
  size_t last_last_nl = 0;
  while (i <= size && expected[i] == actual[i]) {
    if (expected[i] == '\n') {
      last_last_nl = last_nl;
      last_nl = i;
    }
    i++;
  }

  size_t start = last_last_nl;
  size_t expected_end = (i + 10 < strlen(expected)) ? i + 10 : strlen(expected) - 1;
  size_t actual_end = (i + 10 < strlen(actual)) ? i + 10 : strlen(actual) - 1;
  std::string s("Expected contains \"");
  s.append(std::string(expected + start, expected_end - start));
  s.append("\" and actual contains \"");
  s.append(std::string(actual + start, actual_end - start));
  s.append("\"");
  return s;
}

// Test that the AST visitor works: ensure that if you visit a file, you can
// reconstruct its original contents.
TEST(VisitorTests, ReadAndWriteDirectTest) {
  // ---------------40---------------- |
  std::string contents = R"FIDL(
/// C1
library foo.bar; // C2

using baz.qux; // C3

/// C4
type MyEnum = enum { // C5
    /// C6
    MY_VALUE = 1; // C7
};

/// C8
type MyTable = table { // C9
    /// C10
    1: field thing; // C11
};

/// C12
alias MyAlias = MyStruct; // C13

/// C14
protocol MyProtocol { // C15
    /// C16
    MyMethod(struct { // C17
        /// C18
        data MyTable; // C19
    }) -> () error MyEnum; // C20
};
)FIDL";

  TestLibrary library(contents);
  std::unique_ptr<fidl::raw::File> ast;
  bool is_parse_success = library.Parse(&ast);

  if (is_parse_success) {
    NoopTreeVisitor visitor;
    visitor.OnFile(ast);
    std::string expected(library.source_file().data());
    std::string output = visitor.output();
    const char* actual = output.c_str();
    std::string d = targeted_diff(expected.c_str(), actual, output.size());
    d = "example.fidl: " + d;

    EXPECT_STREQ(expected.c_str(), actual, "%s", d.c_str());
  }
}

}  // namespace
