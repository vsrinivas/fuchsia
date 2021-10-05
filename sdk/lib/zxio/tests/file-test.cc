// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/sync/completion.h>
#include <lib/zxio/cpp/inception.h>
#include <lib/zxio/ops.h>

#include <atomic>
#include <future>
#include <memory>
#include <thread>

#include <zxtest/zxtest.h>

#include "sdk/lib/zxio/private.h"
#include "sdk/lib/zxio/tests/file_test_suite.h"
#include "sdk/lib/zxio/tests/test_file_server_base.h"

namespace {

namespace fio = fuchsia_io;

class CloseCountingFileServer : public zxio_tests::TestFileServerBase {
 public:
  CloseCountingFileServer() = default;
  virtual ~CloseCountingFileServer() = default;

  // Exercised by |zxio_close|.
  void Close(CloseRequestView request, CloseCompleter::Sync& completer) final {
    num_close_.fetch_add(1);
    zxio_tests::TestFileServerBase::Close(request, completer);
  }

  // Exercised by |zxio_close|.
  void Close2(Close2RequestView request, Close2Completer::Sync& completer) final {
    num_close_.fetch_add(1);
    zxio_tests::TestFileServerBase::Close2(request, completer);
  }

  void Describe(DescribeRequestView request, DescribeCompleter::Sync& completer) override {
    fio::wire::FileObject file_object;
    completer.Reply(fio::wire::NodeInfo::WithFile(
        fidl::ObjectView<fio::wire::FileObject>::FromExternal(&file_object)));
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

  fpromise::result<fidl::ClientEnd<fio::File>, zx_status_t> OpenConnection() {
    auto ends = fidl::CreateEndpoints<fio::File>();
    if (!ends.is_ok()) {
      return fpromise::error(ends.status_value());
    }
    auto binding = fidl::BindServer(loop_->dispatcher(), std::move(ends->server), server_.get());
    binding_ = std::make_unique<fidl::ServerBindingRef<fio::File>>(std::move(binding));
    return fpromise::ok(std::move(ends->client));
  }

  zx_status_t OpenFile() {
    auto client_end = OpenConnection();
    if (client_end.is_error()) {
      return client_end.error();
    }
    auto result = fidl::WireCall<fio::File>(client_end.value())->Describe();

    if (result.status() != ZX_OK) {
      return result.status();
    }

    EXPECT_TRUE(result->info.is_file());
    return zxio_file_init(&file_, client_end.value().TakeChannel().release(),
                          result->info.mutable_file().event.release(),
                          result->info.mutable_file().stream.release());
  }

  void TearDown() override {
    ASSERT_EQ(0, server_->num_close());
    ASSERT_OK(zxio_close(&file_.io));
    ASSERT_EQ(1, server_->num_close());
  }

 protected:
  zxio_storage_t file_;
  std::unique_ptr<CloseCountingFileServer> server_;
  std::unique_ptr<fidl::ServerBindingRef<fio::File>> binding_;
  std::unique_ptr<async::Loop> loop_;
};

class TestServerEvent final : public CloseCountingFileServer {
 public:
  TestServerEvent() { ASSERT_OK(zx::event::create(0, &event_)); }

  const zx::event& event() const { return event_; }

  void Describe(DescribeRequestView request, DescribeCompleter::Sync& completer) override {
    fio::wire::FileObject file_object;
    zx_status_t status = event_.duplicate(ZX_RIGHTS_BASIC, &file_object.event);
    if (status != ZX_OK) {
      completer.Close(ZX_ERR_INTERNAL);
      return;
    }
    completer.Reply(fio::wire::NodeInfo::WithFile(
        fidl::ObjectView<fio::wire::FileObject>::FromExternal(&file_object)));
  }

