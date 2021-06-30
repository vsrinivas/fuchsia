// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>
#include <string-file.h>

#include "ktl/array.h"
#include "ktl/string_view.h"

namespace {

constexpr char kVal = static_cast<char>(-1);

bool WriteOnEmptyBufferIsOk() {
  BEGIN_TEST;
  constexpr ktl::string_view kInput = "12345";

  StringFile sfile(ktl::span<char>{});
  EXPECT_EQ(sfile.Write(kInput), static_cast<int>(kInput.size()));

  END_TEST;
}

bool WriteEmptyStringViewIsOk() {
  BEGIN_TEST;
  ktl::array<char, 1> buffer = {};

  StringFile sfile(buffer);

  EXPECT_EQ(sfile.Write({}), 0);

  auto writen_view = std::move(sfile).take();
  ASSERT_FALSE(writen_view.empty());
  ASSERT_EQ(buffer.front(), '\0');

  END_TEST;
}

bool WriteStringViewThatFitsInBuffer() {
  BEGIN_TEST;
  constexpr ktl::string_view kInput = "12345";
  ktl::array<char, kInput.length() + 1> buffer = {};

  StringFile sfile(buffer);

  EXPECT_EQ(sfile.Write(kInput), static_cast<int>(kInput.size()));

  auto writen_view = std::move(sfile).take();

  EXPECT_TRUE(memcmp(writen_view.data(), kInput.data(), kInput.size()) == 0);
  ASSERT_TRUE(writen_view.data() == buffer.data());

  END_TEST;
}

bool WriteTruncateStringViewThatDoesntFitsInBuffer() {
  BEGIN_TEST;
  constexpr ktl::string_view kInput = "12345";
  // We would reserve last char for '\0'
  ktl::array<char, kInput.length()> buffer = {};

  StringFile sfile(buffer);

  EXPECT_EQ(sfile.Write(kInput), static_cast<int>(kInput.size()));

  auto writen_view = std::move(sfile).take();

  ASSERT_EQ(writen_view.size(), kInput.size());
  EXPECT_TRUE(memcmp(writen_view.data(), kInput.data(), writen_view.size() - 1) == 0);
  ASSERT_EQ(buffer.back(), '\0');
  ASSERT_TRUE(writen_view.data() == buffer.data());

  END_TEST;
}

bool WriteManyTimes() {
  BEGIN_TEST;
  constexpr ktl::string_view kInput = "12345";
  // We would reserve last char for '\0'
  ktl::array<char, kInput.length()> buffer = {kVal, kVal, kVal, kVal, kVal};

  StringFile sfile(buffer);

  EXPECT_EQ(sfile.Write(kInput.substr(0, 2)), 2);
  EXPECT_TRUE(memcmp(buffer.data(), kInput.data(), 2) == 0);

  EXPECT_EQ(sfile.Write(kInput.substr(2, 1)), 1);
  EXPECT_TRUE(memcmp(buffer.data() + 2, kInput.data() + 2, 1) == 0);

  EXPECT_EQ(sfile.Write(kInput.substr(3, 2)), 2);
  EXPECT_TRUE(memcmp(buffer.data() + 3, kInput.data() + 3, 1) == 0);

  ASSERT_EQ(buffer.back(), kVal);

  auto writen_view = std::move(sfile).take();

  ASSERT_EQ(writen_view.size(), kInput.size());
  EXPECT_TRUE(memcmp(writen_view.data(), kInput.data(), writen_view.size() - 1) == 0);
  ASSERT_EQ(buffer.back(), '\0');
  ASSERT_TRUE(writen_view.data() == buffer.data());

  END_TEST;
}

bool TakeAddsNullCharacter() {
  BEGIN_TEST;
  constexpr ktl::string_view kInput = "12345";
  // We would reserve last char for '\0'
  ktl::array<char, kInput.length()> buffer = {kVal, kVal, kVal, kVal, kVal};

  StringFile sfile(buffer);

  EXPECT_EQ(sfile.Write(kInput), static_cast<int>(kInput.size()));
  // Write should not have set the last character set yet.
  ASSERT_EQ(buffer.back(), kVal);

  auto writen_view = std::move(sfile).take();
  ASSERT_EQ(buffer.back(), '\0');

  ASSERT_EQ(writen_view.size(), kInput.size());
  EXPECT_TRUE(memcmp(writen_view.data(), kInput.data(), writen_view.size() - 1) == 0);
  ASSERT_TRUE(writen_view.data() == buffer.data());

  END_TEST;
}

bool TakeOnEmptyBufferIsEmpty() {
  BEGIN_TEST;

  StringFile sfile(ktl::span<char>{});
  auto writen_view = std::move(sfile).take();
  ASSERT_TRUE(writen_view.empty());

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(string_file_tests)
UNITTEST("StringFile::Write - With Empty Buffer", WriteOnEmptyBufferIsOk)
UNITTEST("StringFile::Write - With Empty Input", WriteEmptyStringViewIsOk)
UNITTEST("StringFile::Write - Input fits in buffer", WriteStringViewThatFitsInBuffer)
UNITTEST("StringFile::Write - Input does not fit in buffer",
         WriteTruncateStringViewThatDoesntFitsInBuffer)
UNITTEST("StringFile::Write - Multiple Calls are correct", WriteManyTimes)
UNITTEST("StringFile::take - Adds Null Character", TakeAddsNullCharacter)
UNITTEST("StringFile::take - Empty Buffer", TakeOnEmptyBufferIsEmpty)
UNITTEST_END_TESTCASE(string_file_tests, "string_file", "StringFile tests")
