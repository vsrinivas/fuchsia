// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddktl/device.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <unittest/unittest.h>

//#define TEST_WILL_NOT_COMPILE 1

namespace {

class TestNone : public ddk::Device<TestNone> {
  public:
    TestNone() : ddk::Device<TestNone>(nullptr) {}

    void DdkRelease() {}
};

#define BEGIN_SUCCESS_CASE(name) \
class Test##name : public ddk::Device<Test##name, ddk::name> { \
  public: \
    Test##name() : ddk::Device<Test##name, ddk::name>(nullptr) {} \
    void DdkRelease() {}

#define END_SUCCESS_CASE };

BEGIN_SUCCESS_CASE(GetProtocolable)
    mx_status_t DdkGetProtocol(uint32_t proto_id, void* protocol) { return MX_OK; }
END_SUCCESS_CASE

BEGIN_SUCCESS_CASE(Openable)
    mx_status_t DdkOpen(mx_device_t** dev_out, uint32_t flags) { return MX_OK; }
END_SUCCESS_CASE

BEGIN_SUCCESS_CASE(OpenAtable)
    mx_status_t DdkOpenAt(mx_device_t** dev_out, const char* path, uint32_t flags) {
        return MX_OK;
    }
END_SUCCESS_CASE

BEGIN_SUCCESS_CASE(Closable)
    mx_status_t DdkClose(uint32_t flags) { return MX_OK; }
END_SUCCESS_CASE

BEGIN_SUCCESS_CASE(Unbindable)
    void DdkUnbind() {}
END_SUCCESS_CASE

BEGIN_SUCCESS_CASE(Readable)
    mx_status_t DdkRead(void* buf, size_t count, mx_off_t off, size_t* actual) { return MX_OK; }
END_SUCCESS_CASE

BEGIN_SUCCESS_CASE(Writable)
    mx_status_t DdkWrite(const void* buf, size_t count, mx_off_t off, size_t* actual) {
        return MX_OK;
    }
END_SUCCESS_CASE

BEGIN_SUCCESS_CASE(IotxnQueueable)
    void DdkIotxnQueue(iotxn_t* txn) {}
END_SUCCESS_CASE

BEGIN_SUCCESS_CASE(GetSizable)
    mx_off_t DdkGetSize() { return 0; }
END_SUCCESS_CASE

BEGIN_SUCCESS_CASE(Ioctlable)
    mx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                         size_t out_len, size_t* out_actual) {
        return MX_OK;
    }
END_SUCCESS_CASE

BEGIN_SUCCESS_CASE(Suspendable)
    mx_status_t DdkSuspend(uint32_t flags) { return MX_OK; }
END_SUCCESS_CASE

BEGIN_SUCCESS_CASE(Resumable)
    mx_status_t DdkResume(uint32_t flags) { return MX_OK; }
END_SUCCESS_CASE


template <typename T>
static bool do_test() {
    BEGIN_TEST;

    fbl::AllocChecker ac;
    auto dev = fbl::unique_ptr<T>(new (&ac) T);
    ASSERT_TRUE(ac.check(), "");

    END_TEST;
}

struct TestDispatch : public ddk::FullDevice<TestDispatch> {
    TestDispatch() : ddk::FullDevice<TestDispatch>(nullptr) {}

    // Give access to the device ops for testing
    mx_protocol_device_t* GetDeviceOps() {
        return &ddk_device_proto_;
    }

    mx_status_t DdkGetProtocol(uint32_t proto_id, void* protcool) {
        get_protocol_called = true;
        return MX_OK;
    }

    mx_status_t DdkOpen(mx_device_t** dev_out, uint32_t flags) {
        open_called = true;
        return MX_OK;
    }

    mx_status_t DdkOpenAt(mx_device_t** dev_out, const char* path, uint32_t flags) {
        open_at_called = true;
        return MX_OK;
    }

    mx_status_t DdkClose(uint32_t flags) {
        close_called = true;
        return MX_OK;
    }

    void DdkUnbind() {
        unbind_called = true;
    }

    void DdkRelease() {
        release_called = true;
    }

    mx_status_t DdkRead(void* buf, size_t count, mx_off_t off, size_t* actual) {
        read_called = true;
        return MX_OK;
    }

    mx_status_t DdkWrite(const void* buf, size_t count, mx_off_t off, size_t* actual) {
        write_called = true;
        return MX_OK;
    }

    void DdkIotxnQueue(iotxn_t* t) {
        iotxn_queue_called = true;
    }

    mx_off_t DdkGetSize() {
        get_size_called = true;
        return 0;
    }

