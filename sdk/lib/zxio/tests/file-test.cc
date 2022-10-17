// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/wire/server.h>
#include <lib/sync/completion.h>
#include <lib/zxio/ops.h>

#include <atomic>
#include <memory>

#include <zxtest/zxtest.h>

#include "sdk/lib/zxio/private.h"
#include "sdk/lib/zxio/tests/file_test_suite.h"
#include "sdk/lib/zxio/tests/test_file_server_base.h"

namespace {

namespace fio = fuchsia_io;

class CloseCountingFileServer : public zxio_tests::TestFileServerBase {
 public:
  CloseCountingFileServer() = default;
  ~CloseCountingFileServer() override = default;

  virtual void Init() {}

  // Exercised by |zxio_close|.
  void Close(CloseCompleter::Sync& completer) final {
    num_close_.fetch_add(1);
    zxio_tests::TestFileServerBase::Close(completer);
  }

  void Query(QueryCompleter::Sync& completer) final {
    const std::string_view kProtocol = fuchsia_io::wire::kFileProtocolName;
    uint8_t* data = reinterpret_cast<uint8_t*>(const_cast<char*>(kProtocol.data()));
    completer.Reply(fidl::VectorView<uint8_t>::FromExternal(data, kProtocol.size()));
  }

  void Describe2(Describe2Completer::Sync& completer) override { completer.Reply({}); }

  uint32_t num_close() const { return num_close_.load(); }

 private:
  std::atomic<uint32_t> num_close_ = 0;
};

class File : public zxtest::Test {
 public:
  ~File() override { binding_->Unbind(); }

  template <typename ServerImpl>
  void StartServer() {
    server_ = std::make_unique<ServerImpl>();
    ASSERT_NO_FATAL_FAILURE(server_->Init());
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    ASSERT_OK(loop_->StartThread("fake-filesystem"));
  }

  template <typename ServerImpl>
  void StartAndGetServer(ServerImpl** out_server) {
    ASSERT_NO_FATAL_FAILURE(StartServer<ServerImpl>());
    *out_server = static_cast<ServerImpl*>(server_.get());
  }

  zx::result<fidl::ClientEnd<fio::File>> OpenConnection() {
    zx::result ends = fidl::CreateEndpoints<fio::File>();
    if (ends.is_error()) {
      return ends.take_error();
    }
    auto binding = fidl::BindServer(loop_->dispatcher(), std::move(ends->server), server_.get());
    binding_ = std::make_unique<fidl::ServerBindingRef<fio::File>>(std::move(binding));
    return zx::ok(std::move(ends->client));
  }

  zx_status_t OpenFile() {
    zx::result client_end = OpenConnection();
    if (client_end.is_error()) {
      return client_end.status_value();
    }
    fidl::WireResult result = fidl::WireCall<fio::File>(client_end.value())->Describe2();
    if (result.status() != ZX_OK) {
      return result.status();
    }
    fio::wire::FileInfo& file = result.value();
    return zxio_file_init(&file_, file.has_observer() ? std::move(file.observer()) : zx::event{},
                          file.has_stream() ? std::move(file.stream()) : zx::stream{},
                          fidl::ClientEnd<fio::Node>{client_end.value().TakeChannel()});
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
  void Init() override { ASSERT_OK(zx::event::create(0, &event_)); }

  const zx::event& event() const { return event_; }

  void Describe2(Describe2Completer::Sync& completer) final {
    zx::event event;
    if (zx_status_t status = event_.duplicate(ZX_RIGHTS_BASIC, &event); status != ZX_OK) {
      completer.Close(ZX_ERR_INTERNAL);
      return;
    }
    fidl::Arena alloc;
    completer.Reply(fio::wire::FileInfo::Builder(alloc).observer(std::move(event)).Build());
  }

 private:
  zx::event event_;
};

TEST_F(File, WaitTimeOut) {
  ASSERT_NO_FAILURES(StartServer<TestServerEvent>());
  ASSERT_NO_FAILURES(OpenFile());

  zxio_signals_t observed = ZX_SIGNAL_NONE;
  ASSERT_STATUS(zxio_wait_one(&file_.io, ZXIO_SIGNAL_ALL, ZX_TIME_INFINITE_PAST, &observed),
                ZX_ERR_TIMED_OUT);
  EXPECT_EQ(ZXIO_SIGNAL_NONE, observed);
}

TEST_F(File, WaitForReadable) {
  TestServerEvent* server;
  ASSERT_NO_FAILURES(StartAndGetServer<TestServerEvent>(&server));
  ASSERT_NO_FAILURES(OpenFile());

  zxio_signals_t observed = ZX_SIGNAL_NONE;
  ASSERT_OK(server->event().signal(
      ZX_SIGNAL_NONE, static_cast<zx_signals_t>(fuchsia_io::wire::FileSignal::kReadable)));
  ASSERT_OK(zxio_wait_one(&file_.io, ZXIO_SIGNAL_READABLE, ZX_TIME_INFINITE_PAST, &observed));
  EXPECT_EQ(ZXIO_SIGNAL_READABLE, observed);
}

TEST_F(File, WaitForWritable) {
  TestServerEvent* server;
  ASSERT_NO_FAILURES(StartAndGetServer<TestServerEvent>(&server));
  ASSERT_NO_FAILURES(OpenFile());

  zxio_signals_t observed = ZX_SIGNAL_NONE;
  ASSERT_OK(server->event().signal(
      ZX_SIGNAL_NONE, static_cast<zx_signals_t>(fuchsia_io::wire::FileSignal::kWritable)));
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
    void GetAttr(GetAttrCompleter::Sync& completer) override {
      completer.Reply(kGetAttrError, fuchsia_io::wire::NodeAttributes{});
    }
    void GetBackingMemory(GetBackingMemoryRequestView request,
                          GetBackingMemoryCompleter::Sync& completer) override {
      completer.ReplyError(kGetBufferError);
    }
  };
  ASSERT_NO_FAILURES(StartServer<TestServer>());
  ASSERT_NO_FAILURES(OpenFile());

