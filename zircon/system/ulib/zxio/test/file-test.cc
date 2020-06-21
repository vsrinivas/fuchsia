// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/sync/completion.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/ops.h>

#include <atomic>
#include <future>
#include <memory>
#include <thread>

#include <zxtest/zxtest.h>

#include "file_test_suite.h"

namespace {

namespace fio = llcpp::fuchsia::io;

class TestServerBase : public fio::File::Interface {
 public:
  TestServerBase() = default;
  virtual ~TestServerBase() = default;

  // Exercised by |zxio_close|.
  void Close(CloseCompleter::Sync completer) override {
    num_close_.fetch_add(1);
    completer.Reply(ZX_OK);
    // After the reply, we should close the connection.
    completer.Close(ZX_OK);
  }

  void Clone(uint32_t flags, zx::channel object, CloneCompleter::Sync completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Describe(DescribeCompleter::Sync completer) override {
    fio::FileObject file_object;
    completer.Reply(fio::NodeInfo::WithFile(fidl::unowned_ptr(&file_object)));
  }

  void Sync(SyncCompleter::Sync completer) override { completer.Close(ZX_ERR_NOT_SUPPORTED); }

  void GetAttr(GetAttrCompleter::Sync completer) override { completer.Close(ZX_ERR_NOT_SUPPORTED); }

  void SetAttr(uint32_t flags, llcpp::fuchsia::io::NodeAttributes attribute,
               SetAttrCompleter::Sync completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Read(uint64_t count, ReadCompleter::Sync completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void ReadAt(uint64_t count, uint64_t offset, ReadAtCompleter::Sync completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Write(fidl::VectorView<uint8_t> data, WriteCompleter::Sync completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void WriteAt(fidl::VectorView<uint8_t> data, uint64_t offset,
               WriteAtCompleter::Sync completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Seek(int64_t offset, llcpp::fuchsia::io::SeekOrigin start,
            SeekCompleter::Sync completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Truncate(uint64_t length, TruncateCompleter::Sync completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void GetFlags(GetFlagsCompleter::Sync completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void SetFlags(uint32_t flags, SetFlagsCompleter::Sync completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void GetBuffer(uint32_t flags, GetBufferCompleter::Sync completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  uint32_t num_close() const { return num_close_.load(); }

 private:
  std::atomic<uint32_t> num_close_ = 0;
};

class File : public zxtest::Test {
 public:
  virtual ~File() { binding_->Unbind(); }

  template <typename ServerImpl>
  ServerImpl* StartServer() {
    server_ = std::make_unique<ServerImpl>();
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    zx_status_t status;
    EXPECT_OK(status = loop_->StartThread("fake-filesystem"));
    if (status != ZX_OK) {
      return nullptr;
    }
    return static_cast<ServerImpl*>(server_.get());
  }

  fit::result<zx::channel, zx_status_t> OpenConnection() {
    zx::channel client_end, server_end;
    zx_status_t status = zx::channel::create(0, &client_end, &server_end);
    if (status != ZX_OK) {
      return fit::error(status);
    }
    auto bind_result = fidl::BindServer(loop_->dispatcher(), std::move(server_end), server_.get());
    EXPECT_TRUE(bind_result.is_ok());
    if (bind_result.is_error()) {
      return bind_result.take_error_result();
    }
    binding_ = std::make_unique<fidl::ServerBindingRef<fio::File>>(std::move(bind_result.value()));
    return fit::ok(std::move(client_end));
  }

  zx_status_t OpenFile() {
    fit::result<zx::channel, zx_status_t> client_end = OpenConnection();
    if (client_end.is_error()) {
      return client_end.error();
    }
    auto result = fio::File::Call::Describe(client_end.value().borrow());

    if (result.status() != ZX_OK) {
      return result.status();
    }

    EXPECT_TRUE(result->info.is_file());
    return zxio_file_init(&file_, client_end.take_value().release(),
                          result->info.mutable_file().event.release(),
                          result->info.mutable_file().stream.release());
  }

  void TearDown() override {
    ASSERT_EQ(0, server_->num_close());
    ASSERT_OK(zxio_close(&file_.io));
    ASSERT_EQ(1, server_->num_close());
    ASSERT_OK(zxio_destroy(&file_.io));
  }

 protected:
  zxio_storage_t file_;
  std::unique_ptr<TestServerBase> server_;
  std::unique_ptr<fidl::ServerBindingRef<fio::File>> binding_;
  std::unique_ptr<async::Loop> loop_;
};

class TestServerEvent final : public TestServerBase {
 public:
  TestServerEvent() { ASSERT_OK(zx::event::create(0, &event_)); }

  const zx::event& event() const { return event_; }

  void Describe(DescribeCompleter::Sync completer) override {
    fio::FileObject file_object;
    zx_status_t status = event_.duplicate(ZX_RIGHTS_BASIC, &file_object.event);
    if (status != ZX_OK) {
      completer.Close(ZX_ERR_INTERNAL);
      return;
    }
    completer.Reply(fio::NodeInfo::WithFile(fidl::unowned_ptr(&file_object)));
  }

 private:
  zx::event event_;
};

TEST_F(File, WaitTimeOut) {
  TestServerEvent* server;
  ASSERT_NO_FAILURES(server = StartServer<TestServerEvent>());
  ASSERT_NO_FAILURES(OpenFile());

  zxio_signals_t observed = ZX_SIGNAL_NONE;
  ASSERT_EQ(ZX_ERR_TIMED_OUT,
            zxio_wait_one(&file_.io, ZXIO_SIGNAL_ALL, ZX_TIME_INFINITE_PAST, &observed));
  EXPECT_EQ(ZXIO_SIGNAL_NONE, observed);
}

TEST_F(File, WaitForReadable) {
  TestServerEvent* server;
  ASSERT_NO_FAILURES(server = StartServer<TestServerEvent>());
  ASSERT_NO_FAILURES(OpenFile());

  zxio_signals_t observed = ZX_SIGNAL_NONE;
  ASSERT_OK(server->event().signal(ZX_SIGNAL_NONE, llcpp::fuchsia::io::FILE_SIGNAL_READABLE));
  ASSERT_OK(zxio_wait_one(&file_.io, ZXIO_SIGNAL_READABLE, ZX_TIME_INFINITE_PAST, &observed));
  EXPECT_EQ(ZXIO_SIGNAL_READABLE, observed);
}

TEST_F(File, WaitForWritable) {
  TestServerEvent* server;
  ASSERT_NO_FAILURES(server = StartServer<TestServerEvent>());
  ASSERT_NO_FAILURES(OpenFile());

  zxio_signals_t observed = ZX_SIGNAL_NONE;
  ASSERT_OK(server->event().signal(ZX_SIGNAL_NONE, llcpp::fuchsia::io::FILE_SIGNAL_WRITABLE));
  ASSERT_OK(zxio_wait_one(&file_.io, ZXIO_SIGNAL_WRITABLE, ZX_TIME_INFINITE_PAST, &observed));
  EXPECT_EQ(ZXIO_SIGNAL_WRITABLE, observed);
}

TEST_F(File, GetVmoPropagatesError) {
  // Positive error codes are protocol-specific errors, and will not
  // occur in the system.
  constexpr zx_status_t kGetAttrError = 1;
  constexpr zx_status_t kGetBufferError = 2;

  class TestServer : public TestServerBase {
   public:
    void GetAttr(GetAttrCompleter::Sync completer) override {
      completer.Reply(kGetAttrError, ::llcpp::fuchsia::io::NodeAttributes{});
    }
    void GetBuffer(uint32_t flags, GetBufferCompleter::Sync completer) override {
      completer.Reply(kGetBufferError, nullptr);
    }
  };
  TestServer* server;
  ASSERT_NO_FAILURES(server = StartServer<TestServer>());
  ASSERT_NO_FAILURES(OpenFile());

  zx::vmo vmo;
  ASSERT_STATUS(kGetBufferError,
                zxio_vmo_get_clone(&file_.io, vmo.reset_and_get_address(), nullptr));
  ASSERT_STATUS(kGetBufferError,
                zxio_vmo_get_exact(&file_.io, vmo.reset_and_get_address(), nullptr));
  ASSERT_STATUS(kGetAttrError, zxio_vmo_get_copy(&file_.io, vmo.reset_and_get_address(), nullptr));
}

class TestServerChannel final : public TestServerBase {
 public:
  TestServerChannel() {
    ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &store_));
    ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_READ | ZX_STREAM_MODE_WRITE, store_, 0, &stream_));
  }

  void Read(uint64_t count, ReadCompleter::Sync completer) override {
    if (count > fio::MAX_BUF) {
      completer.Close(ZX_ERR_OUT_OF_RANGE);
      return;
    }
    uint8_t buffer[fio::MAX_BUF];
    zx_iovec_t vec = {
        .buffer = buffer,
        .capacity = count,
    };
    size_t actual = 0u;
    zx_status_t status = stream_.readv(0, &vec, 1, &actual);
    if (status != ZX_OK) {
      completer.Reply(status, fidl::VectorView<uint8_t>());
      return;
    }
    completer.Reply(ZX_OK, fidl::VectorView(fidl::unowned_ptr(buffer), actual));
  }

  void ReadAt(uint64_t count, uint64_t offset, ReadAtCompleter::Sync completer) override {
    if (count > fio::MAX_BUF) {
      completer.Close(ZX_ERR_OUT_OF_RANGE);
      return;
    }
    uint8_t buffer[fio::MAX_BUF];
    zx_iovec_t vec = {
        .buffer = buffer,
        .capacity = count,
    };
    size_t actual = 0u;
    zx_status_t status = stream_.readv_at(0, offset, &vec, 1, &actual);
    if (status != ZX_OK) {
      completer.Reply(status, fidl::VectorView<uint8_t>());
      return;
    }
    completer.Reply(ZX_OK, fidl::VectorView(fidl::unowned_ptr(buffer), actual));
  }

  void Write(fidl::VectorView<uint8_t> data, WriteCompleter::Sync completer) override {
    if (data.count() > fio::MAX_BUF) {
      completer.Close(ZX_ERR_OUT_OF_RANGE);
      return;
    }
    zx_iovec_t vec = {
        .buffer = data.mutable_data(),
        .capacity = data.count(),
    };
    size_t actual = 0u;
    zx_status_t status = stream_.writev(0, &vec, 1, &actual);
    completer.Reply(status, actual);
  }

  void WriteAt(fidl::VectorView<uint8_t> data, uint64_t offset,
               WriteAtCompleter::Sync completer) override {
    if (data.count() > fio::MAX_BUF) {
      completer.Close(ZX_ERR_OUT_OF_RANGE);
      return;
    }
    zx_iovec_t vec = {
        .buffer = data.mutable_data(),
        .capacity = data.count(),
    };
    size_t actual = 0u;
    zx_status_t status = stream_.writev_at(0, offset, &vec, 1, &actual);
    completer.Reply(status, actual);
  }

  void Seek(int64_t offset, fio::SeekOrigin origin, SeekCompleter::Sync completer) override {
    zx_off_t seek = 0u;
    zx_status_t status = stream_.seek(static_cast<zx_stream_seek_origin_t>(origin), offset, &seek);
    completer.Reply(status, seek);
  }

 private:
  zx::vmo store_;
  zx::stream stream_;
};

TEST_F(File, ReadWriteChannel) {
  TestServerChannel* server = nullptr;
  ASSERT_NO_FAILURES(server = StartServer<TestServerChannel>());
  ASSERT_OK(OpenFile());
  ASSERT_NO_FAILURES(FileTestSuite::ReadWrite(&file_.io));
}

class TestServerStream final : public TestServerBase {
 public:
  TestServerStream() {
    ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &store_));
    ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_READ | ZX_STREAM_MODE_WRITE, store_, 0, &stream_));
  }

  void Describe(DescribeCompleter::Sync completer) override {
    fio::FileObject file_object;
    zx_status_t status = stream_.duplicate(ZX_RIGHT_SAME_RIGHTS, &file_object.stream);
    if (status != ZX_OK) {
      completer.Close(ZX_ERR_INTERNAL);
      return;
    }
    completer.Reply(fio::NodeInfo::WithFile(fidl::unowned_ptr(&file_object)));
  }

 private:
  zx::vmo store_;
  zx::stream stream_;
};

TEST_F(File, ReadWriteStream) {
  TestServerStream* server = nullptr;
  ASSERT_NO_FAILURES(server = StartServer<TestServerStream>());
  ASSERT_OK(OpenFile());
  ASSERT_NO_FAILURES(FileTestSuite::ReadWrite(&file_.io));
}

class FileConcurrentAccess : public File {
 public:
  void TearDown() override {
    // Stop closing the zxio on behalf of the test case.
  }
};

TEST_F(FileConcurrentAccess, CloseShouldInterruptOtherOps) {
  class TestServer : public TestServerBase {
   public:
    void GetAttr(GetAttrCompleter::Sync completer) override {
      // Forever delay the response... until the server is destroyed.
      // This implies the client would have to rely on |zxio_close|
      // to interrupt |zxio_attr_get|.
      EXPECT_FALSE(completer_.has_value());
      sync_completion_signal(&called_get_attr_);
      completer_ = completer.ToAsync();
    }
    virtual ~TestServer() {
      ASSERT_TRUE(completer_.has_value());
      completer_->Close(ZX_ERR_IO);
    }
    sync_completion_t* called_get_attr() { return &called_get_attr_; }

   private:
    sync_completion_t called_get_attr_;
    std::optional<GetAttrCompleter::Async> completer_;
  };
  TestServer* server;
  ASSERT_NO_FAILURES(server = StartServer<TestServer>());
  ASSERT_OK(OpenFile());

  std::atomic<bool> get_attr_returned = false;
  std::future<zx_status_t> concurrent = std::async(std::launch::async, [&] {
    zxio_node_attributes_t attr;
    zx_status_t status = zxio_attr_get(&file_.io, &attr);
    get_attr_returned.store(true);
    return status;
  });

  // First ensure |zxio_attr_get| has been blocked on the FIDL call.
  ASSERT_OK(sync_completion_wait_deadline(server->called_get_attr(), ZX_TIME_INFINITE));

  ASSERT_FALSE(get_attr_returned.load());
  ASSERT_EQ(0, server->num_close());
  ASSERT_OK(zxio_close(&file_.io));
  ASSERT_EQ(1, server->num_close());

  concurrent.wait();
  ASSERT_TRUE(get_attr_returned.load());
  ASSERT_STATUS(ZX_ERR_PEER_CLOSED, concurrent.get());
  ASSERT_OK(zxio_destroy(&file_.io));
}

class Remote : public File {
 public:
  zx_status_t OpenRemote() {
    fit::result<zx::channel, zx_status_t> client_end = OpenConnection();
    if (client_end.is_error()) {
      return client_end.error();
    }

    return zxio_remote_init(&file_, client_end.take_value().release(), ZX_HANDLE_INVALID);
  }
};

TEST_F(Remote, ReadWriteChannel) {
  TestServerChannel* server = nullptr;
  ASSERT_NO_FAILURES(server = StartServer<TestServerChannel>());
  ASSERT_OK(OpenRemote());
  ASSERT_NO_FAILURES(FileTestSuite::ReadWrite(&file_.io));
}

}  // namespace
