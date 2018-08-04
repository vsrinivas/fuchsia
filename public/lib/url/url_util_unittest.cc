// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/url/url_util.h"
#include "gtest/gtest.h"
#include "lib/fxl/macros.h"
#include "lib/url/third_party/mozilla/url_parse.h"
#include "lib/url/url_canon.h"
#include "lib/url/url_canon_stdstring.h"
#include "lib/url/url_test_utils.h"

namespace url {

TEST(URLUtilTest, FindAndCompareScheme) {
  Component found_scheme;

  // Simple case where the scheme is found and matches.
  const char kStr1[] = "http://www.com/";
  EXPECT_TRUE(FindAndCompareScheme(kStr1, static_cast<int>(strlen(kStr1)),
                                   "http", NULL));
  EXPECT_TRUE(FindAndCompareScheme(kStr1, static_cast<int>(strlen(kStr1)),
                                   "http", &found_scheme));
  EXPECT_TRUE(found_scheme == Component(0, 4));

  // A case where the scheme is found and doesn't match.
  EXPECT_FALSE(FindAndCompareScheme(kStr1, static_cast<int>(strlen(kStr1)),
                                    "https", &found_scheme));
  EXPECT_TRUE(found_scheme == Component(0, 4));

  // A case where there is no scheme.
  const char kStr2[] = "httpfoobar";
  EXPECT_FALSE(FindAndCompareScheme(kStr2, static_cast<int>(strlen(kStr2)),
                                    "http", &found_scheme));
  EXPECT_TRUE(found_scheme == Component());

  // When there is an empty scheme, it should match the empty scheme.
  const char kStr3[] = ":foo.com/";
  EXPECT_TRUE(FindAndCompareScheme(kStr3, static_cast<int>(strlen(kStr3)), "",
                                   &found_scheme));
  EXPECT_TRUE(found_scheme == Component(0, 0));

  // But when there is no scheme, it should fail.
  EXPECT_FALSE(FindAndCompareScheme("", 0, "", &found_scheme));
  EXPECT_TRUE(found_scheme == Component());

  // When there is a whitespace char in scheme, it should canonicalize the URL
  // before comparison.
  const char whtspc_str[] = " \r\n\tjav\ra\nscri\tpt:alert(1)";
  EXPECT_TRUE(FindAndCompareScheme(whtspc_str,
                                   static_cast<int>(strlen(whtspc_str)),
                                   "javascript", &found_scheme));
  EXPECT_TRUE(found_scheme == Component(1, 10));

  // Control characters should be stripped out on the ends, and kept in the
  // middle.
  const char ctrl_str[] = "\02jav\02scr\03ipt:alert(1)";
  EXPECT_FALSE(FindAndCompareScheme(ctrl_str,
                                    static_cast<int>(strlen(ctrl_str)),
                                    "javascript", &found_scheme));
  EXPECT_TRUE(found_scheme == Component(1, 11));
}

TEST(URLUtilTest, TestEncodeURIComponent) {
  struct EncodeCase {
    const char* input;
    const char* output;
  } encode_cases[] = {
      {"hello, world", "hello%2C%20world"},
      {"\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F",
       "%01%02%03%04%05%06%07%08%09%0A%0B%0C%0D%0E%0F"},
      {"\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1A\x1B\x1C\x1D\x1E\x1F",
       "%10%11%12%13%14%15%16%17%18%19%1A%1B%1C%1D%1E%1F"},
      {" !\"#$%&'()*+,-./", "%20!%22%23%24%25%26%27()*%2B%2C-.%2F"},
      {"0123456789:;<=>?", "0123456789%3A%3B%3C%3D%3E%3F"},
      {"@ABCDEFGHIJKLMNO", "%40ABCDEFGHIJKLMNO"},
      {"PQRSTUVWXYZ[\\]^_", "PQRSTUVWXYZ%5B%5C%5D%5E_"},
      {"`abcdefghijklmno", "%60abcdefghijklmno"},
      {"pqrstuvwxyz{|}~\x7f", "pqrstuvwxyz%7B%7C%7D~%7F"},
  };

  for (const auto& encode_case : encode_cases) {
    const char* input = encode_case.input;
    RawCanonOutputT<char> buffer;
    EncodeURIComponent(input, strlen(input), &buffer);
    std::string output(buffer.data(), buffer.length());
    EXPECT_EQ(encode_case.output, output);
  }
}

TEST(URLUtilTest, TestResolveRelativeWithNonStandardBase) {
  // This tests non-standard (in the sense that GIsStandard() == false)
  // hierarchical schemes.
  struct ResolveRelativeCase {
    const char* base;
    const char* rel;
    bool is_valid;
    const char* out;
  } resolve_non_standard_cases[] = {
      // Resolving a relative path against a non-hierarchical URL should fail.
      {"scheme:opaque_data", "/path", false, ""},
      // Resolving a relative path against a non-standard authority-based base
      // URL doesn't alter the authority section.
      {"scheme://Authority/", "../path", true, "scheme://Authority/path"},
      // A non-standard hierarchical base is resolved with path URL
      // canonicalization rules.
      {"data:/Blah:Blah/", "file.html", true, "data:/Blah:Blah/file.html"},
      {"data:/Path/../part/part2", "file.html", true,
       "data:/Path/../part/file.html"},
      // Path URL canonicalization rules also apply to non-standard authority-
      // based URLs.
      {"custom://Authority/", "file.html", true,
       "custom://Authority/file.html"},
      {"custom://Authority/", "other://Auth/", true, "other://Auth/"},
      {"custom://Authority/", "../../file.html", true,
       "custom://Authority/file.html"},
      {"custom://Authority/path/", "file.html", true,
       "custom://Authority/path/file.html"},
      {"custom://Authority:NoCanon/path/", "file.html", true,
       "custom://Authority:NoCanon/path/file.html"},
      // It's still possible to get an invalid path URL.
      {"custom://Invalid:!#Auth/", "file.html", false, ""},
      // A path with an authority section gets canonicalized under standard URL
      // rules, even though the base was non-standard.
      {"content://content.Provider/", "//other.Provider", true,
       "content://other.provider/"},
      // Resolving an absolute URL doesn't cause canonicalization of the
      // result.
      {"about:blank", "custom://Authority", true, "custom://Authority"},
      // Fragment URLs can be resolved against a non-standard base.
      {"scheme://Authority/path", "#fragment", true,
       "scheme://Authority/path#fragment"},
      {"scheme://Authority/", "#fragment", true,
       "scheme://Authority/#fragment"},
      // Resolving should fail if the base URL is authority-based but is
      // missing a path component (the '/' at the end).
      {"scheme://Authority", "path", false, ""},
      // Test resolving a fragment (only) against any kind of base-URL.
      {"about:blank", "#id42", true, "about:blank#id42"},
      {"about:blank", " #id42", true, "about:blank#id42"},
      {"about:blank#oldfrag", "#newfrag", true, "about:blank#newfrag"},
      // A surprising side effect of allowing fragments to resolve against
      // any URL scheme is we might break javascript: URLs by doing so...
      {"javascript:alert('foo#bar')", "#badfrag", true,
       "javascript:alert('foo#badfrag"},
      // In this case, the backslashes will not be canonicalized because it's a
      // non-standard URL, but they will be treated as a path separators,
      // giving the base URL here a path of "\".
      //
      // The result here is somewhat arbitrary. One could argue it should be
      // either "aaa://a\" or "aaa://a/" since the path is being replaced with
      // the "current directory". But in the context of resolving on data URLs,
      // adding the requested dot doesn't seem wrong either.
      {"aaa://a\\", "aaa:.", true, "aaa://a\\."}};

  for (const auto& test_data : resolve_non_standard_cases) {
    Parsed base_parsed;
    ParsePathURL(test_data.base, strlen(test_data.base), false, &base_parsed);

    std::string resolved;
    StdStringCanonOutput output(&resolved);
    Parsed resolved_parsed;
    bool valid = ResolveRelative(
        test_data.base, strlen(test_data.base), base_parsed, test_data.rel,
        strlen(test_data.rel), NULL, &output, &resolved_parsed);
    output.Complete();

    EXPECT_EQ(test_data.is_valid, valid);
    if (test_data.is_valid && valid)
      EXPECT_EQ(test_data.out, resolved);
  }
}

TEST(URLUtilTest, TestNoRefComponent) {
  // The hash-mark must be ignored when mailto: scheme is parsed,
  // even if the URL has a base and relative part.
  const char* base = "mailto://to/";
  const char* rel = "any#body";

  Parsed base_parsed;
  ParsePathURL(base, strlen(base), false, &base_parsed);

  std::string resolved;
  StdStringCanonOutput output(&resolved);
  Parsed resolved_parsed;

  bool valid = ResolveRelative(base, strlen(base), base_parsed, rel,
                               strlen(rel), NULL, &output, &resolved_parsed);
  EXPECT_TRUE(valid);
  EXPECT_FALSE(resolved_parsed.ref.is_valid());
}

}  // namespace url
