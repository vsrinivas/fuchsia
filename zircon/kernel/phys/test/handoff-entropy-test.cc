// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "../handoff-entropy.h"

#include <lib/unittest/unittest.h>
#include <lib/zbitl/image.h>
#include <zircon/boot/image.h>

#include <ktl/array.h>
#include <ktl/span.h>

#include "phys-unittest.h"
#include "phys/symbolize.h"
#include "test-main.h"

namespace {

bool ValidZbiItem() {
  BEGIN_TEST;
  static BootOptions options;
  crypto::EntropyPool zero_pool = {};

  ktl::array<uint8_t, crypto::kMinEntropyBytes> entropy = {1, 2, 3};

  ktl::array<uint8_t, 2 * sizeof(zbi_header_t) + ZBI_ALIGN(crypto::kMinEntropyBytes)> buffer = {};
  zbitl::Image<ktl::span<ktl::byte>> view(ktl::as_writable_bytes(ktl::span<uint8_t>(buffer)));
  ASSERT_TRUE(view.clear().is_ok());

  // Append the entropy payload.
  auto payload = ktl::as_writable_bytes(ktl::span<uint8_t>(entropy));

  EntropyHandoff handoff;
  options.cprng_seed_require_cmdline = false;
  handoff.AddEntropy(payload);
  ASSERT_TRUE(memcmp(payload.data(), zero_pool.contents().data(), payload.size()) == 0);

  // Check non-zero
  ASSERT_TRUE(handoff.HasEnoughEntropy());
  auto pool_or = std::move(handoff).Take(options);
  ASSERT_TRUE(pool_or);
  ASSERT_TRUE(memcmp(pool_or->contents().data(), zero_pool.contents().data(),
                     pool_or->contents().size()) != 0);
  END_TEST;
}

bool SmallZbiItem() {
  BEGIN_TEST;
  static BootOptions options;
  crypto::EntropyPool zero_pool = {};

  ktl::array<uint8_t, crypto::kMinEntropyBytes - 1> entropy = {1, 2, 3};

  auto payload = ktl::as_writable_bytes(ktl::span<uint8_t>(entropy));

  EntropyHandoff handoff;
  options.cprng_seed_require_cmdline = false;
  handoff.AddEntropy(payload);

  ASSERT_FALSE(handoff.HasEnoughEntropy());
  auto pool_or = std::move(handoff).Take(options);
  ASSERT_FALSE(pool_or);
  END_TEST;
}

bool ValidCmdlineItem() {
  BEGIN_TEST;
  static BootOptions options;
  crypto::EntropyPool zero_pool = {};

  memcpy(options.entropy_mixin.hex.data(), "0123456789ABCDEF0123456789ABCDEF", 32);
  memcpy(options.entropy_mixin.hex.data() + 32, "0123456789ABCDEF0123456789ABCDEF", 32);
  options.entropy_mixin.len = 64;

  EntropyHandoff handoff;
  handoff.AddEntropy(options);

  for (size_t i = 0; i < 64; ++i) {
    EXPECT_EQ(options.entropy_mixin.hex[i], 'x');
  }

  // Check non-zero
  ASSERT_TRUE(handoff.HasEnoughEntropy());
  auto pool_or = std::move(handoff).Take(options);
  ASSERT_TRUE(pool_or);
  ASSERT_TRUE(memcmp(pool_or->contents().data(), zero_pool.contents().data(),
                     pool_or->contents().size()) != 0);
  END_TEST;
}

bool SmallCmdlineItem() {
  BEGIN_TEST;
  static BootOptions options;
  crypto::EntropyPool zero_pool = {};

  memcpy(options.entropy_mixin.hex.data(), "0123456789ABCDEF0123456789ABCDE", 31);
  options.entropy_mixin.len = 31;

  EntropyHandoff handoff;
  handoff.AddEntropy(options);

  for (size_t i = 0; i < 64; ++i) {
    EXPECT_EQ(options.entropy_mixin.hex[i], 'x');
  }
  ASSERT_FALSE(handoff.HasEnoughEntropy());
  auto pool_or = std::move(handoff).Take(options);
  ASSERT_FALSE(pool_or);
  END_TEST;
}
}  // namespace

UNITTEST_START_TESTCASE(handoff_entropy_tests)
UNITTEST("AddEntropyFromValidZbiItem", ValidZbiItem)
UNITTEST("AddEntropyFromSmallZbiItem", SmallZbiItem)
UNITTEST("AddEntropyFromCmdLine", ValidCmdlineItem)
UNITTEST("AddEntropyFromSmallCmdLine", SmallCmdlineItem)
UNITTEST_END_TESTCASE(handoff_entropy_tests, "handoff_entropy", "handoff entropy tests")

TEST_SUITES("handoff-entropy-tests", handoff_entropy_tests);
