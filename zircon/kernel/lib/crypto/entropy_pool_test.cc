// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crypto/entropy_pool.h>
#include <lib/unittest/unittest.h>

#include "ktl/array.h"
#include "ktl/type_traits.h"

namespace {

using crypto::EntropyPool;

bool DefaultConstructorIsZeroed() {
  BEGIN_TEST;
  EntropyPool pool;
  ktl::array<uint8_t, pool.contents().size()> zeroed_contents = {0};

  ASSERT_TRUE(memcmp(pool.contents().data(), zeroed_contents.data(), zeroed_contents.size()) == 0);
  END_TEST;
}

bool AddEntropyUpdatesThePool() {
  BEGIN_TEST;
  EntropyPool pool;
  ktl::array<uint8_t, pool.contents().size()> zeroed_contents = {0};

  ktl::array<uint8_t, 15> entropy = {1, 2, 3, 4, 5, 6, 7, 8};
  ASSERT_TRUE(memcmp(pool.contents().data(), zeroed_contents.data(), zeroed_contents.size()) == 0);

  pool.Add(entropy);
  EXPECT_FALSE(memcmp(pool.contents().data(), zeroed_contents.data(), zeroed_contents.size()) == 0);

  END_TEST;
}

bool CloneCreatesCopy() {
  BEGIN_TEST;
  EntropyPool pool;

  ktl::array<uint8_t, 15> entropy = {1, 2, 3, 4, 5, 6, 7, 8};
  pool.Add(entropy);
  auto pool_clone = pool.Clone();

  ASSERT_TRUE(memcmp(pool.contents().data(), pool_clone.contents().data(),
                     pool_clone.contents().size()) == 0);

  END_TEST;
}

bool DestructorCleansUpContents() {
  BEGIN_TEST;
  ktl::aligned_storage_t<sizeof(EntropyPool)> storage;

  {
    EntropyPool* pool = new (&storage) EntropyPool();
    pool->~EntropyPool();
  }
  EntropyPool pool;
  ktl::array<uint8_t, pool.contents().size()> shredded_contents = {0};
  memset(shredded_contents.data(), EntropyPool::kShredValue, shredded_contents.size());
  ASSERT_TRUE(memcmp(shredded_contents.data(), &storage, shredded_contents.size()) == 0);

  END_TEST;
}

bool MoveCleansUpContents() {
  BEGIN_TEST;
  ktl::aligned_storage_t<sizeof(EntropyPool)> storage;

  {
    EntropyPool* pool_ptr = new (&storage) EntropyPool();
    EntropyPool new_pool(std::move(*pool_ptr));
  }

  EntropyPool pool;
  ktl::array<uint8_t, pool.contents().size()> shredded_contents = {0};
  memset(shredded_contents.data(), EntropyPool::kShredValue, shredded_contents.size());
  ASSERT_TRUE(memcmp(shredded_contents.data(), &storage, shredded_contents.size()) == 0);

  {
    EntropyPool* pool_ptr = new (&storage) EntropyPool();
    EntropyPool new_pool;
    new_pool = std::move(*pool_ptr);
  }

  ASSERT_TRUE(memcmp(shredded_contents.data(), &storage, shredded_contents.size()) == 0);
  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(crypto_entropy_pool_tests)
UNITTEST("Default Constructor is has zeroed contents.", DefaultConstructorIsZeroed)
UNITTEST("Clone generates copy.", CloneCreatesCopy)
UNITTEST("AddEntropy update contents.", AddEntropyUpdatesThePool)
UNITTEST("Move shreds contents.", DestructorCleansUpContents)
UNITTEST("Destructor shreds contents.", MoveCleansUpContents)
UNITTEST_END_TESTCASE(crypto_entropy_pool_tests, "crypto_entropy_pool",
                      "Validate security properties of entropy pool.")
