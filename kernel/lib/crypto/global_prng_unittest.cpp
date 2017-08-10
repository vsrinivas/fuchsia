// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crypto/global_prng.h>

#include <stdint.h>
#include <unittest.h>

namespace crypto {

namespace {
bool identical(void*) {
    BEGIN_TEST;

    PRNG* prng1 = GlobalPRNG::GetInstance();
    PRNG* prng2 = GlobalPRNG::GetInstance();

    EXPECT_NE(prng1, nullptr, "");
    EXPECT_EQ(prng1, prng2, "");

    END_TEST;
}

} // namespace

UNITTEST_START_TESTCASE(global_prng_tests)
UNITTEST("Identical", identical)
UNITTEST_END_TESTCASE(global_prng_tests, "global_prng",
                      "Validate global PRNG singleton",
                      nullptr, nullptr);

} // namespace crypto
