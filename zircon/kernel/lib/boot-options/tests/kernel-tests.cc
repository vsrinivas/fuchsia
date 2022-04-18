// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-options/boot-options.h>
#include <lib/unittest/unittest.h>
#include <string-file.h>

#include <fbl/alloc_checker.h>
#include <ktl/algorithm.h>
#include <ktl/array.h>
#include <ktl/string_view.h>
#include <ktl/unique_ptr.h>

#include <ktl/enforce.h>

// We exercise basic boot option functionality here, with an aim toward
// covering the (libc/ktl) behavior that would be sufficiently different in the
// phys and kernel environments. More generic and involved tests are left to
// userland.

namespace {

constexpr size_t kFileSizeMax = 64;

// Can be wrapped by FILE to give a file pointer whose contents may be
// trivially accessed.
class CapturedFile : public StringFile {
 public:
  CapturedFile() : StringFile(buffer_) {}

  bool ContentsMatchExactly(ktl::string_view expected) const {
    auto actual = used_region();
    if (actual.size() != expected.size()) {
      return false;
    }
    return ktl::string_view(actual.data(), actual.size()) == expected;
  }

  bool IsEmpty() const { return used_region().empty(); }

 private:
  ktl::array<char, kFileSizeMax> buffer_ = {};
};

#ifdef _KERNEL

using BootOptionsPtr = ktl::unique_ptr<BootOptions>;

#else  // !_KERNEL

using BootOptionsPtr = BootOptions*;

#endif

// Provides an instance of boot options->for a test to use.
BootOptionsPtr MakeBootOptions() {
#ifdef _KERNEL

  fbl::AllocChecker ac;
  auto options = ktl::make_unique<BootOptions>(&ac);
  if (!ac.check()) {
    options = nullptr;
  }
  return options;

#else  // !_KERNEL

  static BootOptions options;
  options = {};
  return &options;

#endif
}

bool ParseBool() {
  BEGIN_TEST;

  // Default value.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    EXPECT_FALSE(options->test_bool);
  }

  // true.
  {
    CapturedFile file;
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_bool = false;
    options->SetMany("test.option.bool=true", &file);
    EXPECT_TRUE(options->test_bool);
    EXPECT_TRUE(file.IsEmpty());
  }

  // false.
  {
    CapturedFile file;
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_bool = true;
    options->SetMany("test.option.bool=false", &file);
    EXPECT_FALSE(options->test_bool);
    EXPECT_TRUE(file.IsEmpty());
  }

  {  // "0" should be falsey.
    CapturedFile file;
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_bool = true;
    options->SetMany("test.option.bool=0", &file);
    EXPECT_FALSE(options->test_bool);
    EXPECT_TRUE(file.IsEmpty());
  }

  {  // "off" should be falsey.
    CapturedFile file;
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_bool = true;
    options->SetMany("test.option.bool=off", &file);
    EXPECT_FALSE(options->test_bool);
    EXPECT_TRUE(file.IsEmpty());
  }

  END_TEST;
}

bool UnparseBool() {
  BEGIN_TEST;

  // true.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_bool = true;

    constexpr ktl::string_view kExpected = "test.option.bool=true\n";
    CapturedFile file;
    ASSERT_EQ(0, options->Show("test.option.bool", false, &file));
    ASSERT_TRUE(file.ContentsMatchExactly(kExpected));
  }

  // false.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_bool = false;

    constexpr ktl::string_view kExpected = "test.option.bool=false\n";
    CapturedFile file;
    ASSERT_EQ(0, options->Show("test.option.bool", false, &file));
    ASSERT_TRUE(file.ContentsMatchExactly(kExpected));
  }

  END_TEST;
}

