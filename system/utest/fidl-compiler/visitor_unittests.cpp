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

// Test that the AST visitor works: ensure that if you visit a file, you can
// reconstruct its original contents.
bool read_and_write_direct_test() {
    BEGIN_TEST;

    for (auto element : Examples::map()) {
        TestLibrary library(element.first, element.second);
        std::unique_ptr<fidl::raw::File> ast;
        EXPECT_TRUE(library.Parse(ast));

        NoopTreeVisitor visitor;
        visitor.OnFile(ast);

        EXPECT_STR_EQ(library.source_file().data().data(),
                      visitor.output().c_str());
    }

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(visitor_tests);
RUN_TEST(read_and_write_direct_test);
END_TEST_CASE(visitor_tests);
