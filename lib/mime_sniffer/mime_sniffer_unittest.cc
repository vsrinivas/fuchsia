// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/mime_sniffer/mime_sniffer.h"
#include "gtest/gtest.h"

namespace mime_sniffer {
namespace {

// Turn |str|, a constant string with one or more embedded NULs, along with
// a NUL terminator, into an std::string() containing just that data.
// Turn |str|, a string with one or more embedded NULs, into an std::string()
template <size_t N>
std::string MakeConstantString(const char (&str)[N]) {
  return std::string(str, N - 1);
}

static bool IsHtmlType(const std::string& content) {
  std::string mime_type;
  bool have_enough_content;
  return SniffForHTML(content.data(), content.size(), &have_enough_content,
                      &mime_type);
}

TEST(MimeSnifferTest, HtmlSniffingTest) {
  std::string result;
  EXPECT_TRUE(IsHtmlType(MakeConstantString("<!DOCTYPE html PUBLIC")));
  EXPECT_TRUE(IsHtmlType(MakeConstantString(" \n <hTmL>\n <hea")));
  EXPECT_TRUE(IsHtmlType(MakeConstantString("<html><body>text</body></html>")));
  EXPECT_TRUE(IsHtmlType(MakeConstantString("<body>text</body>")));
  EXPECT_TRUE(IsHtmlType(MakeConstantString("<p>text</p>")));
  EXPECT_TRUE(IsHtmlType(MakeConstantString("<div>text</div>")));
  EXPECT_TRUE(IsHtmlType(MakeConstantString("<head>text</head>")));
  EXPECT_TRUE(IsHtmlType(MakeConstantString("<iframe>text</iframe>")));
  EXPECT_TRUE(IsHtmlType(MakeConstantString("<h1>text</h1>")));
  EXPECT_TRUE(IsHtmlType(MakeConstantString("<br/>")));
  EXPECT_TRUE(IsHtmlType(MakeConstantString("<a>text</a>")));
  EXPECT_TRUE(IsHtmlType(MakeConstantString("<table>text</table>")));
  EXPECT_TRUE(IsHtmlType(MakeConstantString("<script>text</script>")));
  EXPECT_TRUE(IsHtmlType(MakeConstantString("<font>text</font>")));
  EXPECT_TRUE(IsHtmlType(MakeConstantString("<b>text</b>")));
  EXPECT_TRUE(IsHtmlType(MakeConstantString("<title>text</title>")));
  EXPECT_TRUE(IsHtmlType(MakeConstantString("<style>text</style>")));

  EXPECT_FALSE(
      IsHtmlType(MakeConstantString("GIF87a\n<html>\n<body>"
                                    "<script>alert('haxorzed');\n</script>"
                                    "</body></html>\n")));
  EXPECT_FALSE(
      IsHtmlType(MakeConstantString("a\n<html>\n<body>"
                                    "<script>alert('haxorzed');\n</script>"
                                    "</body></html>\n")));
}

}  // namespace
}  // namespace mime_sniffer
