// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-options/boot-options.h>
#include <lib/unittest/unittest.h>

#include <fbl/alloc_checker.h>
#include <ktl/algorithm.h>
#include <ktl/string_view.h>
#include <ktl/unique_ptr.h>

// We exercise basic boot option functionality here, with an aim toward
// covering the (libc/ktl) behavior that would be sufficiently different in the
// phys and kernel environments. More generic and involved tests are left to
// userland.

namespace {

constexpr size_t kFileSizeMax = 64;

// Can be wrapped by FILE to give a file pointer whose contents may be
// trivially accessed.
class DummyFile {
 public:
  int Write(ktl::string_view s) {
    size_t wrote = s.copy(buffer_ + write_, ktl::min(s.size(), kFileSizeMax - write_));
    write_ += wrote;
    if (wrote < s.size()) {
      s.remove_prefix(wrote);
      printf("failed to write \"%.*s\" to file\n", static_cast<int>(s.size()), s.data());
    }
    return static_cast<int>(wrote);
  }

  ktl::string_view contents() { return {buffer_, write_}; }

 private:
  char buffer_[kFileSizeMax];
  size_t write_ = 0;
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
    DummyFile dummy;
    FILE file{&dummy};
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_bool = false;
    options->SetMany("test.option.bool=true", &file);
    EXPECT_TRUE(options->test_bool);
    EXPECT_EQ(0u, dummy.contents().size());
  }

  // false.
  {
    DummyFile dummy;
    FILE file{&dummy};
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_bool = true;
    options->SetMany("test.option.bool=false", &file);
    EXPECT_FALSE(options->test_bool);
    EXPECT_EQ(0u, dummy.contents().size());
  }

  {  // "0" should be falsey.
    DummyFile dummy;
    FILE file{&dummy};
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_bool = true;
    options->SetMany("test.option.bool=0", &file);
    EXPECT_FALSE(options->test_bool);
    EXPECT_EQ(0u, dummy.contents().size());
  }

  {  // "off" should be falsey.
    DummyFile dummy;
    FILE file{&dummy};
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_bool = true;
    options->SetMany("test.option.bool=off", &file);
    EXPECT_FALSE(options->test_bool);
    EXPECT_EQ(0u, dummy.contents().size());
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
    DummyFile dummy;
    FILE file{&dummy};
    ASSERT_EQ(0, options->Show("test.option.bool", false, &file));
    ASSERT_EQ(kExpected.size(), dummy.contents().size());
    EXPECT_EQ(0, memcmp(kExpected.data(), dummy.contents().data(), kExpected.size()));
  }

  // false.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_bool = false;

    constexpr ktl::string_view kExpected = "test.option.bool=false\n";
    DummyFile dummy;
    FILE file{&dummy};
    ASSERT_EQ(0, options->Show("test.option.bool", false, &file));
    ASSERT_EQ(kExpected.size(), dummy.contents().size());
    EXPECT_EQ(0, memcmp(kExpected.data(), dummy.contents().data(), kExpected.size()));
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
    DummyFile dummy;
    FILE file{&dummy};
    options->test_uint32 = 0u;
    options->SetMany("test.option.uint32=321", &file);
    EXPECT_EQ(321u, options->test_uint32);
    EXPECT_EQ(0u, dummy.contents().size());
  }

  // 0x123: hex notation is kosher.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    DummyFile dummy;
    FILE file{&dummy};
    options->test_uint32 = 0u;
    options->SetMany("test.option.uint32=0x123", &file);
    EXPECT_EQ(0x123u, options->test_uint32);
    EXPECT_EQ(0u, dummy.contents().size());
  };

  // -123.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    DummyFile dummy;
    FILE file{&dummy};
    options->test_uint32 = 0u;
    options->SetMany("test.option.uint32=-123", &file);
    EXPECT_EQ(~uint32_t{123u} + 1, options->test_uint32);
    EXPECT_EQ(0u, dummy.contents().size());
  };

  // Unparsable values are reset to default.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    DummyFile dummy;
    FILE file{&dummy};
    options->test_uint32 = 456;
    options->SetMany("test.option.uint32=not-a-uint32", &file);
    EXPECT_EQ(123u, options->test_uint32);
    EXPECT_EQ(0u, dummy.contents().size());
  };

  // Bits after 32 are truncated.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    DummyFile dummy;
    FILE file{&dummy};
    options->test_uint32 = 0u;
    options->SetMany("test.option.uint32=0x987654321", &file);
    EXPECT_EQ(uint32_t{0x87654321}, options->test_uint32);
    EXPECT_EQ(0u, dummy.contents().size());
  };

  END_TEST;
}

