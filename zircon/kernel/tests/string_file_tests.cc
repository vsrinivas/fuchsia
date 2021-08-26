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

bool AvailableUsedSpace() {
  BEGIN_TEST;

  auto Validate = [](const StringFile& file, const char* buffer, size_t capacity,
                     size_t written) -> bool {
    BEGIN_TEST;

    const size_t effective_capacity = (capacity > 0) ? capacity - 1 : 0;
    const size_t expected_used = ktl::min(written, effective_capacity);
    const size_t expected_avail = effective_capacity - expected_used;
    const char* const expected_used_base = (expected_used > 0) ? buffer : nullptr;
    const char* const expected_avail_base = (expected_avail > 0) ? buffer + expected_used : nullptr;

    ASSERT_EQ(expected_used, file.used_region().size());
    ASSERT_EQ(expected_avail, file.available_region().size());

    ktl::span<char> used_region = file.used_region();
    ASSERT_EQ(expected_used_base, used_region.data());
    ASSERT_EQ(expected_used, used_region.size());

    ktl::span<char> available_region = file.available_region();
    ASSERT_EQ(expected_avail_base, available_region.data());
    ASSERT_EQ(expected_avail, available_region.size());

    END_TEST;
  };

  // Empty files should always report no space remaining, and no space used.
  StringFile empty_file(ktl::span<char>{});
  ASSERT_TRUE(Validate(empty_file, nullptr, 0, 0));

  // Writing to the file with no buffer should not change anything.
  empty_file.Write(ktl::string_view("x"));
  ASSERT_TRUE(Validate(empty_file, nullptr, 0, 1));

  // Repeat the tests, but now with a file backed by a non-empty buffer.
  char buffer[4];
  StringFile sfile(ktl::span<char>{buffer, sizeof(buffer)});
  for (size_t i = 0; i < sizeof(buffer); ++i) {
    ASSERT_TRUE(Validate(sfile, buffer, sizeof(buffer), i));
    sfile.Write(ktl::string_view("x"));
    ASSERT_TRUE(Validate(sfile, buffer, sizeof(buffer), i + 1));
  }

  END_TEST;
}

bool Skip() {
  BEGIN_TEST;

  char buffer[10]{0};

  // Skip a part of the start of the file, but overwrite the end.
  {
    memset(buffer, 'x', ktl::size(buffer));
    StringFile sfile{ktl::span<char>(buffer, ktl::size(buffer))};

    sfile.Skip(3);
    sfile.Write("123456789abcde");

    constexpr const char* expected_str = "xxx123456";
    ktl::span<char> actual_str = std::move(sfile).take();
    ASSERT_EQ(ktl::size(buffer), actual_str.size());
    ASSERT_EQ(ktl::size(buffer), strlen(expected_str) + 1);
    ASSERT_BYTES_EQ(reinterpret_cast<const uint8_t*>(expected_str),
                    reinterpret_cast<const uint8_t*>(actual_str.data()), ktl::size(buffer));
  }

  // Skip some of the middle of a file.
  {
    memset(buffer, 'x', ktl::size(buffer));
    StringFile sfile{ktl::span<char>(buffer, ktl::size(buffer))};

    sfile.Write("123");
    sfile.Skip(3);
    sfile.Write("456789abcde");

    constexpr const char* expected_str = "123xxx456";
    ktl::span<char> actual_str = std::move(sfile).take();
    ASSERT_EQ(ktl::size(buffer), actual_str.size());
    ASSERT_EQ(ktl::size(buffer), strlen(expected_str) + 1);
    ASSERT_BYTES_EQ(reinterpret_cast<const uint8_t*>(expected_str),
                    reinterpret_cast<const uint8_t*>(actual_str.data()), ktl::size(buffer));
  }

  // Attempt to skip past the end of a file.
  {
    memset(buffer, 'x', ktl::size(buffer));
    StringFile sfile{ktl::span<char>(buffer, ktl::size(buffer))};

    sfile.Write("123456");
    sfile.Skip(30);
    sfile.Write("789abcde");

    constexpr const char* expected_str = "123456xxx";
    ktl::span<char> actual_str = std::move(sfile).take();
    ASSERT_EQ(ktl::size(buffer), actual_str.size());
    ASSERT_EQ(ktl::size(buffer), strlen(expected_str) + 1);
    ASSERT_BYTES_EQ(reinterpret_cast<const uint8_t*>(expected_str),
                    reinterpret_cast<const uint8_t*>(actual_str.data()), ktl::size(buffer));
  }

  END_TEST;
}

bool StringViewConversion() {
  BEGIN_TEST;

  auto Validate = [](const StringFile& file, const ktl::string_view expected) -> bool {
    BEGIN_TEST;

    ktl::string_view sv;

    // Test the |as_string_view| method.
    sv = file.as_string_view();
    ASSERT_EQ(expected.size(), sv.size());
    if (!expected.size()) {
      ASSERT_NULL(sv.data());
    } else {
      ASSERT_BYTES_EQ(reinterpret_cast<const uint8_t*>(expected.data()),
                      reinterpret_cast<const uint8_t*>(sv.data()), expected.size());
    }

    // Same test, but used the explicit conversion operator instead.
    sv = static_cast<ktl::string_view>(file);
    ASSERT_EQ(expected.size(), sv.size());
    if (!expected.size()) {
      ASSERT_NULL(sv.data());
    } else {
      ASSERT_BYTES_EQ(reinterpret_cast<const uint8_t*>(expected.data()),
                      reinterpret_cast<const uint8_t*>(sv.data()), expected.size());
    }

    END_TEST;
  };

  {
    // A file with no buffer should always yield an empty string view.
    StringFile empty_file(ktl::span<char>({}));
    ASSERT_TRUE(Validate(empty_file, {}));

    // Attempting to write to the file should not change this.
    empty_file.Write("12345");
    ASSERT_TRUE(Validate(empty_file, {}));
  }

  {
    constexpr ktl::string_view kPattern{"1234"};
    char buffer[10]{0};
    StringFile sfile({buffer, sizeof(buffer)});

    ASSERT_TRUE(Validate(sfile, {}));
    sfile.Write(kPattern);
    ASSERT_TRUE(Validate(sfile, {"1234"}));
    sfile.Write(kPattern);
    ASSERT_TRUE(Validate(sfile, {"12341234"}));
    sfile.Write(kPattern);
    ASSERT_TRUE(Validate(sfile, {"123412341"}));
    sfile.Write(kPattern);
    ASSERT_TRUE(Validate(sfile, {"123412341"}));
  }

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
UNITTEST("StringFile avail/used space", AvailableUsedSpace)
UNITTEST("StringFile::Skip", Skip)
UNITTEST("StringFile string_view conversion", StringViewConversion)
UNITTEST_END_TESTCASE(string_file_tests, "string_file", "StringFile tests")
