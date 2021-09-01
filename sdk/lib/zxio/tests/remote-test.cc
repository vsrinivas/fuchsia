// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io2/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/sync/completion.h>
#include <lib/zxio/cpp/inception.h>
#include <lib/zxio/ops.h>
#include <lib/zxio/zxio.h>

#include <atomic>
#include <memory>

#include <zxtest/zxtest.h>

#include "sdk/lib/zxio/private.h"

namespace {

namespace fio = fuchsia_io;

class TestServerBase : public fidl::WireServer<fio::Node> {
 public:
  TestServerBase() = default;
  virtual ~TestServerBase() = default;

  // Exercised by |zxio_close|.
  void Close(CloseRequestView request, CloseCompleter::Sync& completer) override {
    num_close_.fetch_add(1);
    completer.Reply(ZX_OK);
    // After the reply, we should close the connection.
    completer.Close(ZX_OK);
  }

  void Clone(CloneRequestView request, CloneCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Describe(DescribeRequestView request, DescribeCompleter::Sync& completer) override {
    fio::wire::FileObject file_object;
    completer.Reply(fio::wire::NodeInfo::WithFile(
        fidl::ObjectView<fio::wire::FileObject>::FromExternal(&file_object)));
  }

  void Sync(SyncRequestView request, SyncCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void GetAttr(GetAttrRequestView request, GetAttrCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void SetAttr(SetAttrRequestView request, SetAttrCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  uint32_t num_close() const { return num_close_.load(); }

 private:
  std::atomic<uint32_t> num_close_ = 0;
};

class Remote : public zxtest::Test {
 public:
  void SetUp() final {
    auto control_client_end = fidl::CreateEndpoints(&control_server_);
    ASSERT_OK(control_client_end.status_value());
    ASSERT_OK(zx::eventpair::create(0, &eventpair_to_client_, &eventpair_on_server_));
    ASSERT_OK(zxio_remote_init(&remote_, control_client_end->TakeChannel().release(),
                               eventpair_to_client_.release()));
  }

  template <typename ServerImpl>
  ServerImpl* StartServer() {
    server_ = std::make_unique<ServerImpl>();
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    zx_status_t status;
    EXPECT_OK(status = loop_->StartThread("fake-filesystem"));
    if (status != ZX_OK) {
      return nullptr;
    }

    EXPECT_OK(fidl::BindSingleInFlightOnly(loop_->dispatcher(), std::move(control_server_),
                                           server_.get()));
    if (status != ZX_OK) {
      return nullptr;
    }
    return static_cast<ServerImpl*>(server_.get());
  }

  void TearDown() final {
    ASSERT_EQ(0, server_->num_close());
    ASSERT_OK(zxio_close(&remote_.io));
    ASSERT_EQ(1, server_->num_close());
  }

 protected:
  zxio_storage_t remote_;
  fidl::ServerEnd<fio::Node> control_server_;

  zx::eventpair eventpair_on_server_;
  zx::eventpair eventpair_to_client_;
  std::unique_ptr<TestServerBase> server_;
  std::unique_ptr<async::Loop> loop_;
};

TEST_F(Remote, ServiceGetAttributes) {
  class TestServer : public TestServerBase {
   public:
    void GetAttr(GetAttrRequestView request, GetAttrCompleter::Sync& completer) override {
      completer.Reply(ZX_OK,
                      fuchsia_io::wire::NodeAttributes{.mode = fuchsia_io::wire::kModeTypeService});
    }
  };
  ASSERT_NO_FAILURES(StartServer<TestServer>());

  zxio_node_attributes_t attr = {};
  ASSERT_OK(zxio_attr_get(&remote_.io, &attr));
  EXPECT_EQ(ZXIO_NODE_PROTOCOL_FILE, attr.protocols);
}

TEST_F(Remote, Borrow) {
  ASSERT_NO_FAILURES(StartServer<TestServerBase>());

  zx_handle_t handle = ZX_HANDLE_INVALID;
  EXPECT_OK(zxio_borrow(&remote_.io, &handle));
  EXPECT_NE(handle, ZX_HANDLE_INVALID);
}

}  // namespace
