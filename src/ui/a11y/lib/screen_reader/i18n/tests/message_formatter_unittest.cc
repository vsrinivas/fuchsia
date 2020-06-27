// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/i18n/message_formatter.h"

#include <lib/gtest/test_loop_fixture.h>

#include <memory>

#include "src/lib/intl/lookup/cpp/lookup.h"
#include "third_party/icu/source/common/unicode/ucnv.h"
#include "third_party/icu/source/i18n/unicode/msgfmt.h"

namespace accessibility_test {
namespace {

// The tests bellow use intl::Lookup::NewForTest method for building a fake Lookup to be used in
// each test case. Check its documentation for a full list, but important to know here:
// - Message IDs == 1 the message will be a icu::MessageFormat pattern with a named argument.
// - Message IDs == even number the message will be a icu::MessageFormat pattern with no arguments.
// - Message IDs == odd number the message will not be found.

class MessageFormatterTest : public gtest::TestLoopFixture {
 public:
  MessageFormatterTest() {}

  ~MessageFormatterTest() = default;
};

TEST_F(MessageFormatterTest, MessageIDDoesNotExist) {
  auto lookup_or_error = intl::Lookup::NewForTest({"foo-Bar"});
  ASSERT_FALSE(lookup_or_error.is_error());
  auto formatter = std::make_unique<a11y::i18n::MessageFormatter>(icu::Locale("pt"),
                                                                  lookup_or_error.take_value());
  auto result = formatter->FormatStringById(3);
  EXPECT_FALSE(result);
}

TEST_F(MessageFormatterTest, FormatsMessageWithNamedArgument) {
  auto lookup_or_error = intl::Lookup::NewForTest({"foo-Bar"});
  ASSERT_FALSE(lookup_or_error.is_error());
  auto formatter = std::make_unique<a11y::i18n::MessageFormatter>(icu::Locale("pt"),
                                                                  lookup_or_error.take_value());
  auto result = formatter->FormatStringById(1, {"person"}, {"Goku"});
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value(), "Hello Goku!");
}

TEST_F(MessageFormatterTest, InvalidArgumentName) {
  auto lookup_or_error = intl::Lookup::NewForTest({"foo-Bar"});
  ASSERT_FALSE(lookup_or_error.is_error());
  auto formatter = std::make_unique<a11y::i18n::MessageFormatter>(icu::Locale("pt"),
                                                                  lookup_or_error.take_value());
  auto result = formatter->FormatStringById(1, {"age"}, {"42"});
  ASSERT_FALSE(result);
}

TEST_F(MessageFormatterTest, MoreArgumentsThanPattern) {
  auto lookup_or_error = intl::Lookup::NewForTest({"foo-Bar"});
  ASSERT_FALSE(lookup_or_error.is_error());
  auto formatter = std::make_unique<a11y::i18n::MessageFormatter>(icu::Locale("pt"),
                                                                  lookup_or_error.take_value());
  auto result = formatter->FormatStringById(1, {"person", "age"}, {"Goku", "42"});
  ASSERT_FALSE(result);
}

TEST_F(MessageFormatterTest, DifferentNumberOfArgumentValuesAndArgumentNames) {
  auto lookup_or_error = intl::Lookup::NewForTest({"foo-Bar"});
  ASSERT_FALSE(lookup_or_error.is_error());
  auto formatter = std::make_unique<a11y::i18n::MessageFormatter>(icu::Locale("pt"),
                                                                  lookup_or_error.take_value());
  auto result = formatter->FormatStringById(1, {"person"}, {"Goku", "42"});
  ASSERT_FALSE(result);
}

TEST_F(MessageFormatterTest, FormatsMessageWithNoArgument) {
  auto lookup_or_error = intl::Lookup::NewForTest({"foo-Bar"});
  ASSERT_FALSE(lookup_or_error.is_error());
  auto formatter = std::make_unique<a11y::i18n::MessageFormatter>(icu::Locale("pt-BR"),
                                                                  lookup_or_error.take_value());
  auto result = formatter->FormatStringById(2);
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value(), "Hello world!");
}

}  // namespace
}  // namespace accessibility_test
