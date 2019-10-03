// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <pthread.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <zxtest/zxtest.h>

namespace {

uint8_t DestructionTrackerStorage[32];

template <bool EnableAdoptionValidator>
class DestructionTracker
    : public fbl::RefCounted<DestructionTracker<EnableAdoptionValidator>, EnableAdoptionValidator> {
 public:
  explicit DestructionTracker(bool* destroyed) : destroyed_(destroyed) {}
  ~DestructionTracker() { *destroyed_ = true; }

  // During our death tests, we will be doing things which would normally be
  // Very Bad for actually heap allocated objects.  These tests only ever need
  // a single DestructionTracker object to be allocated at a time.  Overload
  // new/delete so that we are using statically allocated storage and avoid
  // doing bad things to our heap.
  void* operator new(size_t size) {
    ZX_ASSERT(size <= sizeof(DestructionTrackerStorage));
    return DestructionTrackerStorage;
  }

  void* operator new(size_t size, fbl::AllocChecker* ac) {
    ZX_ASSERT(ac != nullptr);
    ZX_ASSERT(size <= sizeof(DestructionTrackerStorage));
    ac->arm(size, true);
    return DestructionTrackerStorage;
  }

  void operator delete(void* ptr) { ZX_ASSERT(ptr == DestructionTrackerStorage); }

 private:
  bool* destroyed_;
};

static_assert(sizeof(DestructionTracker<true>) == sizeof(DestructionTracker<false>),
              "DestructionTracker debug vs. release size mismatch!");
static_assert(sizeof(DestructionTracker<true>) <= sizeof(DestructionTrackerStorage),
              "Not enough static storage for DestructionTracker<true|false>!");

template <bool EnableAdoptionValidator>
void* inc_and_dec(void* arg) {
  auto tracker = reinterpret_cast<DestructionTracker<EnableAdoptionValidator>*>(arg);
  for (size_t i = 0u; i < 500; ++i) {
    fbl::RefPtr<DestructionTracker<EnableAdoptionValidator>> ptr(tracker);
  }
  return nullptr;
}

template <bool EnableAdoptionValidator>
void ref_counted_test() {
  bool destroyed = false;
  {
    fbl::AllocChecker ac;
    fbl::RefPtr<DestructionTracker<EnableAdoptionValidator>> ptr =
        fbl::AdoptRef(new (&ac) DestructionTracker<EnableAdoptionValidator>(&destroyed));
    EXPECT_TRUE(ac.check());

    EXPECT_FALSE(destroyed, "should not be destroyed");
    void* arg = reinterpret_cast<void*>(ptr.get());

    pthread_t threads[5];
    for (size_t i = 0u; i < fbl::count_of(threads); ++i) {
      int res = pthread_create(&threads[i], NULL, &inc_and_dec<EnableAdoptionValidator>, arg);
      ASSERT_LE(0, res, "Failed to create inc_and_dec thread!");
    }

    inc_and_dec<EnableAdoptionValidator>(arg);

    for (size_t i = 0u; i < fbl::count_of(threads); ++i)
      pthread_join(threads[i], NULL);

    EXPECT_FALSE(destroyed, "should not be destroyed after inc/dec pairs");
  }
  EXPECT_TRUE(destroyed, "should be when RefPtr falls out of scope");
}

TEST(RefCountedTests, RefCountedWithAdoptValidation) {
  auto do_test = ref_counted_test<true>;
  ASSERT_NO_FAILURES(do_test());
}

TEST(RefCountedTests, RefCountedWithoutAdoptValidation) {
  auto do_test = ref_counted_test<false>;
  ASSERT_NO_FAILURES(do_test());
}

template <bool EnableAdoptionValidator>
void make_ref_counted_test() {
  bool destroyed = false;
  {
    auto ptr = fbl::MakeRefCounted<DestructionTracker<EnableAdoptionValidator>>(&destroyed);
    EXPECT_FALSE(destroyed, "should not be destroyed");
  }
  EXPECT_TRUE(destroyed, "should be when RefPtr falls out of scope");

  destroyed = false;
  {
    fbl::AllocChecker ac;
    auto ptr2 =
        fbl::MakeRefCountedChecked<DestructionTracker<EnableAdoptionValidator>>(&ac, &destroyed);
    EXPECT_TRUE(ac.check());
  }
  EXPECT_TRUE(destroyed, "should be when RefPtr falls out of scope");
}

TEST(RefCountedTests, MakeRefCountedWithAdoptValidation) {
  auto do_test = make_ref_counted_test<true>;
  ASSERT_NO_FAILURES(do_test());
}

TEST(RefCountedTests, MakeRefCountedWithoutAdoptValidation) {
  auto do_test = make_ref_counted_test<false>;
  ASSERT_NO_FAILURES(do_test());
}

TEST(RefCountedTests, WrapDeadPointerAssert) {
  bool destroyed = false;
  DestructionTracker<true>* raw = nullptr;
  {
    // Create and adopt a ref-counted object, and let it go out of scope.
    fbl::AllocChecker ac;
    fbl::RefPtr<DestructionTracker<true>> ptr =
        fbl::AdoptRef(new (&ac) DestructionTracker<true>(&destroyed));
    EXPECT_TRUE(ac.check());
    raw = ptr.get();
    EXPECT_FALSE(destroyed);
  }
  EXPECT_TRUE(destroyed);

  // Wrapping the now-destroyed object should trigger an assertion.
  auto lambda = [raw]() {
    __UNUSED fbl::RefPtr<DestructionTracker<true>> zombie = fbl::RefPtr(raw);
  };
  ASSERT_DEATH(lambda, "Assert should have fired after wraping dead object\n");
}

TEST(RefCountedTests, ExtraReleaseAssert) {
  // Create and adopt a ref-counted object.
  bool destroyed = false;
  fbl::AllocChecker ac;
  DestructionTracker<true>* raw = new (&ac) DestructionTracker<true>(&destroyed);
  ASSERT_TRUE(ac.check());
  raw->Adopt();

  // Manually release once, which should tell us to delete the object.
  EXPECT_TRUE(raw->Release());
  // (But it's not deleted since we didn't listen to the return value
  // of Release())
  EXPECT_FALSE(destroyed);

  auto lambda = [raw]() {
    // Manually releasing again should trigger the assertion.
    __UNUSED bool unused = raw->Release();
  };
  ASSERT_DEATH(lambda, "Assert should have fired after releasing object with ref count of zero\n");

  // Do not attempt to actually delete the object.  It was never actually heap
  // allocated, so we are not leaking anything, and the system is in a bad
  // state now.  Attempting to delete the object can trigger other ASSERTs
  // which will crash the test.
}

TEST(RefCountedTests, WrapZeroRefCountAssert) {
  // Create and adopt a ref-counted object.
  bool destroyed = false;
  fbl::AllocChecker ac;
  DestructionTracker<true>* raw = new (&ac) DestructionTracker<true>(&destroyed);
  ASSERT_TRUE(ac.check());
  raw->Adopt();

  // Manually release once, which should tell us to delete the object.
  EXPECT_TRUE(raw->Release());
  // (But it's not deleted since we didn't listen to the return value
  // of Release())
  EXPECT_FALSE(destroyed);

  auto lambda = [raw]() {
    // Adding another ref (by wrapping) should trigger the assertion.
    __UNUSED bool unused = raw->Release();
  };
  ASSERT_DEATH(lambda, "Assert should have fired after wraping object with ref count of zero\n");

  // Do not attempt to actually delete the object.  See previous comments.
}

TEST(RefCountedTests, AddRefUnadoptedAssert) {
  // Create an un-adopted ref-counted object.
  bool destroyed = false;
  fbl::AllocChecker ac;
  DestructionTracker<true>* raw = new (&ac) DestructionTracker<true>(&destroyed);
  ASSERT_TRUE(ac.check());

  auto lambda = [raw]() {
    // Adding a ref (by wrapping) without adopting first should trigger an
    // assertion.
    fbl::RefPtr<DestructionTracker<true>> unadopted = fbl::RefPtr(raw);
  };
  ASSERT_DEATH(lambda, "Assert should have fired after wraping non-adopted object\n");

  // Do not attempt to actually delete the object.  See previous comments.
}

TEST(RefCountedTests, ReleaseUnadoptedAssert) {
  // Create an un-adopted ref-counted object.
  bool destroyed = false;
  fbl::AllocChecker ac;
  DestructionTracker<true>* raw = new (&ac) DestructionTracker<true>(&destroyed);
  ASSERT_TRUE(ac.check());

  auto lambda = [raw]() {
    // Releasing without adopting first should trigger an assertion.
    __UNUSED bool unused = raw->Release();
  };
  ASSERT_DEATH(lambda, "Assert should have fired after releasing non-adopted object\n");

  // Do not attempt to actually delete the object.  See previous comments.
}

}  // namespace
