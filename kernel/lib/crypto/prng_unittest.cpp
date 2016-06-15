// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crypto/prng.h>

#include <stdint.h>
#include <unittest.h>

namespace crypto {

namespace {
bool instantiate(void) {
  BEGIN_TEST;

  { PRNG prng("", 0); }

  END_TEST;
}

bool prng_output(void) {
  BEGIN_TEST;

  static const char kSeed1[] = "abc";
  static const int kSeed1Size = 3;
  static const int kDrawSize = 13;

  PRNG prng1(kSeed1, kSeed1Size);
  uint8_t out1[kDrawSize];
  prng1.Draw(out1, sizeof(out1));

  PRNG prng2(kSeed1, kSeed1Size);
  uint8_t out2[kDrawSize];
  prng2.Draw(out2, sizeof(out2));

  EXPECT_EQ(0, memcmp(out1, out2, sizeof(out1)), "inconsistent prng");

  // Draw from prng1 again. Check that the output is different this time.
  // There is no theoritical guarantee that the output is different, but
  // kDrawSize is large enough that the probability of this happening is
  // negligible. Also this test is fully deterministic for one given PRNG
  // implementation.
  prng1.Draw(out1, sizeof(out1));

  EXPECT_NEQ(0, memcmp(out1, out2, sizeof(out1)), "prng output is constant");

  // We can expect the same output from prng2.
  prng2.Draw(out2, sizeof(out2));

  EXPECT_EQ(0, memcmp(out1, out2, sizeof(out1)), "inconsistent prng");

  // Now verify that different seeds produce different outputs.
  PRNG prng3("blah", 4);
  uint8_t out3[kDrawSize];
  prng3.Draw(out3, sizeof(out3));

  PRNG prng4("bleh", 4);
  uint8_t out4[kDrawSize];
  prng3.Draw(out4, sizeof(out4));

  EXPECT_NEQ(0, memcmp(out3, out4, sizeof(out3)), "prng output is constant");

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(prng_tests);

RUN_TEST(instantiate);
RUN_TEST(prng_output);

END_TEST_CASE(prng_tests);

}  // namespace crypto
