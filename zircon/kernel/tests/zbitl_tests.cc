// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>
#include <lib/zbitl/view.h>

#include <ktl/span.h>

#include "tests.h"

// zbitl is primarily tested by its host/userland unit tests.
// This serves to test some basic cases in the kernel and phys
// environments specifically, mostly just to make sure it compiles.

#define ASSERT_IS_OK(result) ASSERT_TRUE(result.is_ok())

namespace {

// `zbi --output=$OUTPUT_ZBI; hexdump -v -e '1/1 "\\x%02x"' $OUTPUT_ZBI`.
alignas(ZBI_ALIGNMENT) constexpr char kEmptyZbi[] =
    "\x42\x4f\x4f\x54\x00\x00\x00\x00\xe6\xf7\x8c\x86\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x29\x17\x78\xb5\xd6\xe8\x87\x4a";

// ```
// zbi --output=$OUTPUT_ZBI --type CMDLINE --entry "hello world"
// hexdump -v -e '1/1 "\\x%02x"' $OUTPUT_ZBI
// ```
alignas(ZBI_ALIGNMENT) constexpr char kSimpleZbi[] =
    "\x42\x4f\x4f\x54\x30\x00\x00\x00\xe6\xf7\x8c\x86\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x29\x17\x78\xb5\xd6\xe8\x87\x4a\x43\x4d\x44\x4c\x0c\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x03\x00\x00\x00\x00\x00\x00\x00\x00\x00\x29\x17\x78\xb5\x77\xa5\x78\x81\x68\x65\x6c\x6c\x6f"
    "\x20\x77\x6f\x72\x6c\x64\x00\x00\x00\x00\x00";

// The above, but with a payload byte changed.
alignas(ZBI_ALIGNMENT) constexpr char kBadCrcZbi[] =
    "\x42\x4f\x4f\x54\x30\x00\x00\x00\xe6\xf7\x8c\x86\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x29\x17\x78\xb5\xd6\xe8\x87\x4a\x43\x4d\x44\x4c\x0c\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x03\x00\x00\x00\x00\x00\x00\x00\x00\x00\x29\x17\x78\xb5\x77\xa5\x78\x81\x00\x65\x6c\x6c\x6f"
    "\x20\x77\x6f\x72\x6c\x64\x00\x00\x00\x00\x00";

constexpr char kHelloWorld[] = "hello world";

bool EmptyZbiTest() {
  BEGIN_TEST;

  zbitl::View zbi(ktl::span<const char>{kEmptyZbi, sizeof(kEmptyZbi)});

  ASSERT_IS_OK(zbi.container_header());

  for (auto [header, payload] : zbi) {
    EXPECT_EQ(header->type, header->type);
    EXPECT_TRUE(false, "should not be reached");
  }

  ASSERT_IS_OK(zbi.take_error());

  END_TEST;
}

bool SimpleZbiTest() {
  BEGIN_TEST;

  zbitl::View zbi(ktl::span<const char>{kSimpleZbi, sizeof(kSimpleZbi)});

  ASSERT_IS_OK(zbi.container_header());

  size_t num_items = 0;
  for (auto [header, payload] : zbi) {
    EXPECT_EQ(static_cast<uint32_t>(ZBI_TYPE_CMDLINE), header->type);
    switch (num_items++) {
      case 0:
        ASSERT_EQ(sizeof(kHelloWorld), payload.size());
        EXPECT_EQ(0, memcmp(kHelloWorld, payload.data(), payload.size()));
        break;
    }
    EXPECT_TRUE(header->flags & ZBI_FLAG_VERSION);
  }
  EXPECT_EQ(1u, num_items);

  ASSERT_IS_OK(zbi.take_error());

  END_TEST;
}

bool BadCrcZbiTest() {
  BEGIN_TEST;

  zbitl::View<ktl::span<const char>, zbitl::Checking::kCrc> zbi({kBadCrcZbi, sizeof(kBadCrcZbi)});

  ASSERT_IS_OK(zbi.container_header());

  for (auto [header, payload] : zbi) {
    EXPECT_EQ(header->type, header->type);
    EXPECT_TRUE(false, "should not be reached");
  }

  auto error = zbi.take_error();
  ASSERT_TRUE(error.is_error());
  // The error should not be storage-related.
  EXPECT_FALSE(error.error_value().storage_error);

  END_TEST;
}

bool MutationTest() {
  BEGIN_TEST;

  alignas(ZBI_ALIGNMENT) char contents[sizeof(kSimpleZbi)];
  memcpy(contents, kSimpleZbi, sizeof(contents));

  // Storage type is mutable.
  zbitl::View zbi(ktl::span<char>{contents, sizeof(contents)});

  ASSERT_IS_OK(zbi.container_header());

  size_t num_items = 0;
  for (auto it = zbi.begin(); it != zbi.end(); ++it) {
    auto [header, payload] = *it;
    EXPECT_EQ(static_cast<uint32_t>(ZBI_TYPE_CMDLINE), header->type);
    switch (num_items++) {
      case 0:
        ASSERT_EQ(sizeof(kHelloWorld), payload.size());
        EXPECT_EQ(0, memcmp(kHelloWorld, payload.data(), payload.size()));
        // GCC's -Wmissing-field-initializers is buggy: it should allow
        // designated initializers without all fields, but doesn't (in C++?).
        zbi_header_t discard{};
        discard.type = ZBI_TYPE_DISCARD;
        ASSERT_TRUE(zbi.EditHeader(it, discard).is_ok());
        break;
    }
    EXPECT_TRUE(header->flags & ZBI_FLAG_VERSION);
  }
  EXPECT_EQ(1u, num_items);

  num_items = 0;
  for (auto [header, payload] : zbi) {
    EXPECT_EQ(static_cast<uint32_t>(ZBI_TYPE_DISCARD), header->type);
    switch (num_items++) {
      case 0:
        ASSERT_EQ(sizeof(kHelloWorld), payload.size());
        EXPECT_EQ(0, memcmp(kHelloWorld, payload.data(), payload.size()));
        break;
    }
    EXPECT_TRUE(header->flags & ZBI_FLAG_VERSION);
  }
  EXPECT_EQ(1u, num_items);

  ASSERT_IS_OK(zbi.take_error());

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(zbitl_tests)
UNITTEST("empty", EmptyZbiTest)
UNITTEST("simple", SimpleZbiTest)
UNITTEST("bad CRC", BadCrcZbiTest)
UNITTEST("mutation", MutationTest)
UNITTEST_END_TESTCASE(zbitl_tests, "zbitl", "Tests of ZBI template library")