bool ParseUint32() {
  BEGIN_TEST;

  // Default value
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    EXPECT_EQ(123u, options->test_uint32);
  }

  // 321.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    CapturedFile file;
    options->test_uint32 = 0u;
    options->SetMany("test.option.uint32=321", &file);
    EXPECT_EQ(321u, options->test_uint32);
    EXPECT_TRUE(file.IsEmpty());
  }

  // 0x123: hex notation is kosher.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    CapturedFile file;
    options->test_uint32 = 0u;
    options->SetMany("test.option.uint32=0x123", &file);
    EXPECT_EQ(0x123u, options->test_uint32);
    EXPECT_TRUE(file.IsEmpty());
  };

  // -123.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    CapturedFile file;
    options->test_uint32 = 0u;
    options->SetMany("test.option.uint32=-123", &file);
    EXPECT_EQ(~uint32_t{123u} + 1, options->test_uint32);
    EXPECT_TRUE(file.IsEmpty());
  };

  // Unparsable values are reset to default.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    CapturedFile file;
    options->test_uint32 = 456;
    options->SetMany("test.option.uint32=not-a-uint32", &file);
    EXPECT_EQ(123u, options->test_uint32);
    EXPECT_TRUE(file.IsEmpty());
  };

  // Bits after 32 are truncated.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    CapturedFile file;
    options->test_uint32 = 0u;
    options->SetMany("test.option.uint32=0x987654321", &file);
    EXPECT_EQ(uint32_t{0x87654321}, options->test_uint32);
    EXPECT_TRUE(file.IsEmpty());
  };

  END_TEST;
}

bool UnparseUint32() {
  BEGIN_TEST;

  // 123.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_uint32 = 123u;

    constexpr ktl::string_view kExpected = "test.option.uint32=0x7b\n";
    CapturedFile file;
    ASSERT_EQ(0, options->Show("test.option.uint32", false, &file));
    ASSERT_TRUE(file.ContentsMatchExactly(kExpected));
  }

  // 0x123.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_uint32 = 0x123u;

    constexpr ktl::string_view kExpected = "test.option.uint32=0x123\n";
    CapturedFile file;
    ASSERT_EQ(0, options->Show("test.option.uint32", false, &file));
    ASSERT_TRUE(file.ContentsMatchExactly(kExpected));
  }

  // -123.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_uint32 = ~uint32_t{123u} + 1;

    constexpr ktl::string_view kExpected = "test.option.uint32=0xffffff85\n";
    CapturedFile file;
    ASSERT_EQ(0, options->Show("test.option.uint32", false, &file));
    ASSERT_TRUE(file.ContentsMatchExactly(kExpected));
  }

  END_TEST;
}

bool ParseUint64() {
  BEGIN_TEST;

  // Default value.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    EXPECT_EQ(456u, options->test_uint64);
  }

  // 654.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    CapturedFile file;
    options->test_uint64 = 0u;
    options->SetMany("test.option.uint64=654", &file);
    EXPECT_EQ(654u, options->test_uint64);
    EXPECT_TRUE(file.IsEmpty());
  }

  // 0x456: hex notation is kosher.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    CapturedFile file;
    options->test_uint64 = 0u;
    options->SetMany("test.option.uint64=0x456", &file);
    EXPECT_EQ(0x456u, options->test_uint64);
    EXPECT_TRUE(file.IsEmpty());
  };

  // -456.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    CapturedFile file;
    options->test_uint64 = 0u;
    options->SetMany("test.option.uint64=-456", &file);
    EXPECT_EQ(~uint64_t{456u} + 1, options->test_uint64);
    EXPECT_TRUE(file.IsEmpty());
  };

  // Unparsable values reset value to default.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    CapturedFile file;
    options->test_uint64 = 1234;
    options->SetMany("test.option.uint64=not-a-uint64", &file);
    EXPECT_EQ(456u, options->test_uint64);
    EXPECT_TRUE(file.IsEmpty());
  };

  // Bits after 64 are truncated.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    CapturedFile file;
    options->test_uint64 = 0u;
    options->SetMany("test.option.uint64=0x87654321012345678", &file);
    EXPECT_EQ(uint64_t{0x7654321012345678}, options->test_uint64);
    EXPECT_TRUE(file.IsEmpty());
  };

  END_TEST;
}