    mx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                         size_t out_len, size_t* out_actual) {
        ioctl_called = true;
        return MX_OK;
    }

    mx_status_t DdkSuspend(uint32_t flags) {
        suspend_called = true;
        return MX_OK;
    }

    mx_status_t DdkResume(uint32_t flags) {
        resume_called = true;
        return MX_OK;
    }

    bool get_protocol_called = false;
    bool open_called = false;
    bool open_at_called = false;
    bool close_called = false;
    bool unbind_called = false;
    bool release_called = false;
    bool read_called = false;
    bool write_called = false;
    bool iotxn_queue_called = false;
    bool get_size_called = false;
    bool ioctl_called = false;
    bool suspend_called = false;
    bool resume_called = false;
};

static bool test_dispatch() {
    BEGIN_TEST;

    fbl::AllocChecker ac;
    auto dev = fbl::unique_ptr<TestDispatch>(new (&ac) TestDispatch);
    ASSERT_TRUE(ac.check(), "");

    // Since we're not adding the device to devmgr, we don't have a valid mx_device_t.
    // TODO: use a devmgr API to add a test device, and use that instead
    auto ctx = dev.get();
    auto ops = dev->GetDeviceOps();
    EXPECT_EQ(MX_OK, ops->get_protocol(ctx, 0, nullptr), "");
    EXPECT_EQ(MX_OK, ops->open(ctx, nullptr, 0), "");
    EXPECT_EQ(MX_OK, ops->open_at(ctx, nullptr, "", 0), "");
    EXPECT_EQ(MX_OK, ops->close(ctx, 0), "");
    ops->unbind(ctx);
    ops->release(ctx);
    EXPECT_EQ(MX_OK, ops->read(ctx, nullptr, 0, 0, nullptr), "");
    EXPECT_EQ(MX_OK, ops->write(ctx, nullptr, 0, 0, nullptr), "");
    ops->iotxn_queue(ctx, nullptr);
    EXPECT_EQ(0, ops->get_size(ctx), "");
    EXPECT_EQ(MX_OK, ops->ioctl(ctx, 0, nullptr, 0, nullptr, 0, nullptr), "");
    EXPECT_EQ(MX_OK, ops->suspend(ctx, 0), "");
    EXPECT_EQ(MX_OK, ops->resume(ctx, 0), "");

    EXPECT_TRUE(dev->get_protocol_called, "");
    EXPECT_TRUE(dev->open_called, "");
    EXPECT_TRUE(dev->open_at_called, "");
    EXPECT_TRUE(dev->close_called, "");
    EXPECT_TRUE(dev->unbind_called, "");
    EXPECT_TRUE(dev->release_called, "");
    EXPECT_TRUE(dev->read_called, "");
    EXPECT_TRUE(dev->write_called, "");
    EXPECT_TRUE(dev->iotxn_queue_called, "");
    EXPECT_TRUE(dev->get_size_called, "");
    EXPECT_TRUE(dev->ioctl_called, "");
    EXPECT_TRUE(dev->suspend_called, "");
    EXPECT_TRUE(dev->resume_called, "");

    END_TEST;
}

#if TEST_WILL_NOT_COMPILE || 0

class TestNotReleasable : public ddk::Device<TestNotReleasable> {
  public:
    TestNotReleasable() : ddk::Device<TestNotReleasable>(nullptr) {}
};

#define DEFINE_FAIL_CASE(name) \
class TestNot##name : public ddk::Device<TestNot##name, ddk::name> { \
  public: \
    TestNot##name() : ddk::Device<TestNot##name, ddk::name>(nullptr) {} \
    void DdkRelease() {} \
};

DEFINE_FAIL_CASE(GetProtocolable)
DEFINE_FAIL_CASE(Openable)
DEFINE_FAIL_CASE(OpenAtable)
DEFINE_FAIL_CASE(Closable)
DEFINE_FAIL_CASE(Unbindable)
DEFINE_FAIL_CASE(Readable)
DEFINE_FAIL_CASE(Writable)
DEFINE_FAIL_CASE(IotxnQueueable)
DEFINE_FAIL_CASE(GetSizable)
DEFINE_FAIL_CASE(Ioctlable)
DEFINE_FAIL_CASE(Suspendable)
DEFINE_FAIL_CASE(Resumable)

class TestBadOverride : public ddk::Device<TestBadOverride, ddk::Closable> {
  public:
    TestBadOverride() : ddk::Device<TestBadOverride, ddk::Closable>(nullptr) {}
    void DdkRelease() {}

    void DdkClose(uint32_t flags) {}
};

class TestHiddenOverride : public ddk::Device<TestHiddenOverride, ddk::Closable> {
  public:
    TestHiddenOverride()
      : ddk::Device<TestHiddenOverride, ddk::Closable>(nullptr) {}
    void DdkRelease() {}