bool UnparseUint32() {
  BEGIN_TEST;

  // 123.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    ;
    options->test_uint32 = 123u;

    constexpr ktl::string_view kExpected = "test.option.uint32=0x7b\n";
    DummyFile dummy;
    FILE file{&dummy};
    ASSERT_EQ(0, options->Show("test.option.uint32", false, &file));
    ASSERT_EQ(kExpected.size(), dummy.contents().size());
    EXPECT_EQ(0, memcmp(kExpected.data(), dummy.contents().data(), kExpected.size()));
  }

  // 0x123.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_uint32 = 0x123u;

    constexpr ktl::string_view kExpected = "test.option.uint32=0x123\n";
    DummyFile dummy;
    FILE file{&dummy};
    ASSERT_EQ(0, options->Show("test.option.uint32", false, &file));
    ASSERT_EQ(kExpected.size(), dummy.contents().size());
    EXPECT_EQ(0, memcmp(kExpected.data(), dummy.contents().data(), kExpected.size()));
  }

  // -123.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_uint32 = ~uint32_t{123u} + 1;

    constexpr ktl::string_view kExpected = "test.option.uint32=0xffffff85\n";
    DummyFile dummy;
    FILE file{&dummy};
    ASSERT_EQ(0, options->Show("test.option.uint32", false, &file));
    ASSERT_EQ(kExpected.size(), dummy.contents().size());
    EXPECT_EQ(0, memcmp(kExpected.data(), dummy.contents().data(), kExpected.size()));
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
    DummyFile dummy;
    FILE file{&dummy};
    options->test_uint64 = 0u;
    options->SetMany("test.option.uint64=654", &file);
    EXPECT_EQ(654u, options->test_uint64);
    EXPECT_EQ(0u, dummy.contents().size());
  }

  // 0x456: hex notation is kosher.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    DummyFile dummy;
    FILE file{&dummy};
    options->test_uint64 = 0u;
    options->SetMany("test.option.uint64=0x456", &file);
    EXPECT_EQ(0x456u, options->test_uint64);
    EXPECT_EQ(0u, dummy.contents().size());
  };

  // -456.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    DummyFile dummy;
    FILE file{&dummy};
    options->test_uint64 = 0u;
    options->SetMany("test.option.uint64=-456", &file);
    EXPECT_EQ(~uint64_t{456u} + 1, options->test_uint64);
    EXPECT_EQ(0u, dummy.contents().size());
  };

  // Unparsable values reset value to default.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    DummyFile dummy;
    FILE file{&dummy};
    options->test_uint64 = 1234;
    options->SetMany("test.option.uint64=not-a-uint64", &file);
    EXPECT_EQ(456u, options->test_uint64);
    EXPECT_EQ(0u, dummy.contents().size());
  };

  // Bits after 64 are truncated.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    DummyFile dummy;
    FILE file{&dummy};
    options->test_uint64 = 0u;
    options->SetMany("test.option.uint64=0x87654321012345678", &file);
    EXPECT_EQ(uint64_t{0x7654321012345678}, options->test_uint64);
    EXPECT_EQ(0u, dummy.contents().size());
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
    DummyFile dummy;
    FILE file{&dummy};
    ASSERT_EQ(0, options->Show("test.option.uint64", false, &file));
    ASSERT_EQ(kExpected.size(), dummy.contents().size());
    EXPECT_EQ(0, memcmp(kExpected.data(), dummy.contents().data(), kExpected.size()));
  }

  // 0x456u.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_uint64 = 0x456u;

    constexpr ktl::string_view kExpected = "test.option.uint64=0x456\n";
    DummyFile dummy;
    FILE file{&dummy};
    ASSERT_EQ(0, options->Show("test.option.uint64", false, &file));
    ASSERT_EQ(kExpected.size(), dummy.contents().size());
    EXPECT_EQ(0, memcmp(kExpected.data(), dummy.contents().data(), kExpected.size()));
  }

  // -456u.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_uint64 = ~uint64_t{456u} + 1;

    constexpr ktl::string_view kExpected = "test.option.uint64=0xfffffffffffffe38\n";
    DummyFile dummy;
    FILE file{&dummy};
    ASSERT_EQ(0, options->Show("test.option.uint64", false, &file));

    ASSERT_EQ(kExpected.size(), dummy.contents().size());
    EXPECT_EQ(0, memcmp(kExpected.data(), dummy.contents().data(), kExpected.size()));
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
    DummyFile dummy;
    FILE file{&dummy};
    options->test_smallstring = {};
    options->SetMany("test.option.smallstring=new-value", &file);
    ASSERT_EQ(options->test_smallstring.data()[options->test_smallstring.size() - 1], '\0');
    EXPECT_EQ(0, strcmp(kNew.data(), options->test_smallstring.data()));
    EXPECT_EQ(0u, dummy.contents().size());
  }

  {  // Multi-world values are not permitted.
    constexpr SmallString kFirst = {'f', 'i', 'r', 's', 't', '\0'};
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    DummyFile dummy;
    FILE file{&dummy};
    options->test_smallstring = {};
    options->SetMany("test.option.smallstring=first second", &file);
    ASSERT_EQ(options->test_smallstring.data()[options->test_smallstring.size() - 1], '\0');
    EXPECT_EQ(0, strcmp(kFirst.data(), options->test_smallstring.data()));
    EXPECT_GT(dummy.contents().size(), 0u);  // File your complaints here.
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
    DummyFile dummy;
    FILE file{&dummy};
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
    EXPECT_EQ(0u, dummy.contents().size());  // Silently truncate.
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
    DummyFile dummy;
    FILE file{&dummy};
    ASSERT_EQ(0, options->Show("test.option.smallstring", false, &file));
    ASSERT_EQ(kExpected.size(), dummy.contents().size());
    EXPECT_EQ(0, memcmp(kExpected.data(), dummy.contents().data(), kExpected.size()));
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
    DummyFile dummy;
    FILE file{&dummy};
    options->test_enum = TestEnum::kDefault;
    options->SetMany("test.option.enum=value1", &file);
    EXPECT_EQ(TestEnum::kValue1, options->test_enum);
    EXPECT_EQ(0u, dummy.contents().size());
  }

  // kValue2.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    DummyFile dummy;
    FILE file{&dummy};
    options->test_enum = TestEnum::kDefault;
    options->SetMany("test.option.enum=value2", &file);
    EXPECT_EQ(TestEnum::kValue2, options->test_enum);
    EXPECT_EQ(0u, dummy.contents().size());
  }

  // Unparsable values reset value to default.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    DummyFile dummy;
    FILE file{&dummy};
    options->test_enum = TestEnum::kValue2;
    options->SetMany("test.option.enum=unknown", &file);
    EXPECT_EQ(TestEnum::kDefault, options->test_enum);
    EXPECT_EQ(0u, dummy.contents().size());
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
    DummyFile dummy;
    FILE file{&dummy};
    ASSERT_EQ(0, options->Show("test.option.enum", false, &file));
    ASSERT_EQ(kExpected.size(), dummy.contents().size());
    EXPECT_EQ(0, memcmp(kExpected.data(), dummy.contents().data(), kExpected.size()));
  }

  // kValue1.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_enum = TestEnum::kValue1;

    constexpr ktl::string_view kExpected = "test.option.enum=value1\n";
    DummyFile dummy;
    FILE file{&dummy};
    ASSERT_EQ(0, options->Show("test.option.enum", false, &file));
    ASSERT_EQ(kExpected.size(), dummy.contents().size());
    EXPECT_EQ(0, memcmp(kExpected.data(), dummy.contents().data(), kExpected.size()));
  }

  // kValue2.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_enum = TestEnum::kValue2;

    constexpr ktl::string_view kExpected = "test.option.enum=value2\n";
    DummyFile dummy;
    FILE file{&dummy};
    ASSERT_EQ(0, options->Show("test.option.enum", false, &file));
    ASSERT_EQ(kExpected.size(), dummy.contents().size());
    EXPECT_EQ(0, memcmp(kExpected.data(), dummy.contents().data(), kExpected.size()));
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
    ASSERT_TRUE(options) DummyFile dummy;
    FILE file{&dummy};

    options->test_struct = TestStruct{};
    options->SetMany("test.option.struct=test", &file);
    EXPECT_TRUE(TestStruct{.present = true} == options->test_struct);
    EXPECT_EQ(0u, dummy.contents().size());
  }

  // Unparsable values reset value to default.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    DummyFile dummy;
    FILE file{&dummy};

    options->test_struct = TestStruct{.present = true};
    options->SetMany("test.option.struct=unparsable", &file);
    EXPECT_TRUE(TestStruct{} == options->test_struct);  // No change.
    EXPECT_EQ(0u, dummy.contents().size());
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
    DummyFile dummy;
    FILE file{&dummy};
    ASSERT_EQ(0, options->Show("test.option.struct", false, &file));
    ASSERT_EQ(kExpected.size(), dummy.contents().size());
    EXPECT_EQ(0, memcmp(kExpected.data(), dummy.contents().data(), kExpected.size()));
  }

  // Basic value.
  {
    auto options = MakeBootOptions();
    ASSERT_TRUE(options);
    options->test_struct = {.present = true};

    constexpr ktl::string_view kExpected = "test.option.struct=test\n";
    DummyFile dummy;
    FILE file{&dummy};
    ASSERT_EQ(0, options->Show("test.option.struct", false, &file));
    ASSERT_EQ(kExpected.size(), dummy.contents().size());
    EXPECT_EQ(0, memcmp(kExpected.data(), dummy.contents().data(), kExpected.size()));
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
