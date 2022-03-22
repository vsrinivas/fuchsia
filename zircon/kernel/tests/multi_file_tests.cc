// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>
#include <multi-file.h>
#include <string-file.h>

#include <ktl/array.h>
#include <ktl/string_view.h>

#include <ktl/enforce.h>

namespace {

template <size_t N>
bool ExpectEq(const ktl::array<char, N>& value, const char (&expected)[N]) {
  BEGIN_TEST;
  EXPECT_BYTES_EQ(reinterpret_cast<const uint8_t*>(value.data()),
                  reinterpret_cast<const uint8_t*>(expected), N);
  END_TEST;
}

bool ZeroLengthArray() {
  BEGIN_TEST;
  constexpr ktl::string_view kInput = "12345";

  MultiFile<0> mfile;
  EXPECT_EQ(mfile.Write(kInput), static_cast<int>(kInput.size()));

  END_TEST;
}

bool Nullptr() {
  BEGIN_TEST;
  constexpr ktl::string_view kInput = "12345";

  MultiFile<1> mfile;
  ASSERT_EQ(mfile.files()[0], nullptr);
  EXPECT_EQ(mfile.Write(kInput), static_cast<int>(kInput.size()));

  END_TEST;
}

bool TwoStrings() {
  BEGIN_TEST;
  constexpr ktl::string_view kInput = "12345";

  ktl::array<char, kInput.size() + 1> output_string_1{}, output_string_2{};
  StringFile sfile1(output_string_1);
  StringFile sfile2(output_string_2);
  MultiFile<2> mfile({&sfile1, &sfile2});

  EXPECT_EQ(mfile.Write(kInput), static_cast<int>(kInput.size()));

  EXPECT_TRUE(ExpectEq(output_string_1, "12345"));
  EXPECT_TRUE(ExpectEq(output_string_2, "12345"));

  END_TEST;
}

bool OneStringOneNullptr() {
  BEGIN_TEST;
  constexpr ktl::string_view kInput = "12345";

  ktl::array<char, kInput.size() + 1> output_string{};
  StringFile sfile(output_string);
  MultiFile<2> mfile({&sfile, nullptr});

  EXPECT_EQ(mfile.files()[1], nullptr);
  EXPECT_EQ(mfile.Write(kInput), static_cast<int>(kInput.size()));

  EXPECT_TRUE(ExpectEq(output_string, "12345"));

  END_TEST;
}

bool OneSuccessOneFail() {
  BEGIN_TEST;
  constexpr ktl::string_view kInput = "12345";

  ktl::array<char, kInput.size() + 1> output_string_1{};
  StringFile sfile1(output_string_1);
  StringFile sfile2(ktl::span<char>{});
  MultiFile<2> mfile({&sfile1, &sfile2});

  EXPECT_EQ(mfile.Write(kInput), static_cast<int>(kInput.size()));

  EXPECT_TRUE(ExpectEq(output_string_1, "12345"));

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(multi_file_tests)
UNITTEST("MultiFile::Write - on zero-length array", ZeroLengthArray)
UNITTEST("MultiFile::Write - on nullptr", Nullptr)
UNITTEST("MultiFile::Write - two strings", TwoStrings)
UNITTEST("MultiFile::Write - one string one nullptr", OneStringOneNullptr)
UNITTEST("MultiFile::Write - one success one fail", OneSuccessOneFail)
UNITTEST_END_TESTCASE(multi_file_tests, "multi_file", "MultiFile tests")
