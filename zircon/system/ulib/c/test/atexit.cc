// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cxxabi.h>
#include <threads.h>
#include <zircon/assert.h>

#include <zxtest/zxtest.h>

// This is not declared anywhere.  The compiler passes its address in its
// implicit __cxa_atexit calls.
extern "C" void* __dso_handle;

// libcxxabi does not declare `__cxa_atexit` in cxxabi.h.
// There is no header (standard or otherwise) that declares
// `extern "C" __cxa_atexit`, so we just declare it here.
extern "C" int __cxa_atexit(void (*)(void*), void*, void*);

namespace {

// The libc implementation supports some number before it does any
// dynamic allocation.  Make sure to test more than that many.
// Currently that's 32, but the implementation might change.
constexpr int kManyAtexit = 100;

int kData;

// This doesn't actually test very much inside the test itself.  The
// registered function validates that it was invoked correctly, so the
// assertion failure would make the executable fail after the test itself
// has succeeded.  But the real purpose of this test is just for the
// LeakSanitizer build to verify that `__cxa_atexit` itself doesn't leak
// internally.
TEST(AtExit, LeakCheck) {
  for (int i = 0; i < kManyAtexit; ++i) {
    EXPECT_EQ(0, __cxa_atexit([](void* ptr) { ZX_ASSERT(ptr == &kData); }, &kData, &__dso_handle));
  }
}

// This is much the same idea, but for __cxa_thread_atexit.
TEST(ThreadAtExit, LeakCheck) {
  struct Sync {
    cnd_t cond;
    mtx_t mutex;
    bool ready = false;
  } sync;
  ASSERT_EQ(thrd_success, cnd_init(&sync.cond));
  ASSERT_EQ(thrd_success, mtx_init(&sync.mutex, mtx_plain));

  thrd_start_t ManyThreadAtExit = [](void* block) -> int {
    int result = 0;
    for (int i = 0; i < kManyAtexit && result == 0; ++i) {
      result = abi::__cxa_thread_atexit([](void* ptr) { ZX_ASSERT(ptr == &kData); }, &kData,
                                        &__dso_handle);
    }
    if (block) {
      Sync* sync = static_cast<Sync*>(block);
      mtx_lock(&sync->mutex);
      sync->ready = true;
      cnd_signal(&sync->cond);
      while (true) {
        cnd_wait(&sync->cond, &sync->mutex);
      }
    }
    return result;
  };

  EXPECT_EQ(0, ManyThreadAtExit(nullptr));

  thrd_t thr;
  ASSERT_EQ(thrd_success,
            thrd_create_with_name(&thr, ManyThreadAtExit, nullptr, "ThreadAtExit.LeakCheck"));

  int result;
  ASSERT_EQ(thrd_success, thrd_join(thr, &result));
  EXPECT_EQ(0, result);

  // Now leave a thread alive so it hasn't run its destructors when the
  // process exits.
  EXPECT_EQ(thrd_success,
            thrd_create_with_name(&thr, ManyThreadAtExit, &sync, "ThreadAtExit.LeakCheck.block"));

  // Make sure it's started up and done its allocations before we return.
  mtx_lock(&sync.mutex);
  while (!sync.ready) {
    EXPECT_EQ(thrd_success, cnd_wait(&sync.cond, &sync.mutex));
  }
  mtx_unlock(&sync.mutex);
}

}  // namespace
