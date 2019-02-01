// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/url/url_canon_icu.h"
#include "lib/url/test/icu_unittest_base.h"
#include "lib/url/url_canon.h"
#include "lib/url/url_canon_stdstring.h"
#include "lib/url/url_test_utils.h"
#include "third_party/icu/source/common/unicode/ucnv.h"

namespace url {

using test_utils::WStringToUTF16;

namespace {

class URLCanonIcuTest : public url::test::IcuUnitTestBase {
 public:
  URLCanonIcuTest() {}
  ~URLCanonIcuTest() override {}

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(URLCanonIcuTest);
};

// Wrapper around a UConverter object that managers creation and destruction.
class UConvScoper {
 public:
  explicit UConvScoper(const char* charset_name) {
    UErrorCode err = U_ZERO_ERROR;
    converter_ = ucnv_open(charset_name, &err);
  }

  ~UConvScoper() {
    if (converter_)
      ucnv_close(converter_);
  }

  // Returns the converter object, may be NULL.
  UConverter* converter() const { return converter_; }

 private:
  UConverter* converter_;
};

TEST_F(URLCanonIcuTest, ICUCharsetConverter) {
  struct ICUCase {
    const wchar_t* input;
    const char* encoding;
    const char* expected;
  } icu_cases[] = {
      // UTF-8.
      {L"Hello, world", "utf-8", "Hello, world"},
      {L"\x4f60\x597d", "utf-8", "\xe4\xbd\xa0\xe5\xa5\xbd"},
      // Non-BMP UTF-8.
      {L"!\xd800\xdf00!", "utf-8", "!\xf0\x90\x8c\x80!"},
      // Big5
      {L"\x4f60\x597d", "big5", "\xa7\x41\xa6\x6e"},
      // Unrepresentable character in the destination set.
      {L"hello\x4f60\x06de\x597dworld", "big5",
       "hello\xa7\x41%26%231758%3B\xa6\x6eworld"},
  };

  for (const auto& icu_case : icu_cases) {
    UConvScoper conv(icu_case.encoding);
    ASSERT_TRUE(conv.converter() != NULL);
    ICUCharsetConverter converter(conv.converter());

    std::string str;
    StdStringCanonOutput output(&str);

    std::basic_string<uint16_t> input_str(WStringToUTF16(icu_case.input));
    int input_len = static_cast<int>(input_str.length());
    converter.ConvertFromUTF16(input_str.c_str(), input_len, &output);
    output.Complete();

    EXPECT_STREQ(icu_case.expected, str.c_str());
  }

  // Test string sizes around the resize boundary for the output to make sure
  // the converter resizes as needed.
  const int static_size = 16;
  UConvScoper conv("utf-8");
  ASSERT_TRUE(conv.converter());
  ICUCharsetConverter converter(conv.converter());
  for (int i = static_size - 2; i <= static_size + 2; i++) {
    // Make a string with the appropriate length.
    std::basic_string<uint16_t> input;
    for (int ch = 0; ch < i; ch++)
      input.push_back('a');

    RawCanonOutput<static_size> output;
    converter.ConvertFromUTF16(input.c_str(), static_cast<int>(input.length()),
                               &output);
    EXPECT_EQ(input.length(), static_cast<size_t>(output.length()));
  }
}

TEST_F(URLCanonIcuTest, QueryWithConverter) {
  struct QueryCase {
    const char* input8;
    const char* encoding;
    const char* expected;
  } query_cases[] = {
      // Regular ASCII case in some different encodings.
      {"foo=bar", "utf-8", "?foo=bar"},
      {"foo=bar", "shift_jis", "?foo=bar"},
      {"foo=bar", "gb2312", "?foo=bar"},
      // Chinese input/output
      {"q=\xe4\xbd\xa0\xe5\xa5\xbd", "gb2312", "?q=%C4%E3%BA%C3"},
      {"q=\xe4\xbd\xa0\xe5\xa5\xbd", "big5", "?q=%A7A%A6n"},
      // Unencodable character in the destination character set should be
      // escaped. The escape sequence unescapes to be the entity name:
      // "?q=&#20320;"
      {"q=Chinese\xef\xbc\xa7", "iso-8859-1", "?q=Chinese%26%2365319%3B"},
  };

  for (const auto& query_case : query_cases) {
    Component out_comp;

    UConvScoper conv(query_case.encoding);
    ASSERT_TRUE(!query_case.encoding || conv.converter());
    ICUCharsetConverter converter(conv.converter());

    int len = static_cast<int>(strlen(query_case.input8));
    Component in_comp(0, len);
    std::string out_str;

    StdStringCanonOutput output(&out_str);
    CanonicalizeQuery(query_case.input8, in_comp, &converter, &output,
                      &out_comp);
    output.Complete();

    EXPECT_EQ(query_case.expected, out_str);
  }

  // Extra test for input with embedded NULL;
  std::string out_str;
  StdStringCanonOutput output(&out_str);
  Component out_comp;
  CanonicalizeQuery("a \x00z\x01", Component(0, 5), NULL, &output, &out_comp);
  output.Complete();
  EXPECT_EQ("?a%20%00z%01", out_str);
}

}  // namespace

}  // namespace url
