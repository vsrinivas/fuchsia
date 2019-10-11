// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/alloc_checker.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <zxtest/zxtest.h>

namespace {

template <typename T>
struct PtrTraits;

template <typename T>
struct PtrTraits<fbl::RefPtr<T>> {
  using ObjType = std::remove_cv_t<T>;
  static fbl::RefPtr<T> MakePointer(T* raw) { return fbl::AdoptRef<T>(raw); }
};

class TestBase {
 public:
  TestBase() { recycle_was_called_ = false; }
  static bool recycle_was_called() { return recycle_was_called_; }

 protected:
  static bool recycle_was_called_;
};

bool TestBase::recycle_was_called_;

class TestPublicRecycle : public TestBase, public fbl::Recyclable<TestPublicRecycle> {
 public:
  void fbl_recycle() {
    recycle_was_called_ = true;
    delete this;
  }
};

class RefedTestPublicRecycle : public TestBase,
                               public fbl::RefCounted<RefedTestPublicRecycle>,
                               public fbl::Recyclable<RefedTestPublicRecycle> {
 public:
  void fbl_recycle() {
    recycle_was_called_ = true;
    delete this;
  }
};

class TestPrivateRecycle : public TestBase, public fbl::Recyclable<TestPrivateRecycle> {
 private:
  friend class fbl::Recyclable<TestPrivateRecycle>;
  void fbl_recycle() {
    recycle_was_called_ = true;
    delete this;
  }
};

class RefedTestPrivateRecycle : public TestBase,
                                public fbl::RefCounted<RefedTestPrivateRecycle>,
                                public fbl::Recyclable<RefedTestPrivateRecycle> {
 private:
  friend class fbl::Recyclable<RefedTestPrivateRecycle>;
  void fbl_recycle() {
    recycle_was_called_ = true;
    delete this;
  }
};

struct FailNoMethod : public fbl::Recyclable<FailNoMethod> {};
struct FailBadRet : public fbl::Recyclable<FailBadRet> {
  int fbl_recycle() { return 1; }
};
struct FailBadArg : public fbl::Recyclable<FailBadArg> {
  void fbl_recycle(int a = 1) {}
};
class FailNotVis : public fbl::Recyclable<FailNotVis> {
  void fbl_recycle() {}
};

#if TEST_WILL_NOT_COMPILE || 0
struct FailCVBase1 : public fbl::Recyclable<const FailCVBase1> {
  void fbl_recycle() {}
};
#endif
#if TEST_WILL_NOT_COMPILE || 0
struct FailCVBase2 : public fbl::Recyclable<volatile FailCVBase2> {
  void fbl_recycle() {}
};
#endif
#if TEST_WILL_NOT_COMPILE || 0
struct FailCVBase3 : public fbl::Recyclable<const volatile FailCVBase3> {
  void fbl_recycle() {}
};
#endif

template <typename T>
void DoTest() {
  fbl::AllocChecker ac;
  auto ptr = PtrTraits<T>::MakePointer(new (&ac) typename PtrTraits<T>::ObjType);

  ASSERT_TRUE(ac.check());
  EXPECT_FALSE(TestBase::recycle_was_called());

  ptr = nullptr;
  EXPECT_TRUE(TestBase::recycle_was_called());
}

TEST(RecycleTests, PublicRecycle) {
  auto do_test = DoTest<fbl::RefPtr<RefedTestPublicRecycle>>;
  ASSERT_NO_FAILURES(do_test());
}

TEST(RecycleTests, PrivateRecycle) {
  auto do_test = DoTest<fbl::RefPtr<RefedTestPrivateRecycle>>;
  ASSERT_NO_FAILURES(do_test());
}

// TODO(johngro) : If we ever support RefPtr<>s to const/volatile objects,
// instantiate tests for them here.
#if TEST_WILL_NOT_COMPILE || 0
TEST(RecycleTests, ConstPublicRecycle) {
  auto do_test = DoTest<fbl::RefPtr<const RefedTestPublicRecycle>>;
  ASSERT_NO_FAILURES(do_test());
}

TEST(RecycleTests, VolatilePublicRecycle) {
  auto do_test = DoTest<fbl::RefPtr<volatile RefedTestPublicRecycle>>;
  ASSERT_NO_FAILURES(do_test());
}

TEST(RecycleTests, ConstVolatilePublicRecycle) {
  auto do_test = DoTest<fbl::RefPtr<const volatile RefedTestPublicRecycle>>;
  ASSERT_NO_FAILURES(do_test());
}

TEST(RecycleTests, ConstPrivateRecycle) {
  auto do_test = DoTest<fbl::RefPtr<const RefedTestPrivateRecycle>>;
  ASSERT_NO_FAILURES(do_test());
}

TEST(RecycleTests, VolatilePrivateRecycle) {
  auto do_test = DoTest<fbl::RefPtr<volatile RefedTestPrivateRecycle>>;
  ASSERT_NO_FAILURES(do_test());
}

TEST(RecycleTests, ConstVolatilePrivateRecycle) {
  auto do_test = DoTest<fbl::RefPtr<const volatile RefedTestPrivateRecycle>>;
  ASSERT_NO_FAILURES(do_test());
}
#endif

}  // namespace