bool UnparseUint64() {
  BEGIN_TEST;

  // 456u.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_uint32 = 456u;

    constexpr ktl::string_view kExpected = "test.option.uint64=0x1c8\n";
    CapturedFile file;
    ASSERT_EQ(0, options->Show("test.option.uint64", false, &file));
    ASSERT_TRUE(file.ContentsMatchExactly(kExpected));
  }

  // 0x456u.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_uint64 = 0x456u;

    constexpr ktl::string_view kExpected = "test.option.uint64=0x456\n";
    CapturedFile file;
    ASSERT_EQ(0, options->Show("test.option.uint64", false, &file));
    ASSERT_TRUE(file.ContentsMatchExactly(kExpected));
  }

  // -456u.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_uint64 = ~uint64_t{456u} + 1;

    constexpr ktl::string_view kExpected = "test.option.uint64=0xfffffffffffffe38\n";
    CapturedFile file;
    ASSERT_EQ(0, options->Show("test.option.uint64", false, &file));

    ASSERT_TRUE(file.ContentsMatchExactly(kExpected));
  }

  END_TEST;
}

bool ParseSmallString() {
  BEGIN_TEST;

  // Default value.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    constexpr SmallString kDefault = {'t', 'e', 's', 't', '-', 'd', 'e', 'f', 'a', 'u',
                                      'l', 't', '-', 'v', 'a', 'l', 'u', 'e', '\0'};
    ASSERT_EQ(options->test_smallstring.data()[options->test_smallstring.size() - 1], '\0');
    EXPECT_EQ(0, strcmp(kDefault.data(), options->test_smallstring.data()));
  }

  // new-value.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    constexpr SmallString kNew = {'n', 'e', 'w', '-', 'v', 'a', 'l', 'u', 'e', '\0'};
    CapturedFile file;
    options->test_smallstring = {};
    options->SetMany("test.option.smallstring=new-value", &file);
    ASSERT_EQ(options->test_smallstring.data()[options->test_smallstring.size() - 1], '\0');
    EXPECT_EQ(0, strcmp(kNew.data(), options->test_smallstring.data()));
    EXPECT_TRUE(file.IsEmpty());
  }

  {  // Multi-world values are not permitted.
    constexpr SmallString kFirst = {'f', 'i', 'r', 's', 't', '\0'};
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    CapturedFile file;
    options->test_smallstring = {};
    options->SetMany("test.option.smallstring=first second", &file);
    ASSERT_EQ(options->test_smallstring.data()[options->test_smallstring.size() - 1], '\0');
    EXPECT_EQ(0, strcmp(kFirst.data(), options->test_smallstring.data()));
    EXPECT_FALSE(file.IsEmpty());  // File your complaints here.
  }

  {  // Too big.

    // clang-format off
    constexpr SmallString kSevenAlphabetsTruncated = {
      'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
      'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
      'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
      'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
      'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
      'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
      'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
      'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
      'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
      'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
      'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
      'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
      'a', 'b', 'c', '\0',
    };

    // clang-format on
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    CapturedFile file;
    options->test_smallstring = {};
    options->SetMany(
        "test.option.smallstring="  // Seven alphabets.
        "abcdefghijklmnopqrstuvwxyz"
        "abcdefghijklmnopqrstuvwxyz"
        "abcdefghijklmnopqrstuvwxyz"
        "abcdefghijklmnopqrstuvwxyz"
        "abcdefghijklmnopqrstuvwxyz"
        "abcdefghijklmnopqrstuvwxyz"
        "abcdefghijklmnopqrstuvwxyz",
        &file);
    ASSERT_EQ(options->test_smallstring.data()[options->test_smallstring.size() - 1], '\0');
    EXPECT_EQ(0, strcmp(kSevenAlphabetsTruncated.data(), options->test_smallstring.data()));
    EXPECT_TRUE(file.IsEmpty());
  }

  END_TEST;
}

bool UnparseSmallString() {
  BEGIN_TEST;

  // new-value.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_smallstring = {'n', 'e', 'w', '-', 'v', 'a', 'l', 'u', 'e', '\0'};

    constexpr ktl::string_view kExpected = "test.option.smallstring=new-value\n";
    CapturedFile file;
    ASSERT_EQ(0, options->Show("test.option.smallstring", false, &file));
    ASSERT_TRUE(file.ContentsMatchExactly(kExpected));
  }

  END_TEST;
}

