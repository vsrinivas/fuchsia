// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>
#include <fidl/tree_visitor.h>

#include "examples.h"
#include "test_library.h"

namespace {

// A TreeVisitor that reads in a file and spits back out the same file
class NoopTreeVisitor : public fidl::raw::DeclarationOrderTreeVisitor {
public:
    NoopTreeVisitor()
        : last_source_location_(nullptr) {}

    virtual void OnSourceElementStart(const fidl::raw::SourceElement& element) override {
        OnSourceElementShared(element.start_);
    }

    virtual void OnSourceElementEnd(const fidl::raw::SourceElement& element) override {
        OnSourceElementShared(element.end_);
    }
    void OnSourceElementShared(const fidl::Token& current_token) {
        const char* ws_location = current_token.previous_end().data().data();
        // Printed code must increase in monotonic order, for two reasons.
        // First of all, we don't reorder anything.  Second of all, the start
        // token for an identifier list (for example) is the same as the start
        // token for the first identifier, so we need to make sure we don't
        // print that token twice.
        if (ws_location > last_source_location_) {
            int size = (int)(current_token.data().data() - current_token.previous_end().data().data());
            std::string gap(ws_location, size);
            std::string content(current_token.data().data(), current_token.data().size());
            output_ += gap + content;
            last_source_location_ = ws_location;
        }
    }
    std::string& output() { return output_; }

private:
    std::string output_;
    const char* last_source_location_;
};

// Provides more useful context for string diff than EXPECT_STR_EQ, which shows
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
bool read_and_write_direct_test() {
    BEGIN_TEST;

    for (auto element : Examples::map()) {
        TestLibrary library(element.first, element.second);
        std::unique_ptr<fidl::raw::File> ast;
        EXPECT_TRUE(library.Parse(&ast));

        NoopTreeVisitor visitor;
        visitor.OnFile(ast);
        std::string expected(library.source_file().data());
        std::string output = visitor.output();
        const char* actual = output.c_str();
        std::string d = targeted_diff(expected.c_str(), actual, output.size());
        d = element.first + ": " + d;

        EXPECT_STR_EQ(expected.c_str(), actual, d.c_str());
    }

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(visitor_tests)
RUN_TEST(read_and_write_direct_test)
END_TEST_CASE(visitor_tests)