  zx::vmo vmo;
  ASSERT_STATUS(kGetBufferError, zxio_vmo_get_clone(&file_.io, vmo.reset_and_get_address()));
  ASSERT_STATUS(kGetBufferError, zxio_vmo_get_exact(&file_.io, vmo.reset_and_get_address()));
  ASSERT_STATUS(kGetAttrError, zxio_vmo_get_copy(&file_.io, vmo.reset_and_get_address()));
}

class TestServerChannel final : public CloseCountingFileServer {
 public:
  void Init() override {
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
      completer.ReplyError(status);
      return;
    }
    completer.ReplySuccess(fidl::VectorView<uint8_t>::FromExternal(buffer, actual));
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
      completer.ReplyError(status);
      return;
    }
    completer.ReplySuccess(fidl::VectorView<uint8_t>::FromExternal(buffer, actual));
  }

  void Write(WriteRequestView request, WriteCompleter::Sync& completer) override {
    if (request->data.count() > fio::wire::kMaxBuf) {
      completer.Close(ZX_ERR_OUT_OF_RANGE);
      return;
    }
    zx_iovec_t vec = {
        .buffer = request->data.data(),
        .capacity = request->data.count(),
    };
    size_t actual = 0u;
    zx_status_t status = stream_.writev(0, &vec, 1, &actual);
    if (status != ZX_OK) {
      completer.ReplyError(status);
      return;
    }
    completer.ReplySuccess(actual);
  }

  void WriteAt(WriteAtRequestView request, WriteAtCompleter::Sync& completer) override {
    if (request->data.count() > fio::wire::kMaxBuf) {
      completer.Close(ZX_ERR_OUT_OF_RANGE);
      return;
    }
    zx_iovec_t vec = {
        .buffer = request->data.data(),
        .capacity = request->data.count(),
    };
    size_t actual = 0u;
    zx_status_t status = stream_.writev_at(0, request->offset, &vec, 1, &actual);
    if (status != ZX_OK) {
      completer.ReplyError(status);
      return;
    }
    completer.ReplySuccess(actual);
  }

  void Seek(SeekRequestView request, SeekCompleter::Sync& completer) override {
    zx_off_t seek;
    if (zx_status_t status = stream_.seek(static_cast<zx_stream_seek_origin_t>(request->origin),
                                          request->offset, &seek);
        status != ZX_OK) {
      completer.ReplyError(status);
    } else {
      completer.ReplySuccess(seek);
    }
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
  void Init() override {
    ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &store_));
    const size_t kZero = 0u;
    ASSERT_OK(store_.set_property(ZX_PROP_VMO_CONTENT_SIZE, &kZero, sizeof(kZero)));
    ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_READ | ZX_STREAM_MODE_WRITE, store_, 0, &stream_));
  }

  void Describe2(Describe2Completer::Sync& completer) final {
    zx::stream stream;
    if (zx_status_t status = stream_.duplicate(ZX_RIGHT_SAME_RIGHTS, &stream); status != ZX_OK) {
      completer.Close(ZX_ERR_INTERNAL);
      return;
    }
    fidl::Arena alloc;
    completer.Reply(fio::wire::FileInfo::Builder(alloc).stream(std::move(stream)).Build());
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
    zx::result client_end = OpenConnection();
    if (client_end.is_error()) {
      return client_end.status_value();
    }
    return zxio_remote_init(&file_, zx::event{},
                            fidl::ClientEnd<fio::Node>(client_end.value().TakeChannel()),
                            /*is_tty=*/false);
  }
};

TEST_F(Remote, ReadWriteChannel) {
  ASSERT_NO_FAILURES(StartServer<TestServerChannel>());
  ASSERT_OK(OpenRemote());
  ASSERT_NO_FAILURES(FileTestSuite::ReadWrite(&file_.io));
}

}  // namespace
