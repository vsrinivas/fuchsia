// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crypto/global_prng.h>
#include <lib/unittest/unittest.h>
#include <platform.h>
#include <stdint.h>
#include <string.h>

namespace crypto {

namespace {
bool identical() {
  BEGIN_TEST;

  PRNG* prng1 = GlobalPRNG::GetInstance();
  PRNG* prng2 = GlobalPRNG::GetInstance();

  EXPECT_NE(prng1, nullptr);
  EXPECT_EQ(prng1, prng2);

  END_TEST;
}

bool ZbiDoesNotContainCmdlineEntropy() {
  BEGIN_TEST;

  size_t rsize;
  uint8_t * rbase = reinterpret_cast<uint8_t *>(platform_get_ramdisk(&rsize));
  ASSERT_NE(rbase, nullptr);

  const char* needle = "kernel.entropy-mixin=";
  size_t len = strlen(needle);

  // Scan the whole zbi looking for the cmdline message.
  for (size_t i = 0; i + len <= rsize; i++) {
    EXPECT_NE(memcmp(rbase + i, needle, len), 0);
  }

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(global_prng_tests)
UNITTEST("Identical", identical)
UNITTEST("CmdlineEntropyRemoved", ZbiDoesNotContainCmdlineEntropy)
UNITTEST_END_TESTCASE(global_prng_tests, "global_prng", "Validate global PRNG singleton");

}  // namespace crypto
