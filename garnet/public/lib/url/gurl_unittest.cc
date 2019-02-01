// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/url/gurl.h"
#include "gtest/gtest.h"
#include "lib/fxl/macros.h"
#include "lib/url/url_canon.h"
#include "lib/url/url_test_utils.h"

namespace url {

namespace {

// Returns the canonicalized string for the given URL string for the
// GURLTest.Types test.
std::string TypesTestCase(const char* src) {
  GURL gurl(src);
  return gurl.possibly_invalid_spec();
}

}  // namespace

// Different types of URLs should be handled differently, and handed off to
// different canonicalizers.
TEST(GURLTest, Types) {
  // URLs with unknown schemes should be treated as path URLs, even when they
  // have things like "://".
  EXPECT_EQ("something:///HOSTNAME.com/",
            TypesTestCase("something:///HOSTNAME.com/"));

  // Conversely, URLs with known schemes should always trigger standard URL
  // handling.
  EXPECT_EQ("http://hostname.com/", TypesTestCase("http:HOSTNAME.com"));
  EXPECT_EQ("http://hostname.com/", TypesTestCase("http:/HOSTNAME.com"));
  EXPECT_EQ("http://hostname.com/", TypesTestCase("http://HOSTNAME.com"));
  EXPECT_EQ("http://hostname.com/", TypesTestCase("http:///HOSTNAME.com"));
}

// Test the basic creation and querying of components in a GURL. We assume that
// the parser is already tested and works, so we are mostly interested if the
// object does the right thing with the results.
TEST(GURLTest, Components) {
  GURL url("http://user:pass@google.com:99/foo;bar?q=a#ref");
  EXPECT_TRUE(url.is_valid());
  EXPECT_TRUE(url.SchemeIs("http"));
  EXPECT_FALSE(url.SchemeIsFile());

  EXPECT_EQ("http", url.scheme());
  EXPECT_EQ("user", url.username());
  EXPECT_EQ("pass", url.password());
  EXPECT_EQ("google.com", url.host());
  EXPECT_EQ("99", url.port());
  EXPECT_EQ(99, url.IntPort());
  EXPECT_EQ("/foo;bar", url.path());
  EXPECT_EQ("q=a", url.query());
  EXPECT_EQ("ref", url.ref());

  // Test parsing userinfo with special characters.
  GURL url_special_pass("http://user:%40!$&'()*+,;=:@google.com:12345");
  EXPECT_TRUE(url_special_pass.is_valid());
  // GURL canonicalizes some delimiters.
  EXPECT_EQ("%40!$&%27()*+,%3B%3D%3A", url_special_pass.password());
  EXPECT_EQ("google.com", url_special_pass.host());
  EXPECT_EQ("12345", url_special_pass.port());
}

TEST(GURLTest, Empty) {
  GURL url;
  EXPECT_FALSE(url.is_valid());
  EXPECT_EQ("", url.spec());

  EXPECT_EQ("", url.scheme());
  EXPECT_EQ("", url.username());
  EXPECT_EQ("", url.password());
  EXPECT_EQ("", url.host());
  EXPECT_EQ("", url.port());
  EXPECT_EQ(PORT_UNSPECIFIED, url.IntPort());
  EXPECT_EQ("", url.path());
  EXPECT_EQ("", url.query());
  EXPECT_EQ("", url.ref());
}

TEST(GURLTest, Copy) {
  GURL url("http://user:pass@google.com:99/foo;bar?q=a#ref");

  GURL url2(url);
  EXPECT_TRUE(url2.is_valid());

  EXPECT_EQ("http://user:pass@google.com:99/foo;bar?q=a#ref", url2.spec());
  EXPECT_EQ("http", url2.scheme());
  EXPECT_EQ("user", url2.username());
  EXPECT_EQ("pass", url2.password());
  EXPECT_EQ("google.com", url2.host());
  EXPECT_EQ("99", url2.port());
  EXPECT_EQ(99, url2.IntPort());
  EXPECT_EQ("/foo;bar", url2.path());
  EXPECT_EQ("q=a", url2.query());
  EXPECT_EQ("ref", url2.ref());

  // Copying of invalid URL should be invalid
  GURL invalid;
  GURL invalid2(invalid);
  EXPECT_FALSE(invalid2.is_valid());
  EXPECT_EQ("", invalid2.spec());
  EXPECT_EQ("", invalid2.scheme());
  EXPECT_EQ("", invalid2.username());
  EXPECT_EQ("", invalid2.password());
  EXPECT_EQ("", invalid2.host());
  EXPECT_EQ("", invalid2.port());
  EXPECT_EQ(PORT_UNSPECIFIED, invalid2.IntPort());
  EXPECT_EQ("", invalid2.path());
  EXPECT_EQ("", invalid2.query());
  EXPECT_EQ("", invalid2.ref());
}

TEST(GURLTest, Assign) {
  GURL url("http://user:pass@google.com:99/foo;bar?q=a#ref");

  GURL url2;
  url2 = url;
  EXPECT_TRUE(url2.is_valid());

  EXPECT_EQ("http://user:pass@google.com:99/foo;bar?q=a#ref", url2.spec());
  EXPECT_EQ("http", url2.scheme());
  EXPECT_EQ("user", url2.username());
  EXPECT_EQ("pass", url2.password());
  EXPECT_EQ("google.com", url2.host());
  EXPECT_EQ("99", url2.port());
  EXPECT_EQ(99, url2.IntPort());
  EXPECT_EQ("/foo;bar", url2.path());
  EXPECT_EQ("q=a", url2.query());
  EXPECT_EQ("ref", url2.ref());

  // Assignment of invalid URL should be invalid
  GURL invalid;
  GURL invalid2;
  invalid2 = invalid;
  EXPECT_FALSE(invalid2.is_valid());
  EXPECT_EQ("", invalid2.spec());
  EXPECT_EQ("", invalid2.scheme());
  EXPECT_EQ("", invalid2.username());
  EXPECT_EQ("", invalid2.password());
  EXPECT_EQ("", invalid2.host());
  EXPECT_EQ("", invalid2.port());
  EXPECT_EQ(PORT_UNSPECIFIED, invalid2.IntPort());
  EXPECT_EQ("", invalid2.path());
  EXPECT_EQ("", invalid2.query());
  EXPECT_EQ("", invalid2.ref());
}

TEST(GURLTest, IsValid) {
  const char* valid_cases[] = {
      "http://google.com",
      "unknown://google.com",
      "http://user:pass@google.com",
      "http://google.com:12345",
      "http://google.com/path",
      "http://google.com//path",
      "http://google.com?k=v#fragment",
      "http://user:pass@google.com:12345/path?k=v#fragment",
      "http:/path",
      "http:path",
      "://google.com",
  };

  for (const char* valid_case : valid_cases) {
    EXPECT_TRUE(GURL(valid_case).is_valid()) << "Case: " << valid_case;
  }

  const char* invalid_cases[] = {
      "http://?k=v",
      "http:://google.com",
      "http//google.com",
      "http://google.com:12three45",
      "path",
  };
  for (const char* invalid_case : invalid_cases) {
    EXPECT_FALSE(GURL(invalid_case).is_valid()) << "Case: " << invalid_case;
  }
}

TEST(GURLTest, ExtraSlashesBeforeAuthority) {
  // According to RFC3986, the hierarchical part for URI with an authority
  // must use only two slashes; GURL intentionally just ignores extra slashes
  // if there are more than 2, and parses the following part as an authority.
  GURL url("http:///host");
  EXPECT_EQ("host", url.host());
  EXPECT_EQ("/", url.path());
}

// Given an invalid URL, we should still get most of the components.
TEST(GURLTest, ComponentGettersWorkEvenForInvalidURL) {
  GURL url("http:google.com:foo");
  EXPECT_FALSE(url.is_valid());
  EXPECT_EQ("http://google.com:foo/", url.possibly_invalid_spec());

  EXPECT_EQ("http", url.scheme());
  EXPECT_EQ("", url.username());
  EXPECT_EQ("", url.password());
  EXPECT_EQ("google.com", url.host());
  EXPECT_EQ("foo", url.port());
  EXPECT_EQ(PORT_INVALID, url.IntPort());
  EXPECT_EQ("/", url.path());
  EXPECT_EQ("", url.query());
  EXPECT_EQ("", url.ref());
}

TEST(GURLTest, Resolve) {
  // The tricky cases for relative URL resolving are tested in the
  // canonicalizer unit test. Here, we just test that the GURL integration
  // works properly.
  struct ResolveCase {
    const char* base;
    const char* relative;
    bool expected_valid;
    const char* expected;
  } resolve_cases[] = {
      {"http://www.google.com/", "foo.html", true,
       "http://www.google.com/foo.html"},
      {"http://www.google.com/foo/", "bar", true,
       "http://www.google.com/foo/bar"},
      {"http://www.google.com/foo/", "/bar", true, "http://www.google.com/bar"},
      {"http://www.google.com/foo", "bar", true, "http://www.google.com/bar"},
      {"http://www.google.com/", "http://images.google.com/foo.html", true,
       "http://images.google.com/foo.html"},
      {"http://www.google.com/blah/bloo?c#d", "../../../hello/./world.html?a#b",
       true, "http://www.google.com/hello/world.html?a#b"},
      {"http://www.google.com/foo#bar", "#com", true,
       "http://www.google.com/foo#com"},
      {"http://www.google.com/", "Https:images.google.com", true,
       "https://images.google.com/"},
      // A non-standard base can be replaced with a standard absolute URL.
      {"data:blahblah", "http://google.com/", true, "http://google.com/"},
      {"data:blahblah", "http:google.com", true, "http://google.com/"},
  };

  for (const auto& resolve_case : resolve_cases) {
    GURL input(resolve_case.base);
    GURL output = input.Resolve(resolve_case.relative);
    EXPECT_EQ(resolve_case.expected_valid, output.is_valid())
        << resolve_case.expected;
    EXPECT_EQ(resolve_case.expected, output.spec()) << resolve_case.expected;
  }
}

TEST(GURLTest, GetWithEmptyPath) {
  struct TestCase {
    const char* input;
    const char* expected;
  } cases[] = {
      {"http://www.google.com", "http://www.google.com/"},
      {"javascript:window.alert(\"hello, world\");", ""},
      {"http://www.google.com/foo/bar.html?baz=22", "http://www.google.com/"},
  };

  for (const auto& test_case : cases) {
    GURL url(test_case.input);
    GURL empty_path = url.GetWithEmptyPath();
    EXPECT_EQ(test_case.expected, empty_path.spec());
  }
}

TEST(GURLTest, PathForRequest) {
  struct TestCase {
    const char* input;
    const char* expected;
  } cases[] = {
      {"http://www.google.com", "/"},
      {"http://www.google.com/", "/"},
      {"http://www.google.com/foo/bar.html?baz=22", "/foo/bar.html?baz=22"},
      {"http://www.google.com/foo/bar.html#ref", "/foo/bar.html"},
      {"http://www.google.com/foo/bar.html?query#ref", "/foo/bar.html?query"},
  };

  for (const auto& test_case : cases) {
    GURL url(test_case.input);
    std::string path_request = url.PathForRequest();
    EXPECT_EQ(test_case.expected, path_request);
  }
}

TEST(GURLTest, EffectiveIntPort) {
  struct PortTest {
    const char* spec;
    int expected_int_port;
  } port_tests[] = {
      // http
      {"http://www.google.com/", 80},
      {"http://www.google.com:80/", 80},
      {"http://www.google.com:443/", 443},

      // https
      {"https://www.google.com/", 443},
      {"https://www.google.com:443/", 443},
      {"https://www.google.com:80/", 80},

      // ftp
      {"ftp://www.google.com/", 21},
      {"ftp://www.google.com:21/", 21},
      {"ftp://www.google.com:80/", 80},

      // gopher
      {"gopher://www.google.com/", 70},
      {"gopher://www.google.com:70/", 70},
      {"gopher://www.google.com:80/", 80},

      // file - no port
      {"file://www.google.com/", PORT_UNSPECIFIED},
      {"file://www.google.com:443/", PORT_UNSPECIFIED},

      // data - no port
      {"data:www.google.com:90", PORT_UNSPECIFIED},
      {"data:www.google.com", PORT_UNSPECIFIED},
  };

  for (const auto& port_test : port_tests) {
    GURL url(port_test.spec);
    EXPECT_EQ(port_test.expected_int_port, url.EffectiveIntPort());
  }
}

TEST(GURLTest, IPAddress) {
  struct IPTest {
    const char* spec;
    bool expected_ip;
  } ip_tests[] = {
      {"http://www.google.com/", false},
      {"http://192.168.9.1/", true},
      {"http://192.168.9.1.2/", false},
      {"http://192.168.m.1/", false},
      {"http://2001:db8::1/", false},
      {"http://[2001:db8::1]/", true},
      {"", false},
      {"some random input!", false},
  };

  for (const auto& ip_test : ip_tests) {
    GURL url(ip_test.spec);
    EXPECT_EQ(ip_test.expected_ip, url.HostIsIPAddress());
  }
}

TEST(GURLTest, HostNoBrackets) {
  struct TestCase {
    const char* input;
    const char* expected_host;
    const char* expected_plainhost;
  } cases[] = {
      {"http://www.google.com", "www.google.com", "www.google.com"},
      {"http://[2001:db8::1]/", "[2001:db8::1]", "2001:db8::1"},
      {"http://[::]/", "[::]", "::"},

      // Don't require a valid URL, but don't crash either.
      {"http://[]/", "[]", ""},
      {"http://[x]/", "[x]", "x"},
      {"http://[x/", "[x", "[x"},
      {"http://x]/", "x]", "x]"},
      {"http://[/", "[", "["},
      {"http://]/", "]", "]"},
      {"", "", ""},
  };
  for (const auto& test_case : cases) {
    GURL url(test_case.input);
    EXPECT_EQ(test_case.expected_host, url.host());
    EXPECT_EQ(test_case.expected_plainhost, url.HostNoBrackets());
  }
}

TEST(GURLTest, DomainIs) {
  GURL url_1("http://google.com/foo");
  EXPECT_TRUE(url_1.DomainIs("google.com"));

  // Subdomain and port are ignored.
  GURL url_2("http://www.google.com:99/foo");
  EXPECT_TRUE(url_2.DomainIs("google.com"));

  // Different top-level domain.
  GURL url_3("http://www.google.com.cn/foo");
  EXPECT_FALSE(url_3.DomainIs("google.com"));

  // Different host name.
  GURL url_4("http://www.iamnotgoogle.com/foo");
  EXPECT_FALSE(url_4.DomainIs("google.com"));

  // The input must be lower-cased otherwise DomainIs returns false.
  GURL url_5("http://www.google.com/foo");
  EXPECT_FALSE(url_5.DomainIs("Google.com"));

  // If the URL is invalid, DomainIs returns false.
  GURL invalid_url("google.com");
  EXPECT_FALSE(invalid_url.is_valid());
  EXPECT_FALSE(invalid_url.DomainIs("google.com"));
}

TEST(GURLTest, DomainIsTerminatingDotBehavior) {
  // If the host part ends with a dot, it matches input domains
  // with or without a dot.
  GURL url_with_dot("http://www.google.com./foo");
  EXPECT_TRUE(url_with_dot.DomainIs("google.com"));
  EXPECT_TRUE(url_with_dot.DomainIs("google.com."));
  EXPECT_TRUE(url_with_dot.DomainIs(".com"));
  EXPECT_TRUE(url_with_dot.DomainIs(".com."));

  // But, if the host name doesn't end with a dot and the input
  // domain does, then it's considered to not match.
  GURL url_without_dot("http://google.com/foo");
  EXPECT_FALSE(url_without_dot.DomainIs("google.com."));

  // If the URL ends with two dots, it doesn't match.
  GURL url_with_two_dots("http://www.google.com../foo");
  EXPECT_FALSE(url_with_two_dots.DomainIs("google.com"));
}

// Newlines should be stripped from inputs.
TEST(GURLTest, Newlines) {
  // Constructor.
  GURL url_1(" \t ht\ntp://\twww.goo\rgle.com/as\ndf \n ");
  EXPECT_EQ("http://www.google.com/asdf", url_1.spec());

  // Relative path resolver.
  GURL url_2 = url_1.Resolve(" \n /fo\to\r ");
  EXPECT_EQ("http://www.google.com/foo", url_2.spec());

  // Note that newlines are NOT stripped from ReplaceComponents.
}

TEST(GURLTest, IsStandard) {
  GURL a("http:foo/bar");
  EXPECT_TRUE(a.IsStandard());

  GURL b("foo:bar/baz");
  EXPECT_FALSE(b.IsStandard());

  GURL c("foo://bar/baz");
  EXPECT_FALSE(c.IsStandard());
}

TEST(GURLTest, SchemeIsHTTPOrHTTPS) {
  EXPECT_TRUE(GURL("http://bar/").SchemeIsHTTPOrHTTPS());
  EXPECT_TRUE(GURL("HTTPS://BAR").SchemeIsHTTPOrHTTPS());
  EXPECT_FALSE(GURL("ftp://bar/").SchemeIsHTTPOrHTTPS());
}

TEST(GURLTest, SchemeIsWSOrWSS) {
  EXPECT_TRUE(GURL("WS://BAR/").SchemeIsWSOrWSS());
  EXPECT_TRUE(GURL("wss://bar/").SchemeIsWSOrWSS());
  EXPECT_FALSE(GURL("http://bar/").SchemeIsWSOrWSS());
}

TEST(GURLTest, SchemeIsBlob) {
  EXPECT_TRUE(GURL("BLOB://BAR/").SchemeIsBlob());
  EXPECT_TRUE(GURL("blob://bar/").SchemeIsBlob());
  EXPECT_FALSE(GURL("http://bar/").SchemeIsBlob());
}

TEST(GURLTest, ContentAndPathForNonStandardURLs) {
  struct TestCase {
    const char* url;
    const char* expected;
  } cases[] = {
      {"null", ""},
      {"not-a-standard-scheme:this is arbitrary content",
       "this is arbitrary content"},
      {"view-source:http://example.com/path", "http://example.com/path"},
      {"blob:http://example.com/GUID", "http://example.com/GUID"},
      {"blob://http://example.com/GUID", "//http://example.com/GUID"},
      {"blob:http://user:password@example.com/GUID",
       "http://user:password@example.com/GUID"},
  };

  for (const auto& test : cases) {
    GURL url(test.url);
    EXPECT_EQ(test.expected, url.path()) << test.url;
    EXPECT_EQ(test.expected, url.GetContent()) << test.url;
  }
}

}  // namespace url