 private:
  zx::event event_;
};

TEST_F(File, WaitTimeOut) {
  ASSERT_NO_FAILURES(StartServer<TestServerEvent>());
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
  ASSERT_OK(server->event().signal(ZX_SIGNAL_NONE, fuchsia_io::wire::kFileSignalReadable));
  ASSERT_OK(zxio_wait_one(&file_.io, ZXIO_SIGNAL_READABLE, ZX_TIME_INFINITE_PAST, &observed));
  EXPECT_EQ(ZXIO_SIGNAL_READABLE, observed);
}

TEST_F(File, WaitForWritable) {
  TestServerEvent* server;
  ASSERT_NO_FAILURES(server = StartServer<TestServerEvent>());
  ASSERT_NO_FAILURES(OpenFile());

  zxio_signals_t observed = ZX_SIGNAL_NONE;
  ASSERT_OK(server->event().signal(ZX_SIGNAL_NONE, fuchsia_io::wire::kFileSignalWritable));
  ASSERT_OK(zxio_wait_one(&file_.io, ZXIO_SIGNAL_WRITABLE, ZX_TIME_INFINITE_PAST, &observed));
  EXPECT_EQ(ZXIO_SIGNAL_WRITABLE, observed);
}

TEST_F(File, GetVmoPropagatesError) {
  // Positive error codes are protocol-specific errors, and will not
  // occur in the system.
  constexpr zx_status_t kGetAttrError = 1;
  constexpr zx_status_t kGetBufferError = 2;

  class TestServer : public CloseCountingFileServer {
   public:
    void GetAttr(GetAttrRequestView request, GetAttrCompleter::Sync& completer) override {
      completer.Reply(kGetAttrError, fuchsia_io::wire::NodeAttributes{});
    }
    void GetBuffer(GetBufferRequestView request, GetBufferCompleter::Sync& completer) override {
      completer.Reply(kGetBufferError, nullptr);
    }
  };
  ASSERT_NO_FAILURES(StartServer<TestServer>());
  ASSERT_NO_FAILURES(OpenFile());

  zx::vmo vmo;
  ASSERT_STATUS(kGetBufferError,
                zxio_vmo_get_clone(&file_.io, vmo.reset_and_get_address(), nullptr));
  ASSERT_STATUS(kGetBufferError,
                zxio_vmo_get_exact(&file_.io, vmo.reset_and_get_address(), nullptr));
  ASSERT_STATUS(kGetAttrError, zxio_vmo_get_copy(&file_.io, vmo.reset_and_get_address(), nullptr));
}

class TestServerChannel final : public CloseCountingFileServer {
 public:
  TestServerChannel() {
    ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &store_));
    const size_t kZero = 0u;
    ASSERT_OK(store_.set_property(ZX_PROP_VMO_CONTENT_SIZE, &kZero, sizeof(kZero)));
    ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_READ | ZX_STREAM_MODE_WRITE, store_, 0, &stream_));
  }

  void Read(ReadRequestView request, ReadCompleter::Sync& completer) override {
    if (request->count > fio::wire::kMaxBuf) {
      completer.Close(ZX_ERR_OUT_OF_RANGE);
      return;
    }
    uint8_t buffer[fio::wire::kMaxBuf];
    zx_iovec_t vec = {
        .buffer = buffer,
        .capacity = request->count,
    };
    size_t actual = 0u;
    zx_status_t status = stream_.readv(0, &vec, 1, &actual);
    if (status != ZX_OK) {
      completer.Reply(status, fidl::VectorView<uint8_t>());
      return;
    }
    completer.Reply(ZX_OK, fidl::VectorView<uint8_t>::FromExternal(buffer, actual));
  }

  void ReadAt(ReadAtRequestView request, ReadAtCompleter::Sync& completer) override {
    if (request->count > fio::wire::kMaxBuf) {
      completer.Close(ZX_ERR_OUT_OF_RANGE);
      return;
    }
    uint8_t buffer[fio::wire::kMaxBuf];
    zx_iovec_t vec = {
        .buffer = buffer,
        .capacity = request->count,
    };
    size_t actual = 0u;
    zx_status_t status = stream_.readv_at(0, request->offset, &vec, 1, &actual);
    if (status != ZX_OK) {
      completer.Reply(status, fidl::VectorView<uint8_t>());
      return;
    }
    completer.Reply(ZX_OK, fidl::VectorView<uint8_t>::FromExternal(buffer, actual));
  }

  void Write(WriteRequestView request, WriteCompleter::Sync& completer) override {
    if (request->data.count() > fio::wire::kMaxBuf) {
      completer.Close(ZX_ERR_OUT_OF_RANGE);
      return;
    }
    zx_iovec_t vec = {
        .buffer = request->data.mutable_data(),
        .capacity = request->data.count(),
    };
    size_t actual = 0u;
    zx_status_t status = stream_.writev(0, &vec, 1, &actual);
    completer.Reply(status, actual);
  }

  void WriteAt(WriteAtRequestView request, WriteAtCompleter::Sync& completer) override {
    if (request->data.count() > fio::wire::kMaxBuf) {
      completer.Close(ZX_ERR_OUT_OF_RANGE);
      return;
    }
    zx_iovec_t vec = {
        .buffer = request->data.mutable_data(),
        .capacity = request->data.count(),
    };
    size_t actual = 0u;
    zx_status_t status = stream_.writev_at(0, request->offset, &vec, 1, &actual);
    completer.Reply(status, actual);
  }

  void Seek(SeekRequestView request, SeekCompleter::Sync& completer) override {
    zx_off_t seek = 0u;
    zx_status_t status =
        stream_.seek(static_cast<zx_stream_seek_origin_t>(request->start), request->offset, &seek);
    completer.Reply(status, seek);
  }

 private:
  zx::vmo store_;
  zx::stream stream_;
};

