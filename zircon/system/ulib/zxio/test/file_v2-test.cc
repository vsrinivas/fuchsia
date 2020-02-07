// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io2/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/zx/stream.h>
#include <lib/zx/vmo.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/ops.h>

#include <atomic>
#include <memory>

#include <zxtest/zxtest.h>

namespace {

namespace fio2 = ::llcpp::fuchsia::io2;

class TestServerBase : public fio2::File::Interface {
 public:
  TestServerBase() = default;

  // Exercised by |zxio_close|.
  void Close(CloseCompleter::Sync completer) override {
    num_close_.fetch_add(1);
    completer.Close(ZX_OK);
  }

  void Reopen(fio2::ConnectionOptions options, ::zx::channel object_request,
              ReopenCompleter::Sync completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Describe(fio2::ConnectionInfoQuery query, DescribeCompleter::Sync completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void GetToken(GetTokenCompleter::Sync completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void GetAttributes(fio2::NodeAttributesQuery query,
                     GetAttributesCompleter::Sync completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void UpdateAttributes(fio2::NodeAttributes attributes,
                        UpdateAttributesCompleter::Sync completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Sync(SyncCompleter::Sync completer) override { completer.Close(ZX_ERR_NOT_SUPPORTED); }

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

  void Seek(fio2::SeekOrigin origin, int64_t offset, SeekCompleter::Sync completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Resize(uint64_t length, ResizeCompleter::Sync completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void GetMemRange(fio2::VmoFlags flags, GetMemRangeCompleter::Sync completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  uint32_t num_close() const { return num_close_.load(); }

 private:
  std::atomic<uint32_t> num_close_ = 0;
};

class FileV2 : public zxtest::Test {
 public:
  void SetUp() final {
    ASSERT_OK(zx::channel::create(0, &control_client_end_, &control_server_end_));
    ASSERT_OK(zx::event::create(0, &event_on_server_));
    ASSERT_OK(event_on_server_.duplicate(ZX_RIGHT_SAME_RIGHTS, &event_to_client_));
    ASSERT_OK(zxio_file_v2_init(&file_, control_client_end_.release(), event_to_client_.release()));
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
    EXPECT_OK(status =
                  fidl::Bind(loop_->dispatcher(), std::move(control_server_end_), server_.get()));
    if (status != ZX_OK) {
      return nullptr;
    }
    return static_cast<ServerImpl*>(server_.get());
  }

  void TearDown() final {
    ASSERT_EQ(0, server_->num_close());
    ASSERT_OK(zxio_close(&file_.io));
    ASSERT_EQ(1, server_->num_close());
  }

 protected:
  zxio_storage_t file_;
  zx::channel control_client_end_;
  zx::channel control_server_end_;
  zx::event event_on_server_;
  zx::event event_to_client_;
  std::unique_ptr<TestServerBase> server_;
  std::unique_ptr<async::Loop> loop_;
};

TEST_F(FileV2, WaitTimeOut) {
  ASSERT_NO_FAILURES(StartServer<TestServerBase>());
  zxio_signals_t observed = ZX_SIGNAL_NONE;
  ASSERT_STATUS(ZX_ERR_TIMED_OUT,
                zxio_wait_one(&file_.io, ZXIO_SIGNAL_ALL, ZX_TIME_INFINITE_PAST, &observed));
  EXPECT_EQ(ZXIO_SIGNAL_NONE, observed);
}

TEST_F(FileV2, WaitForReadable) {
  ASSERT_NO_FAILURES(StartServer<TestServerBase>());
  zxio_signals_t observed = ZX_SIGNAL_NONE;
  // Signal readability on the server end.
  ASSERT_OK(event_on_server_.signal(ZX_SIGNAL_NONE,
                                    static_cast<zx_signals_t>(fio2::FileSignal::READABLE)));
  ASSERT_OK(zxio_wait_one(&file_.io, ZXIO_SIGNAL_READABLE, ZX_TIME_INFINITE_PAST, &observed));
  EXPECT_EQ(ZXIO_SIGNAL_READABLE, observed);
}

TEST_F(FileV2, WaitForWritable) {
  ASSERT_NO_FAILURES(StartServer<TestServerBase>());
  zxio_signals_t observed = ZX_SIGNAL_NONE;
  // Signal writability on the server end.
  ASSERT_OK(event_on_server_.signal(ZX_SIGNAL_NONE,
                                    static_cast<zx_signals_t>(fio2::FileSignal::WRITABLE)));
  ASSERT_OK(zxio_wait_one(&file_.io, ZXIO_SIGNAL_WRITABLE, ZX_TIME_INFINITE_PAST, &observed));
  EXPECT_EQ(ZXIO_SIGNAL_WRITABLE, observed);
}

constexpr zx_stream_seek_origin_t ToZXStreamSeekOrigin(fio2::SeekOrigin whence) {
  return static_cast<zx_stream_seek_origin_t>(whence) - 1;
}

class TestServerStream final : public TestServerBase {
 public:
  // The storage_size must be a multiple of PAGE_SIZE.
  zx_status_t Initialize(size_t storage_size) {
    zx_status_t status = zx::vmo::create(storage_size, 0, &store_);
    if (status != ZX_OK) {
      return status;
    }
    return zx::stream::create(ZX_STREAM_MODE_READ | ZX_STREAM_MODE_WRITE, store_, 0, &stream_);
  }

  void Read(uint64_t count, ReadCompleter::Sync completer) override {
    if (count > fio2::MAX_TRANSFER_SIZE) {
      completer.Close(ZX_ERR_OUT_OF_RANGE);
      return;
    }
    uint8_t buffer[fio2::MAX_TRANSFER_SIZE];
    zx_iovec_t vec = {
        .buffer = buffer,
        .capacity = count,
    };
    size_t actual = 0u;
    zx_status_t status = stream_.readv(0, &vec, 1, &actual);
    if (status != ZX_OK) {
      completer.ReplyError(status);
      return;
    }
    completer.ReplySuccess(fidl::VectorView(buffer, actual));
  }

  void ReadAt(uint64_t count, uint64_t offset, ReadAtCompleter::Sync completer) override {
    if (count > fio2::MAX_TRANSFER_SIZE) {
      completer.Close(ZX_ERR_OUT_OF_RANGE);
      return;
    }
    uint8_t buffer[fio2::MAX_TRANSFER_SIZE];
    zx_iovec_t vec = {
        .buffer = buffer,
        .capacity = count,
    };
    size_t actual = 0u;
    zx_status_t status = stream_.readv_at(0, offset, &vec, 1, &actual);
    if (status != ZX_OK) {
      completer.ReplyError(status);
      return;
    }
    completer.ReplySuccess(fidl::VectorView(buffer, actual));
  }

  void Write(fidl::VectorView<uint8_t> data, WriteCompleter::Sync completer) override {
    if (data.count() > fio2::MAX_TRANSFER_SIZE) {
      completer.Close(ZX_ERR_OUT_OF_RANGE);
      return;
    }
    zx_iovec_t vec = {
        .buffer = data.mutable_data(),
        .capacity = data.count(),
    };
    size_t actual = 0u;
    zx_status_t status = stream_.writev(0, &vec, 1, &actual);
    if (status != ZX_OK) {
      completer.ReplyError(status);
      return;
    }
    completer.ReplySuccess(actual);
  }

  void WriteAt(fidl::VectorView<uint8_t> data, uint64_t offset,
               WriteAtCompleter::Sync completer) override {
    if (data.count() > fio2::MAX_TRANSFER_SIZE) {
      completer.Close(ZX_ERR_OUT_OF_RANGE);
      return;
    }
    zx_iovec_t vec = {
        .buffer = data.mutable_data(),
        .capacity = data.count(),
    };
    size_t actual = 0u;
    zx_status_t status = stream_.writev_at(0, offset, &vec, 1, &actual);
    if (status != ZX_OK) {
      completer.ReplyError(status);
      return;
    }
    completer.ReplySuccess(actual);
  }

  void Seek(fio2::SeekOrigin origin, int64_t offset, SeekCompleter::Sync completer) override {
    zx_off_t seek = 0u;
    static_assert(ToZXStreamSeekOrigin(fio2::SeekOrigin::START) == ZX_STREAM_SEEK_ORIGIN_START,
                  "ToZXStreamSeekOrigin should work for START");
    static_assert(ToZXStreamSeekOrigin(fio2::SeekOrigin::CURRENT) == ZX_STREAM_SEEK_ORIGIN_CURRENT,
                  "ToZXStreamSeekOrigin should work for CURRENT");
    static_assert(ToZXStreamSeekOrigin(fio2::SeekOrigin::END) == ZX_STREAM_SEEK_ORIGIN_END,
                  "ToZXStreamSeekOrigin should work for END");
    zx_status_t status = stream_.seek(ToZXStreamSeekOrigin(origin), offset, &seek);
    if (status != ZX_OK) {
      completer.ReplyError(status);
      return;
    }
    completer.ReplySuccess(seek);
  }

 private:
  zx::vmo store_;
  zx::stream stream_;
};

TEST_F(FileV2, ReadWrite) {
  TestServerStream* server = nullptr;
  ASSERT_NO_FAILURES(server = StartServer<TestServerStream>());
  server->Initialize(ZX_PAGE_SIZE);

  size_t actual = 0u;
  ASSERT_OK(zxio_write(&file_.io, "abcd", 4, 0, &actual));
  EXPECT_EQ(actual, 4u);

  size_t seek = 0;
  ASSERT_OK(zxio_seek(&file_.io, ZXIO_SEEK_ORIGIN_CURRENT, -2, &seek));
  EXPECT_EQ(2u, seek);

  char buffer[1024] = {};
  actual = 0u;
  ASSERT_OK(zxio_read(&file_.io, buffer, 1024, 0, &actual));
  EXPECT_EQ(actual, 2u);
  EXPECT_STR_EQ("cd", buffer);
  memset(buffer, 0, sizeof(buffer));

  actual = 2;
  ASSERT_OK(zxio_write_at(&file_.io, 1, "xy", 2, 0, &actual));
  EXPECT_EQ(actual, 2u);

  actual = 0u;
  ASSERT_OK(zxio_read_at(&file_.io, 1, buffer, 1024, 0, &actual));
  EXPECT_EQ(actual, 3u);
  EXPECT_STR_EQ("xyd", buffer);
  memset(buffer, 0, sizeof(buffer));
}

}  // namespace
