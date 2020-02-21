// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crypto/prng.h>
#include <lib/unittest/unittest.h>
#include <stdint.h>

#include <kernel/thread.h>
#include <kernel/thread_lock.h>

namespace crypto {

namespace {
bool instantiate() {
  BEGIN_TEST;

  { PRNG prng("", 0); }

  END_TEST;
}

bool non_thread_safe_prng_same_behavior() {
  BEGIN_TEST;

  static const char kSeed1[32] = {'1', '2', '3'};
  static const int kSeed1Size = sizeof(kSeed1);
  static const char kSeed2[32] = {'a', 'b', 'c'};
  static const int kSeed2Size = sizeof(kSeed2);
  static const int kDrawSize = 13;

  PRNG prng1(kSeed1, kSeed1Size, PRNG::NonThreadSafeTag());
  PRNG prng2(kSeed1, kSeed1Size);

  EXPECT_FALSE(prng1.is_thread_safe(), "unexpected PRNG state");
  EXPECT_TRUE(prng2.is_thread_safe(), "unexpected PRNG state");

  uint8_t out1[kDrawSize] = {0};
  uint8_t out2[kDrawSize] = {0};
  prng1.Draw(out1, sizeof(out1));
  prng2.Draw(out2, sizeof(out2));
  EXPECT_EQ(0, memcmp(out1, out2, sizeof(out1)), "inconsistent prng");

  // Verify they stay in sync after adding entropy
  prng1.AddEntropy(kSeed2, kSeed2Size);
  prng2.AddEntropy(kSeed2, kSeed2Size);

  prng1.Draw(out1, sizeof(out1));
  prng2.Draw(out2, sizeof(out2));
  EXPECT_EQ(0, memcmp(out1, out2, sizeof(out1)), "inconsistent prng");

  // Verify they stay in sync after the non-thread-safe one transitions
  // to being thread-safe.
  prng1.BecomeThreadSafe();
  EXPECT_TRUE(prng1.is_thread_safe(), "unexpected PRNG state");

  prng1.Draw(out1, sizeof(out1));
  prng2.Draw(out2, sizeof(out2));
  EXPECT_EQ(0, memcmp(out1, out2, sizeof(out1)), "inconsistent prng");

  END_TEST;
}

bool reseed() {
  BEGIN_TEST;

  static const char kSeed1[32] = {'1', '2', '3'};
  static const int kSeed1Size = sizeof(kSeed1);
  static const char kSeed2[32] = {'a', 'b', 'c'};
  static const int kSeed2Size = sizeof(kSeed2);
  static const int kDrawSize = 13;

  PRNG prng1(kSeed1, kSeed1Size);
  PRNG prng2(kSeed1, kSeed1Size);
  PRNG prng3(kSeed1, kSeed1Size);

  uint8_t out1[kDrawSize] = {0};
  uint8_t out2[kDrawSize] = {0};
  uint8_t out3[kDrawSize] = {0};
  prng1.Draw(out1, sizeof(out1));
  prng2.Draw(out2, sizeof(out2));
  prng3.Draw(out3, sizeof(out3));
  EXPECT_EQ(0, memcmp(out1, out2, sizeof(out1)), "inconsistent prng");
  EXPECT_EQ(0, memcmp(out1, out3, sizeof(out1)), "inconsistent prng");

  // Verify state changed after reseeding.
  prng2.AddEntropy(kSeed2, kSeed2Size);
  prng3.SelfReseed();

  prng1.Draw(out1, sizeof(out1));
  prng2.Draw(out2, sizeof(out2));
  prng3.Draw(out3, sizeof(out3));
  EXPECT_NE(0, memcmp(out1, out2, sizeof(out1)), "same output after reseeding");
  EXPECT_NE(0, memcmp(out1, out3, sizeof(out1)), "same output after reseeding");
  EXPECT_NE(0, memcmp(out3, out2, sizeof(out1)), "same output after reseeding");

  END_TEST;
}

bool prng_output() {
  BEGIN_TEST;

  static const char kSeed1[32] = {'a', 'b', 'c'};
  static const int kSeed1Size = sizeof(kSeed1);
  static const int kDrawSize = 13;

  PRNG prng1(kSeed1, kSeed1Size);
  uint8_t out1[kDrawSize] = {0};
  prng1.Draw(out1, sizeof(out1));

  PRNG prng2(kSeed1, kSeed1Size);
  uint8_t out2[kDrawSize] = {0};
  prng2.Draw(out2, sizeof(out2));

  EXPECT_EQ(0, memcmp(out1, out2, sizeof(out1)), "inconsistent prng");

  // Draw from prng1 again. Check that the output is different this time.
  // There is no theoritical guarantee that the output is different, but
  // kDrawSize is large enough that the probability of this happening is
  // negligible. Also this test is fully deterministic for one given PRNG
  // implementation.
  prng1.Draw(out1, sizeof(out1));

  EXPECT_NE(0, memcmp(out1, out2, sizeof(out1)), "prng output is constant");

  // We can expect the same output from prng2.
  prng2.Draw(out2, sizeof(out2));

  EXPECT_EQ(0, memcmp(out1, out2, sizeof(out1)), "inconsistent prng");

  // Now verify that different seeds produce different outputs.
  static const char kSeed2[33] = {'b', 'l', 'a', 'h'};
  PRNG prng3(kSeed2, sizeof(kSeed2));
  uint8_t out3[kDrawSize] = {0};
  prng3.Draw(out3, sizeof(out3));

  static const char kSeed3[33] = {'b', 'l', 'e', 'h'};
  PRNG prng4(kSeed3, sizeof(kSeed3));
  uint8_t out4[kDrawSize] = {0};
  prng3.Draw(out4, sizeof(out4));

  EXPECT_NE(0, memcmp(out3, out4, sizeof(out3)), "prng output is constant");

  END_TEST;
}

static int cprng_drawer_thread(void* prng) {
  uint8_t buf[16] = {0};
  static_cast<PRNG*>(prng)->Draw(buf, sizeof(buf));
  return 0;
}

// If not enough entropy has been added to the CPRNG, it should block.
bool prng_blocks() {
  BEGIN_TEST;
  uint8_t fake_entropy[PRNG::kMinEntropy] = {0};

  PRNG prng(nullptr, 0, PRNG::NonThreadSafeTag());
  prng.BecomeThreadSafe();

  Thread* drawer =
      Thread::Create("cprng drawer thread", &cprng_drawer_thread, &prng, DEFAULT_PRIORITY);
  drawer->Resume();

  int64_t wait_duration = ZX_USEC(1);
  while (true) {
    {
      // The drawer thread should be blocked waiting for the prng to have enough entropy.
      Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
      if (drawer->state_ == THREAD_BLOCKED) {
        break;
      }
    }
    Thread::Current::SleepRelative(wait_duration);
    wait_duration *= 2;
  }

  prng.AddEntropy(fake_entropy, sizeof(fake_entropy));
  // After this the thread has to eventually finish.

  int thread_retcode = 0;
  zx_status_t res = drawer->Join(&thread_retcode, ZX_TIME_INFINITE);
  EXPECT_EQ(ZX_OK, res);
  END_TEST;
}

// Adding entropy before becoming thread safe should count towards the cprng
// unblocking.
bool prng_doesnt_block_if_entropy_is_added_early() {
  BEGIN_TEST;
  uint8_t fake_entropy[PRNG::kMinEntropy] = {0};

  PRNG prng(nullptr, 0, PRNG::NonThreadSafeTag());
  prng.AddEntropy(fake_entropy, sizeof(fake_entropy));
  prng.BecomeThreadSafe();
  Thread* drawer =
      Thread::Create("cprng drawer thread", &cprng_drawer_thread, &prng, DEFAULT_PRIORITY);

  drawer->Resume();
  int thread_retcode = 0;
  zx_status_t res = drawer->Join(&thread_retcode, ZX_TIME_INFINITE);
  EXPECT_EQ(ZX_OK, res);
  END_TEST;
}

bool prng_randint() {
  BEGIN_TEST;

  static const char kSeed[32] = {'a', 'b', 'c'};
  static const int kSeedSize = sizeof(kSeed);

  PRNG prng(kSeed, kSeedSize);

  // Technically could fall out of the log2 loop below, but let's be explicit
  // about this case.
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(prng.RandInt(1), 0u, "RandInt(1) must equal 0");
  }

  for (int log2 = 1; log2 < 64; ++log2) {
    for (int i = 0; i < 100; ++i) {
      uint64_t bound = 1ull << log2;
      EXPECT_LT(prng.RandInt(bound), bound, "RandInt(2^i) must be less than 2^i");
    }
  }

  bool high_bit = false;
  for (int i = 0; i < 100; ++i) {
    high_bit |= !!(prng.RandInt(UINT64_MAX) & (1ull << 63));
  }
  EXPECT_TRUE(high_bit, "RandInt(UINT64_MAX) should have high bit set sometimes");

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(prng_tests)
UNITTEST("Instantiate", instantiate)
UNITTEST("NonThreadSafeMode", non_thread_safe_prng_same_behavior)
UNITTEST("Reseed", reseed)
UNITTEST("Test Output", prng_output)
UNITTEST("Test RandInt", prng_randint)
UNITTEST("Block if not enough entropy", prng_blocks)
UNITTEST("Dont block if entropy added in early boot", prng_doesnt_block_if_entropy_is_added_early)
UNITTEST_END_TESTCASE(prng_tests, "prng", "Test pseudo-random number generator implementation.")

}  // namespace crypto
