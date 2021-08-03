// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io2/llcpp/fidl.h>
#include <fuchsia/io2/llcpp/fidl_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/default.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/sync/completion.h>
#include <lib/zxio/cpp/inception.h>
#include <lib/zxio/ops.h>

#include <atomic>
#include <memory>

#include <zxtest/zxtest.h>

#include "sdk/lib/zxio/private.h"

namespace {

namespace fio2 = fuchsia_io2;

class TestServerBase : public fio2::testing::Directory_TestBase {
 public:
  TestServerBase() = default;
  virtual ~TestServerBase() = default;

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) final {
    ADD_FAILURE("unexpected message received: %s", name.c_str());
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  // Exercised by |zxio_close|.
  void Close(CloseRequestView request, CloseCompleter::Sync& completer) override {
    num_close_.fetch_add(1);
    completer.Close(ZX_OK);
  }

  uint32_t num_close() const { return num_close_.load(); }

 private:
  std::atomic<uint32_t> num_close_ = 0;
};

class DirV2 : public zxtest::Test {
 public:
  void SetUp() final {
    auto client_end = fidl::CreateEndpoints(&server_end_);
    ASSERT_OK(client_end.status_value());
    ASSERT_OK(zxio_dir_v2_init(&dir_, client_end->TakeChannel().release()));
  }

  template <typename ServerImpl>
  ServerImpl* StartServer() {
    server_ = std::make_unique<ServerImpl>();
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    zx_status_t status = ZX_OK;
    EXPECT_OK(status = loop_->StartThread("fake-filesystem"));
    if (status != ZX_OK) {
      return nullptr;
    }
    EXPECT_OK(
        fidl::BindSingleInFlightOnly(loop_->dispatcher(), std::move(server_end_), server_.get()));
    if (status != ZX_OK) {
      return nullptr;
    }
    return static_cast<ServerImpl*>(server_.get());
  }

  void TearDown() final {
    ASSERT_EQ(0, server_->num_close());
    ASSERT_OK(zxio_close(&dir_.io));
    ASSERT_EQ(1, server_->num_close());
  }

 protected:
  zxio_storage_t dir_;
  fidl::ServerEnd<fio2::Directory> server_end_;
  std::unique_ptr<TestServerBase> server_;
  std::unique_ptr<async::Loop> loop_;
};

TEST_F(DirV2, Enumerate) {
  class TestServer : public TestServerBase {
   public:
    void Enumerate(EnumerateRequestView request, EnumerateCompleter::Sync& completer) override {
      class IteratorServer : public fidl::WireServer<fio2::DirectoryIterator> {
       public:
        explicit IteratorServer(sync_completion_t* completion) : completion_(completion) {}

        // Sends a different entry every time.
        void GetNext(GetNextRequestView request, GetNextCompleter::Sync& completer) override {
          fidl::Arena allocator;
          fidl::VectorView<fio2::wire::DirectoryEntry> entry(allocator, 1);
          entry[0].Allocate(allocator);
          switch (count_) {
            case 0:
              entry[0].set_name(allocator, fidl::StringView("zero"));
              entry[0].set_protocols(allocator, fio2::wire::NodeProtocols::kDirectory);
              entry[0].set_abilities(allocator, fio2::wire::Operations::kEnumerate);
              entry[0].set_id(allocator, 0);
              break;
            case 1:
              entry[0].set_name(allocator, fidl::StringView("one"));
              entry[0].set_protocols(allocator, fio2::wire::NodeProtocols::kFile);
              entry[0].set_abilities(allocator, fio2::wire::Operations::kReadBytes);
              entry[0].set_id(allocator, 1);
              break;
            default:
              completer.ReplySuccess(fidl::VectorView<fio2::wire::DirectoryEntry>());
              return;
          }
          count_++;
          completer.ReplySuccess(std::move(entry));
        }

        ~IteratorServer() { sync_completion_signal(completion_); }

       private:
        uint64_t count_ = 0;
        sync_completion_t* completion_;
      };
      EXPECT_OK(fidl::BindSingleInFlightOnly(
          async_get_default_dispatcher(), std::move(request->iterator),
          std::make_unique<IteratorServer>(&iterator_teardown_completion_)));
    }

    sync_completion_t iterator_teardown_completion_;
  };

  TestServer* server;
  ASSERT_NO_FAILURES(server = StartServer<TestServer>());
  zxio_dirent_iterator_t iterator;
  ASSERT_OK(zxio_dirent_iterator_init(&iterator, &dir_.io));

  zxio_dirent_t* entry;
  ASSERT_OK(zxio_dirent_iterator_next(&iterator, &entry));
  EXPECT_TRUE(entry->has.protocols);
  EXPECT_EQ(ZXIO_NODE_PROTOCOL_DIRECTORY, entry->protocols);
  EXPECT_TRUE(entry->has.abilities);
  EXPECT_EQ(ZXIO_OPERATION_ENUMERATE, entry->abilities);
  EXPECT_TRUE(entry->has.id);
  EXPECT_EQ(0, entry->id);
  EXPECT_STR_EQ("zero", entry->name);
  EXPECT_EQ(strlen(entry->name), entry->name_length);

  ASSERT_OK(zxio_dirent_iterator_next(&iterator, &entry));
  EXPECT_TRUE(entry->has.protocols);
  EXPECT_EQ(ZXIO_NODE_PROTOCOL_FILE, entry->protocols);
  EXPECT_TRUE(entry->has.abilities);
  EXPECT_EQ(ZXIO_OPERATION_READ_BYTES, entry->abilities);
  EXPECT_TRUE(entry->has.id);
  EXPECT_EQ(1, entry->id);
  EXPECT_STR_EQ("one", entry->name);
  EXPECT_EQ(strlen(entry->name), entry->name_length);

  ASSERT_EQ(ZX_ERR_NOT_FOUND, zxio_dirent_iterator_next(&iterator, &entry));
  ASSERT_EQ(ZX_ERR_NOT_FOUND, zxio_dirent_iterator_next(&iterator, &entry));

  // Destroying the iterator should trigger the teardown of server-side iterator connection.
  zxio_dirent_iterator_destroy(&iterator);
  ASSERT_OK(
      sync_completion_wait_deadline(&server->iterator_teardown_completion_, ZX_TIME_INFINITE));
}

}  // namespace
