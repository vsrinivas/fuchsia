// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mxalloc/new.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/ref_counted.h>
#include <mxtl/unique_ptr.h>
#include <mxtl/type_support.h>
#include <unittest/unittest.h>

namespace {

template <typename T> struct PtrTraits;

template <typename T>
struct PtrTraits<mxtl::unique_ptr<T>> {
    using ObjType = typename mxtl::remove_cv<T>::type;
    static mxtl::unique_ptr<T> MakePointer(T* raw) { return mxtl::unique_ptr<T>(raw); }
};

template <typename T>
struct PtrTraits<mxtl::RefPtr<T>> {
    using ObjType = typename mxtl::remove_cv<T>::type;
    static mxtl::RefPtr<T> MakePointer(T* raw) { return mxtl::AdoptRef<T>(raw); }
};


class TestBase {
public:
    TestBase() { recycle_was_called_ = false; }
    static bool recycle_was_called() { return recycle_was_called_; }
protected:
    static bool recycle_was_called_;
};

bool TestBase::recycle_was_called_;

class TestPublicRecycle : public TestBase,
                          public mxtl::Recyclable<TestPublicRecycle> {
public:
    void mxtl_recycle() {
        recycle_was_called_ = true;
        delete this;
    }
};

class RefedTestPublicRecycle : public TestBase,
                               public mxtl::RefCounted<RefedTestPublicRecycle>,
                               public mxtl::Recyclable<RefedTestPublicRecycle> {
public:
    void mxtl_recycle() {
        recycle_was_called_ = true;
        delete this;
    }
};

class TestPrivateRecycle : public TestBase,
                           public mxtl::Recyclable<TestPrivateRecycle> {
private:
    friend class mxtl::Recyclable<TestPrivateRecycle>;
    void mxtl_recycle() {
        recycle_was_called_ = true;
        delete this;
    }
};

class RefedTestPrivateRecycle : public TestBase,
                                public mxtl::RefCounted<RefedTestPrivateRecycle>,
                                public mxtl::Recyclable<RefedTestPrivateRecycle> {
private:
    friend class mxtl::Recyclable<RefedTestPrivateRecycle>;
    void mxtl_recycle() {
        recycle_was_called_ = true;
        delete this;
    }
};

struct FailNoMethod : public mxtl::Recyclable<FailNoMethod> { };
struct FailBadRet : public mxtl::Recyclable<FailBadRet> { int mxtl_recycle() { return 1; } };
struct FailBadArg : public mxtl::Recyclable<FailBadArg> { void mxtl_recycle(int a = 1) {} };
class  FailNotVis : public mxtl::Recyclable<FailNotVis> { void mxtl_recycle() {} };

#if TEST_WILL_NOT_COMPILE || 0
struct FailCVBase1 : public mxtl::Recyclable<const FailCVBase1> { void mxtl_recycle() {} };
#endif
#if TEST_WILL_NOT_COMPILE || 0
struct FailCVBase2 : public mxtl::Recyclable<volatile FailCVBase2> { void mxtl_recycle() {} };
#endif
#if TEST_WILL_NOT_COMPILE || 0
struct FailCVBase3 : public mxtl::Recyclable<const volatile FailCVBase3> { void mxtl_recycle() {} };
#endif

template <typename T>
static bool do_test() {
    BEGIN_TEST;

    AllocChecker ac;
    auto ptr = PtrTraits<T>::MakePointer(new (&ac) typename PtrTraits<T>::ObjType);

    ASSERT_TRUE(ac.check(), "");
    EXPECT_FALSE(TestBase::recycle_was_called(), "");

    ptr = nullptr;
    EXPECT_TRUE(TestBase::recycle_was_called(), "");

    END_TEST;
}

}  // anon namespace

BEGIN_TEST_CASE(mxtl_recycle)
RUN_NAMED_TEST("public unique_ptr mxtl_recycle()",
               do_test<mxtl::unique_ptr<TestPublicRecycle>>)
RUN_NAMED_TEST("public const unique_ptr mxtl_recycle()",
               do_test<mxtl::unique_ptr<const TestPublicRecycle>>)
RUN_NAMED_TEST("public volatile unique_ptr mxtl_recycle()",
               do_test<mxtl::unique_ptr<volatile TestPublicRecycle>>)
RUN_NAMED_TEST("public const volatile unique_ptr mxtl_recycle()",
               do_test<mxtl::unique_ptr<const volatile TestPublicRecycle>>)
RUN_NAMED_TEST("private unique_ptr mxtl_recycle()",
               do_test<mxtl::unique_ptr<TestPrivateRecycle>>)
RUN_NAMED_TEST("private const unique_ptr mxtl_recycle()",
               do_test<mxtl::unique_ptr<const TestPrivateRecycle>>)
RUN_NAMED_TEST("private volatile unique_ptr mxtl_recycle()",
               do_test<mxtl::unique_ptr<volatile TestPrivateRecycle>>)
RUN_NAMED_TEST("private const volatile unique_ptr mxtl_recycle()",
               do_test<mxtl::unique_ptr<const volatile TestPrivateRecycle>>)
RUN_NAMED_TEST("public RefPtr mxtl_recycle()",
               do_test<mxtl::RefPtr<RefedTestPublicRecycle>>)
RUN_NAMED_TEST("private RefPtr mxtl_recycle()",
               do_test<mxtl::RefPtr<RefedTestPrivateRecycle>>)
#if TEST_WILL_NOT_COMPILE || 0
// TODO(johngro) : If we ever support RefPtr<>s to const/volatile objects,
// instantiate tests for them here.
RUN_NAMED_TEST("public const RefPtr mxtl_recycle()",
               do_test<mxtl::RefPtr<const RefedTestPublicRecycle>>)
RUN_NAMED_TEST("public volatile RefPtr mxtl_recycle()",
               do_test<mxtl::RefPtr<volatile RefedTestPublicRecycle>>)
RUN_NAMED_TEST("public const volatile RefPtr mxtl_recycle()",
               do_test<mxtl::RefPtr<const volatile RefedTestPublicRecycle>>)
RUN_NAMED_TEST("private const RefPtr mxtl_recycle()",
               do_test<mxtl::RefPtr<const RefedTestPrivateRecycle>>)
RUN_NAMED_TEST("private volatile RefPtr mxtl_recycle()",
               do_test<mxtl::RefPtr<volatile RefedTestPrivateRecycle>>)
RUN_NAMED_TEST("private const volatile RefPtr mxtl_recycle()",
               do_test<mxtl::RefPtr<const volatile RefedTestPrivateRecycle>>)
#endif
#if TEST_WILL_NOT_COMPILE || 0
RUN_NAMED_TEST("FailNoMethod", do_test<mxtl::unique_ptr<FailNoMethod>>);
#endif
#if TEST_WILL_NOT_COMPILE || 0
RUN_NAMED_TEST("FailBadRet",   do_test<mxtl::unique_ptr<FailBadRet>>);
#endif
#if TEST_WILL_NOT_COMPILE || 0
RUN_NAMED_TEST("FailBadArg",   do_test<mxtl::unique_ptr<FailBadArg>>);
#endif
#if TEST_WILL_NOT_COMPILE || 0
RUN_NAMED_TEST("FailNotVis",   do_test<mxtl::unique_ptr<FailBadArg>>);
#endif
END_TEST_CASE(mxtl_recycle);

