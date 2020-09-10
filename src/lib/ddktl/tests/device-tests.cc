// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <ddktl/device.h>
#include <zxtest/zxtest.h>

// TODO(fxbug.dev/51303): The negative compilation tests are broken
//#define TEST_WILL_NOT_COMPILE 1

namespace {

class TestNone : public ddk::Device<TestNone> {
 public:
  TestNone() : ddk::Device<TestNone>(nullptr) {}

  void DdkRelease() {}
};

#define BEGIN_SUCCESS_CASE(name)                                  \
  class Test##name : public ddk::Device<Test##name, ddk::name> {  \
   public:                                                        \
    Test##name() : ddk::Device<Test##name, ddk::name>(nullptr) {} \
    void DdkRelease() {}

#define END_SUCCESS_CASE \
  }                      \
  ;

BEGIN_SUCCESS_CASE(GetProtocolable)
zx_status_t DdkGetProtocol(uint32_t proto_id, void* protocol) { return ZX_OK; }
END_SUCCESS_CASE

BEGIN_SUCCESS_CASE(Initializable)
// As the txn does not contain a valid device pointer, the destructor won't throw an error
// if we don't reply.
void DdkInit(ddk::InitTxn txn) {}
END_SUCCESS_CASE

BEGIN_SUCCESS_CASE(Openable)
zx_status_t DdkOpen(zx_device_t** dev_out, uint32_t flags) { return ZX_OK; }
END_SUCCESS_CASE

BEGIN_SUCCESS_CASE(Closable)
zx_status_t DdkClose(uint32_t flags) { return ZX_OK; }
END_SUCCESS_CASE

BEGIN_SUCCESS_CASE(Unbindable)
// As the txn does not contain a valid device pointer, the destructor won't throw an error
// if we don't reply.
void DdkUnbind(ddk::UnbindTxn txn) {}
END_SUCCESS_CASE

BEGIN_SUCCESS_CASE(Readable)
zx_status_t DdkRead(void* buf, size_t count, zx_off_t off, size_t* actual) { return ZX_OK; }
END_SUCCESS_CASE

BEGIN_SUCCESS_CASE(Writable)
zx_status_t DdkWrite(const void* buf, size_t count, zx_off_t off, size_t* actual) { return ZX_OK; }
END_SUCCESS_CASE

BEGIN_SUCCESS_CASE(GetSizable)
zx_off_t DdkGetSize() { return 0; }
END_SUCCESS_CASE

BEGIN_SUCCESS_CASE(Messageable)
zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) { return ZX_OK; }
END_SUCCESS_CASE

BEGIN_SUCCESS_CASE(Suspendable)
// As the txn does not contain a valid device pointer, the destructor won't throw an error
// if we don't reply.
void DdkSuspend(ddk::SuspendTxn txn) {}
END_SUCCESS_CASE

BEGIN_SUCCESS_CASE(Resumable)
// As the txn does not contain a valid device pointer, the destructor won't throw an error
// if we don't reply.
void DdkResume(ddk::ResumeTxn txn) {}
END_SUCCESS_CASE

BEGIN_SUCCESS_CASE(Rxrpcable)
zx_status_t DdkRxrpc(zx_handle_t channel) { return ZX_OK; }
END_SUCCESS_CASE

template <typename T>
static void do_test() {
  auto dev = std::make_unique<T>();
}

struct TestDispatch : public ddk::FullDevice<TestDispatch> {
  TestDispatch() : ddk::FullDevice<TestDispatch>(nullptr) {}

  // Give access to the device ops for testing
  const zx_protocol_device_t* GetDeviceOps() { return &ddk_device_proto_; }

  zx_status_t DdkGetProtocol(uint32_t proto_id, void* protcool) {
    get_protocol_called = true;
    return ZX_OK;
  }

  void DdkInit(ddk::InitTxn txn) { init_called = true; }

  zx_status_t DdkOpen(zx_device_t** dev_out, uint32_t flags) {
    open_called = true;
    return ZX_OK;
  }

  zx_status_t DdkClose(uint32_t flags) {
    close_called = true;
    return ZX_OK;
  }

  void DdkUnbind(ddk::UnbindTxn txn) { unbind_called = true; }

  void DdkRelease() { release_called = true; }

  zx_status_t DdkRead(void* buf, size_t count, zx_off_t off, size_t* actual) {
    read_called = true;
    return ZX_OK;
  }

