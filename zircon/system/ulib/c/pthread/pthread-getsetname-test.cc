// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _ALL_SOURCE  // For thrd_create_with_name.

#include <pthread.h>
#include <threads.h>
#include <zircon/assert.h>

#include <algorithm>
#include <mutex>

#include <zxtest/zxtest.h>

struct Thread {
  thrd_t thrd;
  std::mutex mutex;

  static int thread(void *mutex_ptr) __TA_ACQUIRE(mutex) __TA_RELEASE(mutex) {
    std::mutex &mutex = *reinterpret_cast<std::mutex *>(mutex_ptr);
    std::unique_lock lock{mutex};
    return 0;
  }

  Thread() {
    mutex.lock();
    thrd_create_with_name(&thrd, thread, &mutex, "thread-name");
  }

  ~Thread() {
    mutex.unlock();
    thrd_join(thrd, nullptr);
  }

  operator thrd_t() const { return thrd; }
};

template <class Function>
void testBoth(Function &f) {
  f(pthread_self());
  Thread thrd;
  f(thrd.thrd);
}

TEST(PthreadGetSetNameTest, GetNameBasic) {
  Thread thrd;
  char name[ZX_MAX_NAME_LEN];
  pthread_getname_np(thrd, name, sizeof(name));
  EXPECT_STREQ(name, "thread-name");
}

TEST(PthreadGetSetNameTest, GetNameTruncate) {
  Thread thrd;
  char name[ZX_MAX_NAME_LEN]{'a', 'b'};
  // Size 0 shouldn't touch name.
  pthread_getname_np(thrd, name, 0);
  EXPECT_STREQ(name, "ab");
  pthread_getname_np(thrd, name, 1);
  EXPECT_STREQ(name, "");
  pthread_getname_np(thrd, name, 2);
  EXPECT_STREQ(name, "t");
  pthread_getname_np(thrd, name, 7);
  EXPECT_STREQ(name, "thread");
  // If this wrote over ZX_MAX_NAME_LEN this would crash
  pthread_getname_np(thrd, name, 100000);
  EXPECT_STREQ(name, "thread-name");
}

#if !__has_feature(undefined_behavior_sanitizer)
TEST(PthreadGetSetNameTest, GetNameErrors) {
  auto test = [](auto &&thrd) {
    ASSERT_DEATH([&thrd] { pthread_getname_np(thrd, nullptr, ZX_MAX_NAME_LEN); });
  };
  testBoth(test);
}
#endif

TEST(PthreadGetSetNameTest, SetName) {
  auto test = [](auto &&thrd) {
    char newname[] = "new-thread-name";
    pthread_setname_np(thrd, newname);
    char name[ZX_MAX_NAME_LEN];
    pthread_getname_np(thrd, name, sizeof(name));
    EXPECT_STREQ(name, newname);
  };
  testBoth(test);
}

template <size_t I>
static void test() {
  if (!I)
    return;

  auto t = [](auto &&thrd) {
    struct A {
      char c = 'a';
    };
    A newname[I];
    newname[I - 1].c = 0;
    pthread_setname_np(thrd, reinterpret_cast<const char *>(newname));
    char name[I];
    pthread_getname_np(thrd, name, sizeof(name));
    constexpr size_t last = std::min(ZX_MAX_NAME_LEN, I) - 1;
    EXPECT_EQ(0, name[last]);
    for (size_t i = 0; i < last; i++)
      EXPECT_EQ('a', name[i]);
  };
  testBoth(t);
}

template <size_t... Ints>
static void runTests(std::index_sequence<Ints...>) {
  (test<Ints>(), ...);
}

template <size_t next, size_t... Ints>
constexpr auto append(std::index_sequence<Ints...> seq) {
  return std::index_sequence<Ints..., next>{};
}

TEST(PthreadGetSetNameTest, SetNameManySizes) {
  runTests(append<10000>(std::make_index_sequence<ZX_MAX_NAME_LEN + 5>{}));
}