TEST_F(File, ReadWriteChannel) {
  ASSERT_NO_FAILURES(StartServer<TestServerChannel>());
  ASSERT_OK(OpenFile());
  ASSERT_NO_FAILURES(FileTestSuite::ReadWrite(&file_.io));
}

class TestServerStream final : public CloseCountingFileServer {
 public:
  TestServerStream() {
    ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &store_));
    const size_t kZero = 0u;
    ASSERT_OK(store_.set_property(ZX_PROP_VMO_CONTENT_SIZE, &kZero, sizeof(kZero)));
    ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_READ | ZX_STREAM_MODE_WRITE, store_, 0, &stream_));
  }

  void Describe(DescribeRequestView request, DescribeCompleter::Sync& completer) override {
    fio::wire::FileObject file_object;
    zx_status_t status = stream_.duplicate(ZX_RIGHT_SAME_RIGHTS, &file_object.stream);
    if (status != ZX_OK) {
      completer.Close(ZX_ERR_INTERNAL);
      return;
    }
    completer.Reply(fio::wire::NodeInfo::WithFile(
        fidl::ObjectView<fio::wire::FileObject>::FromExternal(&file_object)));
  }

 private:
  zx::vmo store_;
  zx::stream stream_;
};

TEST_F(File, ReadWriteStream) {
  ASSERT_NO_FAILURES(StartServer<TestServerStream>());
  ASSERT_OK(OpenFile());
  ASSERT_NO_FAILURES(FileTestSuite::ReadWrite(&file_.io));
}

class Remote : public File {
 public:
  zx_status_t OpenRemote() {
    auto client_end = OpenConnection();
    if (client_end.is_error()) {
      return client_end.error();
    }

    return zxio_remote_init(&file_, client_end.take_value().TakeChannel().release(),
                            ZX_HANDLE_INVALID);
  }
};

TEST_F(Remote, ReadWriteChannel) {
  ASSERT_NO_FAILURES(StartServer<TestServerChannel>());
  ASSERT_OK(OpenRemote());
  ASSERT_NO_FAILURES(FileTestSuite::ReadWrite(&file_.io));
}

}  // namespace
