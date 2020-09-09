// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>
#include <lib/unittest/user_memory.h>

namespace {

using testing::UserMemory;

bool test_get_put() {
  BEGIN_TEST;

  {
    auto umem = UserMemory::Create(sizeof(uint32_t));
    EXPECT_EQ(0u, umem->get<uint32_t>());
  }

  {
    auto umem = UserMemory::Create(2 * sizeof(uint32_t));
    EXPECT_EQ(0u, umem->get<uint32_t>(1));
  }

  {
    auto umem = UserMemory::Create(sizeof(uint32_t));
    umem->put<uint32_t>(0);
  }

  {
    auto umem = UserMemory::Create(2 * sizeof(uint32_t));
    umem->put<uint32_t>(0, 1);
  }

  {
    auto umem = UserMemory::Create(sizeof(uint32_t));
    EXPECT_EQ(0u, umem->get<uint32_t>());
    umem->put<uint32_t>(42u);
    EXPECT_EQ(42u, umem->get<uint32_t>());
  }

  END_TEST;
}

}  // namespace

#define USER_MEMORY_UNITTEST(fname) UNITTEST(#fname, fname)

UNITTEST_START_TESTCASE(user_memory_tests)
USER_MEMORY_UNITTEST(test_get_put)
UNITTEST_END_TESTCASE(user_memory_tests, "user_memory_tests", "UserMemory tests")