  private:
    mx_status_t DdkClose(uint32_t flags) { return MX_OK; }
};

class TestStaticOverride : public ddk::Device<TestStaticOverride, ddk::Closable> {
  public:
    TestStaticOverride()
      : ddk::Device<TestStaticOverride, ddk::Closable>(nullptr) {}
    void DdkRelease() {}

    static mx_status_t DdkClose(uint32_t flags) { return MX_OK; }
};

template <typename D>
struct A {
    explicit A(mx_protocol_device_t* proto) {}
};

class TestNotAMixin : public ddk::Device<TestNotAMixin, A> {
  public:
    TestNotAMixin() : ddk::Device<TestNotAMixin, A>(nullptr) {}
    void DdkRelease() {}
};

class TestNotAllMixins;
using TestNotAllMixinsType = ddk::Device<TestNotAllMixins,
                                         ddk::Openable,
                                         ddk::Closable,
                                         A>;
class TestNotAllMixins : public TestNotAllMixinsType {
  public:
    TestNotAllMixins() : TestNotAllMixinsType(nullptr) {}
    void DdkRelease() {}
    mx_status_t DdkOpen(mx_device_t** dev_out, uint32_t flags) { return MX_OK; }
    mx_status_t DdkClose(uint32_t flags) { return MX_OK; }
};
#endif

}  // namespace

BEGIN_TEST_CASE(ddktl_device)
RUN_NAMED_TEST("No mixins", do_test<TestNone>);
RUN_NAMED_TEST("ddk::GetProtocolable", do_test<TestGetProtocolable>);
RUN_NAMED_TEST("ddk::Openable", do_test<TestOpenable>);
RUN_NAMED_TEST("ddk::OpenAtable", do_test<TestOpenAtable>);
RUN_NAMED_TEST("ddk::Closable", do_test<TestClosable>);
RUN_NAMED_TEST("ddk::Unbindable", do_test<TestUnbindable>);
RUN_NAMED_TEST("ddk::Readable", do_test<TestReadable>);
RUN_NAMED_TEST("ddk::Writable", do_test<TestWritable>);
RUN_NAMED_TEST("ddk::IotxnQueueable", do_test<TestIotxnQueueable>);
RUN_NAMED_TEST("ddk::GetSizable", do_test<TestGetSizable>);
RUN_NAMED_TEST("ddk::Ioctlable", do_test<TestIoctlable>);
RUN_NAMED_TEST("ddk::Suspendable", do_test<TestSuspendable>);
RUN_NAMED_TEST("ddk::Resumable", do_test<TestResumable>);

RUN_NAMED_TEST("Method dispatch test", test_dispatch);

#if TEST_WILL_NOT_COMPILE || 0
RUN_NAMED_TEST("FailNoDdkGetProtocol", do_test<TestNotGetProtocolable>);
RUN_NAMED_TEST("FailNoDdkOpen", do_test<TestNotOpenable>);
RUN_NAMED_TEST("FailNoDdkOpenAt", do_test<TestNotOpenAtable>);
RUN_NAMED_TEST("FailNoDdkClose", do_test<TestNotClosable>);
RUN_NAMED_TEST("FailNoDdkUnbind", do_test<TestNotUnbindable>);
RUN_NAMED_TEST("FailNoDdkRelease", do_test<TestNotReleasable>);
RUN_NAMED_TEST("FailNoDdkRead", do_test<TestNotReadable>);
RUN_NAMED_TEST("FailNoDdkWrite", do_test<TestNotWritable>);
RUN_NAMED_TEST("FailNoDdkIotxnQueue", do_test<TestNotIotxnQueueable>);
RUN_NAMED_TEST("FailNoDdkGetSize", do_test<TestNotGetSizable>);
RUN_NAMED_TEST("FailNoDdkIoctl", do_test<TestNotIoctlable>);
RUN_NAMED_TEST("FailNoDdkSuspend", do_test<TestNotSuspendable>);
RUN_NAMED_TEST("FailNoDdkResume", do_test<TestNotResumable>);
RUN_NAMED_TEST("FailBadOverride", do_test<TestBadOverride>);
RUN_NAMED_TEST("FailHiddenOverride", do_test<TestHiddenOverride>);
RUN_NAMED_TEST("FailStaticOverride", do_test<TestStaticOverride>);
RUN_NAMED_TEST("FailNotAMixin", do_test<TestNotAMixin>);
RUN_NAMED_TEST("FailNotAllMixins", do_test<TestNotAllMixins>);
#endif
END_TEST_CASE(ddktl_device);

test_case_element* test_case_ddktl_device = TEST_CASE_ELEMENT(ddktl_device);