  zx_status_t DdkWrite(const void* buf, size_t count, zx_off_t off, size_t* actual) {
    write_called = true;
    return ZX_OK;
  }

  zx_off_t DdkGetSize() {
    get_size_called = true;
    return 0;
  }

  void DdkSuspend(ddk::SuspendTxn txn) { suspend_called = true; }

  void DdkResume(ddk::ResumeTxn txn) { resume_called = true; }

  zx_status_t DdkRxrpc(zx_handle_t channel) {
    rxrpc_called = true;
    return ZX_OK;
  }

  bool get_protocol_called = false;
  bool init_called = false;
  bool open_called = false;
  bool close_called = false;
  bool unbind_called = false;
  bool release_called = false;
  bool read_called = false;
  bool write_called = false;
  bool get_size_called = false;
  bool suspend_called = false;
  bool resume_called = false;
  bool rxrpc_called = false;
};

TEST(DdktlDevice, Dispatch) {
  auto dev = std::make_unique<TestDispatch>();

  // Since we're not adding the device to devmgr, we don't have a valid zx_device_t.
  // TODO: use a devmgr API to add a test device, and use that instead
  auto ctx = dev.get();
  auto ops = dev->GetDeviceOps();
  EXPECT_EQ(ZX_OK, ops->get_protocol(ctx, 0, nullptr), "");
  ops->init(ctx);
  EXPECT_EQ(ZX_OK, ops->open(ctx, nullptr, 0), "");
  EXPECT_EQ(ZX_OK, ops->close(ctx, 0), "");
  ops->unbind(ctx);
  ops->release(ctx);
  EXPECT_EQ(ZX_OK, ops->read(ctx, nullptr, 0, 0, nullptr), "");
  EXPECT_EQ(ZX_OK, ops->write(ctx, nullptr, 0, 0, nullptr), "");
  EXPECT_EQ(0, ops->get_size(ctx), "");
  ops->suspend(ctx, 2, false, 0);
  ops->resume(ctx, DEV_POWER_STATE_D0);
  EXPECT_EQ(ZX_OK, ops->rxrpc(ctx, 0), "");

  EXPECT_TRUE(dev->get_protocol_called, "");
  EXPECT_TRUE(dev->init_called, "");
  EXPECT_TRUE(dev->open_called, "");
  EXPECT_TRUE(dev->close_called, "");
  EXPECT_TRUE(dev->unbind_called, "");
  EXPECT_TRUE(dev->release_called, "");
  EXPECT_TRUE(dev->read_called, "");
  EXPECT_TRUE(dev->write_called, "");
  EXPECT_TRUE(dev->get_size_called, "");
  EXPECT_TRUE(dev->suspend_called, "");
  EXPECT_TRUE(dev->resume_called, "");
  EXPECT_TRUE(dev->rxrpc_called, "");
}

#if TEST_WILL_NOT_COMPILE || 0

class TestNotReleasable : public ddk::Device<TestNotReleasable> {
 public:
  TestNotReleasable() : ddk::Device<TestNotReleasable>(nullptr) {}
};

#define DEFINE_FAIL_CASE(name)                                          \
  class TestNot##name : public ddk::Device<TestNot##name, ddk::name> {  \
   public:                                                              \
    TestNot##name() : ddk::Device<TestNot##name, ddk::name>(nullptr) {} \
    void DdkRelease() {}                                                \
  };

DEFINE_FAIL_CASE(GetProtocolable)
DEFINE_FAIL_CASE(Initializable)
DEFINE_FAIL_CASE(Openable)
DEFINE_FAIL_CASE(Closable)
DEFINE_FAIL_CASE(UnbindableNew)
DEFINE_FAIL_CASE(Readable)
DEFINE_FAIL_CASE(Writable)
DEFINE_FAIL_CASE(GetSizable)
DEFINE_FAIL_CASE(Suspendable)
DEFINE_FAIL_CASE(Resumable)
DEFINE_FAIL_CASE(Rxrpcable)

class TestBadOverride : public ddk::Device<TestBadOverride, ddk::Closable> {
 public:
  TestBadOverride() : ddk::Device<TestBadOverride, ddk::Closable>(nullptr) {}
  void DdkRelease() {}

  void DdkClose(uint32_t flags) {}
};

