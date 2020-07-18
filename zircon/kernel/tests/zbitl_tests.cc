// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>
#include <lib/zbitl/view.h>

#include <ktl/span.h>

#include "corpus.h"
#include "tests.h"

// zbitl is primarily tested by its host/userland unit tests.
// This serves to test some basic cases in the kernel and phys
// environments specifically, mostly just to make sure it compiles.

#define ASSERT_IS_OK(result) ASSERT_TRUE(result.is_ok())

namespace {

constexpr char kHelloWorld[] = "hello world";

bool EmptyZbiTest() {
  BEGIN_TEST;

  zbitl::View zbi(ktl::span<const char>{zbitl::test::kEmptyZbi, sizeof(zbitl::test::kEmptyZbi)});

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

  zbitl::View zbi(ktl::span<const char>{zbitl::test::kSimpleZbi, sizeof(zbitl::test::kSimpleZbi)});

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

  zbitl::View<ktl::span<const char>, zbitl::Checking::kCrc> zbi(
      {zbitl::test::kBadCrcZbi, sizeof(zbitl::test::kBadCrcZbi)});

  ASSERT_IS_OK(zbi.container_header());

  for (auto [header, payload] : zbi) {
    EXPECT_EQ(header->type, header->type);
    EXPECT_TRUE(false, "should not be reached");
  }

  auto error = zbi.take_error();
  ASSERT_TRUE(error.is_error());
  // The error should not be storage-related.
  EXPECT_TRUE(error.error_value().storage_error.is_ok());

  END_TEST;
}

bool MutationTest() {
  BEGIN_TEST;

  alignas(ZBI_ALIGNMENT) char contents[sizeof(zbitl::test::kSimpleZbi)];
  memcpy(contents, zbitl::test::kSimpleZbi, sizeof(contents));

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
        ASSERT_TRUE(it.Replace(discard).is_ok());
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