bool ParseEnum() {
  BEGIN_TEST;

  // Default value.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    EXPECT_EQ(TestEnum::kDefault, options->test_enum);
  }

  // kValue1.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    CapturedFile file;
    options->test_enum = TestEnum::kDefault;
    options->SetMany("test.option.enum=value1", &file);
    EXPECT_EQ(TestEnum::kValue1, options->test_enum);
    EXPECT_TRUE(file.IsEmpty());
  }

  // kValue2.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    CapturedFile file;
    options->test_enum = TestEnum::kDefault;
    options->SetMany("test.option.enum=value2", &file);
    EXPECT_EQ(TestEnum::kValue2, options->test_enum);
    EXPECT_TRUE(file.IsEmpty());
  }

  // Unparsable values reset value to default.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    CapturedFile file;
    options->test_enum = TestEnum::kValue2;
    options->SetMany("test.option.enum=unknown", &file);
    EXPECT_EQ(TestEnum::kDefault, options->test_enum);
    EXPECT_TRUE(file.IsEmpty());
  }

  END_TEST;
}

bool UnparseEnum() {
  BEGIN_TEST;

  // kDefault.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_enum = TestEnum::kDefault;

    constexpr ktl::string_view kExpected = "test.option.enum=default\n";
    CapturedFile file;
    ASSERT_EQ(0, options->Show("test.option.enum", false, &file));
    ASSERT_TRUE(file.ContentsMatchExactly(kExpected));
    ;
  }

  // kValue1.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_enum = TestEnum::kValue1;

    constexpr ktl::string_view kExpected = "test.option.enum=value1\n";
    CapturedFile file;
    ASSERT_EQ(0, options->Show("test.option.enum", false, &file));
    ASSERT_TRUE(file.ContentsMatchExactly(kExpected));
  }

  // kValue2.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_enum = TestEnum::kValue2;

    constexpr ktl::string_view kExpected = "test.option.enum=value2\n";
    CapturedFile file;
    ASSERT_EQ(0, options->Show("test.option.enum", false, &file));
    ASSERT_TRUE(file.ContentsMatchExactly(kExpected));
  }

  END_TEST;
}

bool ParseStruct() {
  BEGIN_TEST;

  // Default value.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    EXPECT_TRUE(TestStruct{} == options->test_struct);
  }

  // Basic value.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    CapturedFile file;

    options->test_struct = TestStruct{};
    options->SetMany("test.option.struct=test", &file);
    EXPECT_TRUE(TestStruct{.present = true} == options->test_struct);
    EXPECT_TRUE(file.IsEmpty());
  }

  // Unparsable values reset value to default.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    CapturedFile file;

    options->test_struct = TestStruct{.present = true};
    options->SetMany("test.option.struct=unparsable", &file);
    EXPECT_TRUE(TestStruct{} == options->test_struct);  // No change.
    EXPECT_TRUE(file.IsEmpty());
  }

  END_TEST;
}

bool UnparseStruct() {
  BEGIN_TEST;

  // Empty value.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_struct = {};

    constexpr ktl::string_view kExpected = "test.option.struct=test\n";
    CapturedFile file;
    ASSERT_EQ(0, options->Show("test.option.struct", false, &file));
    ASSERT_TRUE(file.ContentsMatchExactly(kExpected));
    ;
  }

  // Basic value.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_struct = {.present = true};

    constexpr ktl::string_view kExpected = "test.option.struct=test\n";
    CapturedFile file;
    ASSERT_EQ(0, options->Show("test.option.struct", false, &file));
    ASSERT_TRUE(file.ContentsMatchExactly(kExpected));
    ;
  }

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(boot_option_tests)
UNITTEST("bool parsing", ParseBool)
UNITTEST("bool unparsing", UnparseBool)
UNITTEST("uint32 parsing", ParseUint32)
UNITTEST("uint32 unparsing", UnparseUint32)
UNITTEST("uint64 parsing", ParseUint64)
UNITTEST("uint64 unparsing", UnparseUint64)
UNITTEST("smallstring parsing", ParseSmallString)
UNITTEST("smallstring unparsing", UnparseSmallString)
UNITTEST("enum parsing", ParseEnum)
UNITTEST("enum unparsing", UnparseEnum)
UNITTEST("struct parsing", ParseStruct)
UNITTEST("struct unparsing", UnparseStruct)
UNITTEST_END_TESTCASE(boot_option_tests, "boot-options", "Tests of boot options library")