class TestHiddenOverride : public ddk::Device<TestHiddenOverride, ddk::Closable> {
 public:
  TestHiddenOverride() : ddk::Device<TestHiddenOverride, ddk::Closable>(nullptr) {}
  void DdkRelease() {}

 private:
  zx_status_t DdkClose(uint32_t flags) { return ZX_OK; }
};

class TestStaticOverride : public ddk::Device<TestStaticOverride, ddk::Closable> {
 public:
  TestStaticOverride() : ddk::Device<TestStaticOverride, ddk::Closable>(nullptr) {}
  void DdkRelease() {}

  static zx_status_t DdkClose(uint32_t flags) { return ZX_OK; }
};

template <typename D>
struct A {
  explicit A(zx_protocol_device_t* proto) {}
};

class TestNotAMixin : public ddk::Device<TestNotAMixin, A> {
 public:
  TestNotAMixin() : ddk::Device<TestNotAMixin, A>(nullptr) {}
  void DdkRelease() {}
};

class TestNotAllMixins;
using TestNotAllMixinsType = ddk::Device<TestNotAllMixins, ddk::Openable, ddk::Closable, A>;
class TestNotAllMixins : public TestNotAllMixinsType {
 public:
  TestNotAllMixins() : TestNotAllMixinsType(nullptr) {}
  void DdkRelease() {}
  zx_status_t DdkOpen(zx_device_t** dev_out, uint32_t flags) { return ZX_OK; }
  zx_status_t DdkClose(uint32_t flags) { return ZX_OK; }
};
#endif

TEST(DdktlDevice, NoMixins) { do_test<TestNone>(); }
TEST(DdktlDevice, MixinGetProtocolable) { do_test<TestGetProtocolable>(); }
TEST(DdktlDevice, MixinInitializable) { do_test<TestInitializable>(); }
TEST(DdktlDevice, MixinOpenable) { do_test<TestOpenable>(); }
TEST(DdktlDevice, MixinClosable) { do_test<TestClosable>(); }
TEST(DdktlDevice, MixinUnbindable) { do_test<TestUnbindable>(); }
TEST(DdktlDevice, MixinReadable) { do_test<TestReadable>(); }
TEST(DdktlDevice, MixinWritable) { do_test<TestWritable>(); }
TEST(DdktlDevice, MixinGetSizable) { do_test<TestGetSizable>(); }
TEST(DdktlDevice, MixinSuspendable) { do_test<TestSuspendable>(); }
TEST(DdktlDevice, MixinResumable) { do_test<TestResumable>(); }
TEST(DdktlDevice, MixinRxrpcable) { do_test<TestRxrpcable>(); }

}  // namespace

#if TEST_WILL_NOT_COMPILE || 0
TEST(DdktlDevice, FailNoGetProtocol) { do_test<TestNotGetProtocolable>(); }
TEST(DdktlDevice, FailNoInitialize) { do_test<TestNotInitializable>(); }
TEST(DdktlDevice, FailNoOpen) { do_test<TestNotOpenable>(); }
TEST(DdktlDevice, FailNoClose) { do_test<TestNotClosable>(); }
TEST(DdktlDevice, FailNoUnbindeNew) { do_test<TestNotUnbindableNew>(); }
TEST(DdktlDevice, FailNoReade) { do_test<TestNotReadable>(); }
TEST(DdktlDevice, FailNoWrite) { do_test<TestNotWritable>(); }
TEST(DdktlDevice, FailNoGetSize) { do_test<TestNotGetSizable>(); }
TEST(DdktlDevice, FailNoSuspende) { do_test<TestNotSuspendable>(); }
TEST(DdktlDevice, FailNoResume) { do_test<TestNotResumable>(); }
TEST(DdktlDevice, FailNoRxrpc) { do_test<TestNotRxrpcable>(); }
TEST(DdktlDevice, FailBadOverride) { do_test<TestBadOverride>(); }
TEST(DdktlDevice, FailHiddenOverride) { do_test<TestHiddenOverride>(); }
TEST(DdktlDevice, FailStaticOverride) { do_test<TestStaticOverride>(); }
TEST(DdktlDevice, FailNotAMixin) { do_test<TestNotAMixin>(); }
TEST(DdktlDevice, FailNotAllMixins) { do_test<TestNotAllMixins>(); }
#endif
