// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io2/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/sync/completion.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/ops.h>
#include <lib/zxio/zxio.h>

#include <atomic>
#include <memory>

#include <zxtest/zxtest.h>

namespace {

namespace fio = ::llcpp::fuchsia::io;

class TestServerBase : public fio::Node::Interface {
 public:
  TestServerBase() = default;
  virtual ~TestServerBase() = default;

  // Exercised by |zxio_close|.
  void Close(CloseCompleter::Sync& completer) override {
    num_close_.fetch_add(1);
    completer.Reply(ZX_OK);
    // After the reply, we should close the connection.
    completer.Close(ZX_OK);
  }

  void Clone(uint32_t flags, zx::channel object, CloneCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Describe(DescribeCompleter::Sync& completer) override {
    fio::FileObject file_object;
    completer.Reply(fio::NodeInfo::WithFile(fidl::unowned_ptr(&file_object)));
  }

  void Sync(SyncCompleter::Sync& completer) override { completer.Close(ZX_ERR_NOT_SUPPORTED); }

  void GetAttr(GetAttrCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void SetAttr(uint32_t flags, llcpp::fuchsia::io::NodeAttributes attribute,
               SetAttrCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  uint32_t num_close() const { return num_close_.load(); }

 private:
  std::atomic<uint32_t> num_close_ = 0;
};

class Remote : public zxtest::Test {
 public:
  void SetUp() final {
    ASSERT_OK(zx::channel::create(0, &control_client_end_, &control_server_end_));
    ASSERT_OK(zx::eventpair::create(0, &eventpair_to_client_, &eventpair_on_server_));
    ASSERT_OK(
        zxio_remote_init(&remote_, control_client_end_.release(), eventpair_to_client_.release()));
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
    EXPECT_OK(fidl::BindSingleInFlightOnly(loop_->dispatcher(), std::move(control_server_end_),
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
  zx::channel control_client_end_;
  zx::channel control_server_end_;
  zx::eventpair eventpair_on_server_;
  zx::eventpair eventpair_to_client_;
  std::unique_ptr<TestServerBase> server_;
  std::unique_ptr<async::Loop> loop_;
};

TEST_F(Remote, ServiceGetAttributes) {
  class TestServer : public TestServerBase {
   public:
    void GetAttr(GetAttrCompleter::Sync& completer) override {
      completer.Reply(ZX_OK, ::llcpp::fuchsia::io::NodeAttributes{
                                 .mode = ::llcpp::fuchsia::io::MODE_TYPE_SERVICE});
    }
  };
  ASSERT_NO_FAILURES(StartServer<TestServer>());

  zxio_node_attributes_t attr = {};
  ASSERT_OK(zxio_attr_get(&remote_.io, &attr));
  EXPECT_EQ(ZXIO_NODE_PROTOCOL_FILE, attr.protocols);
}

}  // namespace
