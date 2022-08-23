// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/zxio/zxio.h>

#include <atomic>
#include <memory>

#include <zxtest/zxtest.h>

#include "sdk/lib/zxio/private.h"

namespace {

namespace fdevice = fuchsia_device;
namespace fio = fuchsia_io;

class TestServerBase : public fidl::testing::WireTestBase<fio::Node2> {
 public:
  TestServerBase() = default;
  ~TestServerBase() override = default;

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) final {
    ADD_FAILURE("unexpected message received: %s", name.c_str());
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  // Exercised by |zxio_close|.
  void Close(CloseRequestView request, CloseCompleter::Sync& completer) override {
    num_close_.fetch_add(1);
    completer.ReplySuccess();
    completer.Close(ZX_OK);
  }

  uint32_t num_close() const { return num_close_.load(); }

 private:
  std::atomic<uint32_t> num_close_ = 0;
};

class RemoteV2 : public zxtest::Test {
 public:
  void SetUp() final {
    zx::status control_client_end = fidl::CreateEndpoints(&control_server_);
    ASSERT_OK(control_client_end.status_value());
    ASSERT_OK(zx::eventpair::create(0, &eventpair_to_client_, &eventpair_on_server_));
    ASSERT_OK(zxio_remote_v2_init(&remote_, control_client_end->TakeChannel().release(),
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
  fidl::ServerEnd<fio::Node2> control_server_;
  zx::eventpair eventpair_on_server_;
  zx::eventpair eventpair_to_client_;
  std::unique_ptr<TestServerBase> server_;
  std::unique_ptr<async::Loop> loop_;
};

TEST_F(RemoteV2, GetAttributes) {
  constexpr uint64_t kContentSize = 42;
  constexpr uint64_t kId = 1;
  class TestServer : public TestServerBase {
   public:
    void GetAttributes(GetAttributesRequestView request,
                       GetAttributesCompleter::Sync& completer) override {
      EXPECT_EQ(request->query, fio::wire::NodeAttributesQuery::kMask);
      uint64_t content_size = kContentSize;
      uint64_t id = kId;

      fidl::Arena allocator;
      fio::wire::MutableNodeAttributes mutable_attributes(allocator);
      fio::wire::ImmutableNodeAttributes immutable_attributes(allocator);
      immutable_attributes.set_content_size(allocator, content_size)
          .set_protocols(allocator, fio::wire::NodeProtocolKinds::kFile)
          .set_id(allocator, id);
      completer.ReplySuccess(mutable_attributes, immutable_attributes);
    }
  };
  ASSERT_NO_FAILURES(StartServer<TestServer>());

  zxio_node_attributes_t attr = {};
  ASSERT_OK(zxio_attr_get(&remote_.io, &attr));

  EXPECT_TRUE(attr.has.protocols);
  EXPECT_EQ(ZXIO_NODE_PROTOCOL_FILE, attr.protocols);
  EXPECT_TRUE(attr.has.content_size);
  EXPECT_EQ(kContentSize, attr.content_size);
  EXPECT_FALSE(attr.has.storage_size);
  EXPECT_FALSE(attr.has.abilities);
  EXPECT_FALSE(attr.has.creation_time);
  EXPECT_FALSE(attr.has.modification_time);
  EXPECT_TRUE(attr.has.id);
  EXPECT_EQ(kId, attr.id);
  EXPECT_FALSE(attr.has.link_count);
}

TEST_F(RemoteV2, GetAttributesError) {
  class TestServer : public TestServerBase {
   public:
    void GetAttributes(GetAttributesRequestView request,
                       GetAttributesCompleter::Sync& completer) override {
      completer.ReplyError(ZX_ERR_INVALID_ARGS);
    }
  };
  ASSERT_NO_FAILURES(StartServer<TestServer>());

  zxio_node_attributes_t attr = {};
  EXPECT_STATUS(zxio_attr_get(&remote_.io, &attr), ZX_ERR_INVALID_ARGS);
}

TEST_F(RemoteV2, SetAttributes) {
  constexpr uint64_t kCreationTime = 123;
  class TestServer : public TestServerBase {
   public:
    void UpdateAttributes(UpdateAttributesRequestView request,
                          UpdateAttributesCompleter::Sync& completer) override {
      EXPECT_TRUE(request->has_creation_time());
      EXPECT_FALSE(request->has_modification_time());

      uint64_t creation_time = kCreationTime;
      EXPECT_EQ(creation_time, request->creation_time());
      called_.store(true);
      completer.ReplySuccess();
    }

    std::atomic<bool> called_ = false;
  };

  TestServer* server;
  ASSERT_NO_FAILURES(server = StartServer<TestServer>());

  zxio_node_attributes_t attr = {};
  ZXIO_NODE_ATTR_SET(attr, creation_time, kCreationTime);
  ASSERT_OK(zxio_attr_set(&remote_.io, &attr));
  EXPECT_TRUE(server->called_.load());
}

TEST_F(RemoteV2, SetAttributesError) {
  class TestServer : public TestServerBase {
   public:
    void UpdateAttributes(UpdateAttributesRequestView request,
                          UpdateAttributesCompleter::Sync& completer) override {
      completer.ReplyError(ZX_ERR_INVALID_ARGS);
    }
  };
  ASSERT_NO_FAILURES(StartServer<TestServer>());

  zxio_node_attributes_t attr = {};
  EXPECT_STATUS(zxio_attr_set(&remote_.io, &attr), ZX_ERR_INVALID_ARGS);
}

TEST_F(RemoteV2, WaitTimeOut) {
  ASSERT_NO_FAILURES(StartServer<TestServerBase>());
  zxio_signals_t observed = ZX_SIGNAL_NONE;
  ASSERT_STATUS(ZX_ERR_TIMED_OUT,
                zxio_wait_one(&remote_.io, ZXIO_SIGNAL_ALL, ZX_TIME_INFINITE_PAST, &observed));
  EXPECT_EQ(ZXIO_SIGNAL_NONE, observed);
}

TEST_F(RemoteV2, WaitForReadable) {
  ASSERT_NO_FAILURES(StartServer<TestServerBase>());
  zxio_signals_t observed = ZX_SIGNAL_NONE;
  ASSERT_OK(eventpair_on_server_.signal_peer(
      ZX_SIGNAL_NONE, static_cast<zx_signals_t>(fdevice::wire::DeviceSignal::kReadable)));
  ASSERT_OK(zxio_wait_one(&remote_.io, ZXIO_SIGNAL_READABLE, ZX_TIME_INFINITE_PAST, &observed));
  EXPECT_EQ(ZXIO_SIGNAL_READABLE, observed);
}

TEST_F(RemoteV2, WaitForWritable) {
  ASSERT_NO_FAILURES(StartServer<TestServerBase>());
  zxio_signals_t observed = ZX_SIGNAL_NONE;
  ASSERT_OK(eventpair_on_server_.signal_peer(
      ZX_SIGNAL_NONE, static_cast<zx_signals_t>(fdevice::wire::DeviceSignal::kWritable)));
  ASSERT_OK(zxio_wait_one(&remote_.io, ZXIO_SIGNAL_WRITABLE, ZX_TIME_INFINITE_PAST, &observed));
  EXPECT_EQ(ZXIO_SIGNAL_WRITABLE, observed);
}

}  // namespace
