// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/sync/completion.h>
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
  ~TestServerBase() override = default;

  // Exercised by |zxio_close|.
  void CloseDeprecated(CloseDeprecatedRequestView request,
                       CloseDeprecatedCompleter::Sync& completer) override {
    num_close_.fetch_add(1);
    completer.Reply(ZX_OK);
    // After the reply, we should close the connection.
    completer.Close(ZX_OK);
  }

  // Exercised by |zxio_close|.
  void Close(CloseRequestView request, CloseCompleter::Sync& completer) override {
    num_close_.fetch_add(1);
    completer.ReplySuccess();
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

  void Describe2(Describe2RequestView request, Describe2Completer::Sync& completer) override {
    fio::wire::FileInfo file_info;
    fio::wire::Representation representation = fio::wire::Representation::WithFile(
        fidl::ObjectView<decltype(file_info)>::FromExternal(&file_info));
    fio::wire::ConnectionInfo connection_info;
    connection_info.set_representation(
        fidl::ObjectView<decltype(representation)>::FromExternal(&representation));
    completer.Reply(connection_info);
  }

  void SyncDeprecated(SyncDeprecatedRequestView request,
                      SyncDeprecatedCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
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

  void GetFlags(GetFlagsRequestView request, GetFlagsCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void SetFlags(SetFlagsRequestView request, SetFlagsCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void QueryFilesystem(QueryFilesystemRequestView request,
                       QueryFilesystemCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  uint32_t num_close() const { return num_close_.load(); }

 private:
  std::atomic<uint32_t> num_close_ = 0;
};

class Remote : public zxtest::Test {
 public:
  void SetUp() final {
    zx::status control_client_end = fidl::CreateEndpoints(&control_server_);
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

class TestCloneServer : public TestServerBase {
 public:
  using CloneFunc = fit::function<void(CloneRequestView request, CloneCompleter::Sync& completer)>;

  void set_clone_func(CloneFunc clone_func) { clone_func_ = std::move(clone_func); }

  void Clone(CloneRequestView request, CloneCompleter::Sync& completer) override {
    clone_func_(request, completer);
  }

 private:
  CloneFunc clone_func_;
};

class CloneTest : public zxtest::Test {
 public:
  CloneTest() : server_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  void SetUp() final {
    zx::status node_ends = fidl::CreateEndpoints<fio::Node>();
    ASSERT_OK(node_ends.status_value());
    node_client_end_ = std::move(node_ends->client);

    node_server_.set_clone_func(
        [this](TestCloneServer::CloneRequestView request,
               TestCloneServer::CloneCompleter::Sync& completer) { Clone(request, completer); });

    fidl::BindServer(server_loop_.dispatcher(), std::move(node_ends->server), &node_server_);

    ASSERT_OK(server_loop_.StartThread("fake-filesystem"));
  }

  void TearDown() final { server_loop_.Shutdown(); }

  fidl::ClientEnd<fio::Node> TakeClientEnd() { return std::move(node_client_end_); }

 private:
  void Clone(TestCloneServer::CloneRequestView request,
             TestCloneServer::CloneCompleter::Sync& completer) {
    auto server = std::make_unique<TestServerBase>();
    auto binding_ref =
        fidl::BindServer(server_loop_.dispatcher(), std::move(request->object), server.get());
    cloned_servers_.push_back(std::move(server));

    if (request->flags & fio::wire::kOpenFlagDescribe) {
      fio::wire::FileObject file_object;
      fidl::WireSendEvent(binding_ref)
          ->OnOpen(ZX_OK, fio::wire::NodeInfo::WithFile(
                              fidl::ObjectView<fio::wire::FileObject>::FromExternal(&file_object)));
    }
  }

  TestCloneServer node_server_;
  fidl::ClientEnd<fio::Node> node_client_end_;
  async::Loop server_loop_;
  std::vector<std::unique_ptr<TestServerBase>> cloned_servers_;
};

TEST_F(CloneTest, Clone) {
  zxio_storage_t node_storage;
  ASSERT_OK(zxio_create(TakeClientEnd().TakeChannel().release(), &node_storage));
  zxio_t* node = &node_storage.io;

  zx::channel clone;
  EXPECT_OK(zxio_clone(node, clone.reset_and_get_address()));

  fidl::ClientEnd<fio::Node> clone_client(std::move(clone));

  const fidl::WireResult describe_response = fidl::WireCall(clone_client)->Describe();
  ASSERT_EQ(ZX_OK, describe_response.status());

  EXPECT_TRUE(describe_response.value().info.is_file());
}

TEST_F(CloneTest, Reopen) {
  zxio_storage_t node_storage;
  ASSERT_OK(zxio_create(TakeClientEnd().TakeChannel().release(), &node_storage));
  zxio_t* node = &node_storage.io;

  zx::channel clone;
  EXPECT_OK(zxio_reopen(node, ZXIO_REOPEN_DESCRIBE, clone.reset_and_get_address()));

  fidl::ClientEnd<fio::Node> clone_client(std::move(clone));

  class EventHandler : public fidl::WireSyncEventHandler<fio::Node> {
   public:
    EventHandler(fidl::ClientEnd<fio::Node> client_end, bool& on_open_received)
        : client_end_(std::move(client_end)), on_open_received_(on_open_received) {}

    const fidl::ClientEnd<fio::Node>& client_end() const { return client_end_; }

    void OnOpen(fidl::WireEvent<fio::Node::OnOpen>* event) final {
      EXPECT_EQ(event->s, ZX_OK);
      EXPECT_TRUE(event->info.is_file());
      on_open_received_ = true;
    }

    zx_status_t Unknown() final {
      ADD_FAILURE("Unexpected event received.");
      return ZX_ERR_IO;
    }

   private:
    fidl::ClientEnd<fio::Node> client_end_;
    bool& on_open_received_;
  };

  bool on_open_received = false;
  EventHandler event_handler(std::move(clone_client), on_open_received);
  EXPECT_OK(event_handler.HandleOneEvent(event_handler.client_end()).status());
  EXPECT_TRUE(on_open_received);
}

}  // namespace
