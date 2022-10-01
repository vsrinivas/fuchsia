// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/lib/rust_url/rust_url.h"

TEST(RustUrlTest, EmptyUrl) {
  RustUrl url;
  EXPECT_EQ(url.Parse(""), ZX_ERR_INVALID_ARGS);
}

struct URLParseCase {
  std::string input;

  std::optional<std::string> domain;
};

static URLParseCase parse_cases[] = {
    // Regular URL with all the parts
    {"http://user:pass@foo:21/bar;par?b#c", "foo"},

    // OK to omit //
    {"http:foo.com", "foo.com"},

    // Spaces!
    {"http://f:21/ b ? d # e ", "f"},

    // Weird port numbers
    {"http://f:/c", "f"},
    {"http://f:0/c", "f"},
    {"http://f:00000000000000/c", "f"},
    {"http://f:00000000000000000000080/c", "f"},
    {"http://f:\n/c", "f"},

    // Username/passwords and things that look like them
    {"http://a:b@c:29/d", "c"},
    {"http::@c:29", "c"},
    {"http://&a:foo(bc@d:2/", "d"},
    {"http://::@c@d:2", "d"},
    {"http://foo.com:b@d/", "d"},

    // Backslashes
    {"http://foo.com/\\@", "foo.com"},
    {"http:\\\\foo.com\\", "foo.com"},
    {"http:\\\\a\\b:c\\d@foo.com\\", "a"},

    // Tolerate different numbers of slashes.
    {"foo:/", ""},
};

class URLParseTest : public testing::TestWithParam<URLParseCase> {};

TEST_P(URLParseTest, Success) {
  RustUrl url;
  EXPECT_EQ(url.Parse(GetParam().input), ZX_OK);
  EXPECT_EQ(url.Domain(), GetParam().domain);
}

INSTANTIATE_TEST_SUITE_P(ParseSuccessSuite, URLParseTest, testing::ValuesIn(parse_cases));
