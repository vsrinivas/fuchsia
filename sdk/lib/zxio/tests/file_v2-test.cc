// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io2/cpp/wire.h>
#include <fidl/fuchsia.io2/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/zx/stream.h>
#include <lib/zx/vmo.h>
#include <lib/zxio/cpp/inception.h>
#include <lib/zxio/ops.h>

#include <atomic>
#include <memory>

#include <zxtest/zxtest.h>

#include "sdk/lib/zxio/private.h"
#include "sdk/lib/zxio/tests/file_test_suite.h"

namespace {

namespace fio2 = fuchsia_io2;

class TestServerBase : public fio2::testing::File_TestBase {
 public:
  TestServerBase() = default;

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) final {
    ADD_FAILURE("unexpected message received: %s", name.c_str());
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  // Exercised by |zxio_close|.
  void Close(CloseRequestView request, CloseCompleter::Sync& completer) override {
    num_close_.fetch_add(1);
    completer.Close(ZX_OK);
  }

  void Describe(DescribeRequestView request, DescribeCompleter::Sync& completer) override {
    if (request->query == fio2::wire::ConnectionInfoQuery::kRepresentation) {
      fidl::Arena allocator;
      fio2::wire::ConnectionInfo info(allocator);
      info.set_representation(allocator,
                              fio2::wire::Representation::WithFile(allocator, allocator));
      completer.Reply(std::move(info));
      return;
    }
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  uint32_t num_close() const { return num_close_.load(); }

 private:
  std::atomic<uint32_t> num_close_ = 0;
};

class FileV2 : public zxtest::Test {
 public:
  template <typename ServerImpl>
  ServerImpl* StartServer() {
    zx_status_t status = ZX_OK;

    server_ = std::make_unique<ServerImpl>();
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    EXPECT_OK(status = loop_->StartThread("fake-filesystem"));
    if (status != ZX_OK) {
      return nullptr;
    }
    return static_cast<ServerImpl*>(server_.get());
  }

  zx_status_t OpenFile() {
    auto ends = fidl::CreateEndpoints<fio2::File>();
    if (!ends.is_ok()) {
      return ends.status_value();
    }

    auto status =
        fidl::BindSingleInFlightOnly(loop_->dispatcher(), std::move(ends->server), server_.get());
    if (status != ZX_OK) {
      return status;
    }

    auto result =
        fidl::WireCall(ends->client)->Describe(fio2::wire::ConnectionInfoQuery::kRepresentation);

    if (result.status() != ZX_OK) {
      return status;
    }

    zx::event observer;
    zx::stream stream;
    if (result->info.has_representation()) {
      EXPECT_TRUE(result->info.representation().is_file());
      fio2::wire::FileInfo& file = result->info.representation().mutable_file();
      if (file.has_observer()) {
        observer = std::move(file.observer());
      }
      if (file.has_stream()) {
        stream = std::move(file.stream());
      }
    }

    return zxio_file_v2_init(&file_, ends->client.TakeChannel().release(), observer.release(),
                             stream.release());
  }

  void TearDown() final {
    ASSERT_EQ(0, server_->num_close());
    ASSERT_OK(zxio_close(&file_.io));
    ASSERT_EQ(1, server_->num_close());
  }

 protected:
  zxio_storage_t file_;
  std::unique_ptr<TestServerBase> server_;
  std::unique_ptr<async::Loop> loop_;
};

class TestServerEvent final : public TestServerBase {
 public:
  TestServerEvent() { ASSERT_OK(zx::event::create(0, &observer_)); }

  const zx::event& observer() const { return observer_; }

  void Describe(DescribeRequestView request, DescribeCompleter::Sync& completer) override {
    if (request->query == fio2::wire::ConnectionInfoQuery::kRepresentation) {
      zx::event client_observer;
      zx_status_t status = observer_.duplicate(ZX_RIGHTS_BASIC, &client_observer);
      if (status != ZX_OK) {
        completer.Close(ZX_ERR_INTERNAL);
        return;
      }
      fidl::Arena allocator;
      fio2::wire::ConnectionInfo info(allocator);
      info.set_representation(allocator,
                              fio2::wire::Representation::WithFile(allocator, allocator));
      info.representation().mutable_file().set_observer(allocator, std::move(client_observer));
      completer.Reply(std::move(info));
      return;
    }
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

 private:
  zx::event observer_;
};

TEST_F(FileV2, WaitTimeOut) {
  ASSERT_NO_FAILURES(StartServer<TestServerEvent>());
  ASSERT_OK(OpenFile());
  zxio_signals_t observed = ZX_SIGNAL_NONE;
  ASSERT_STATUS(ZX_ERR_TIMED_OUT,
                zxio_wait_one(&file_.io, ZXIO_SIGNAL_ALL, ZX_TIME_INFINITE_PAST, &observed));
  EXPECT_EQ(ZXIO_SIGNAL_NONE, observed);
}

TEST_F(FileV2, WaitForReadable) {
  TestServerEvent* server = nullptr;
  ASSERT_NO_FAILURES(server = StartServer<TestServerEvent>());
  ASSERT_OK(OpenFile());
  zxio_signals_t observed = ZX_SIGNAL_NONE;
  // Signal readability on the server end.
  ASSERT_OK(server->observer().signal(
      ZX_SIGNAL_NONE, static_cast<zx_signals_t>(fio2::wire::FileSignal::kReadable)));
  ASSERT_OK(zxio_wait_one(&file_.io, ZXIO_SIGNAL_READABLE, ZX_TIME_INFINITE_PAST, &observed));
  EXPECT_EQ(ZXIO_SIGNAL_READABLE, observed);
}

TEST_F(FileV2, WaitForWritable) {
  TestServerEvent* server = nullptr;
  ASSERT_NO_FAILURES(server = StartServer<TestServerEvent>());
  ASSERT_OK(OpenFile());
  zxio_signals_t observed = ZX_SIGNAL_NONE;
  // Signal writability on the server end.
  ASSERT_OK(server->observer().signal(
      ZX_SIGNAL_NONE, static_cast<zx_signals_t>(fio2::wire::FileSignal::kWritable)));
  ASSERT_OK(zxio_wait_one(&file_.io, ZXIO_SIGNAL_WRITABLE, ZX_TIME_INFINITE_PAST, &observed));
  EXPECT_EQ(ZXIO_SIGNAL_WRITABLE, observed);
}

class TestServerChannel final : public TestServerBase {
 public:
  TestServerChannel() {
    ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &store_));
    const size_t kZero = 0u;
    ASSERT_OK(store_.set_property(ZX_PROP_VMO_CONTENT_SIZE, &kZero, sizeof(kZero)));
    ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_READ | ZX_STREAM_MODE_WRITE, store_, 0, &stream_));
  }

  void Read(ReadRequestView request, ReadCompleter::Sync& completer) override {
    if (request->count > fio2::wire::kMaxTransferSize) {
      completer.Close(ZX_ERR_OUT_OF_RANGE);
      return;
    }
    uint8_t buffer[fio2::wire::kMaxTransferSize];
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
    if (request->count > fio2::wire::kMaxTransferSize) {
      completer.Close(ZX_ERR_OUT_OF_RANGE);
      return;
    }
    uint8_t buffer[fio2::wire::kMaxTransferSize];
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
    if (request->data.count() > fio2::wire::kMaxTransferSize) {
      completer.Close(ZX_ERR_OUT_OF_RANGE);
      return;
    }
    zx_iovec_t vec = {
        .buffer = request->data.mutable_data(),
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
    if (request->data.count() > fio2::wire::kMaxTransferSize) {
      completer.Close(ZX_ERR_OUT_OF_RANGE);
      return;
    }
    zx_iovec_t vec = {
        .buffer = request->data.mutable_data(),
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
    zx_off_t seek = 0u;
    zx_status_t status =
        stream_.seek(static_cast<zx_stream_seek_origin_t>(request->origin), request->offset, &seek);
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

TEST_F(FileV2, ReadWriteChannel) {
  ASSERT_NO_FAILURES(StartServer<TestServerChannel>());
  ASSERT_OK(OpenFile());
  ASSERT_NO_FAILURES(FileTestSuite::ReadWrite(&file_.io));
}

class TestServerStream final : public TestServerBase {
 public:
  TestServerStream() {
    ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &store_));
    const size_t kZero = 0u;
    ASSERT_OK(store_.set_property(ZX_PROP_VMO_CONTENT_SIZE, &kZero, sizeof(kZero)));
    ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_READ | ZX_STREAM_MODE_WRITE, store_, 0, &stream_));
  }

  void Describe(DescribeRequestView request, DescribeCompleter::Sync& completer) override {
    if (request->query == fio2::wire::ConnectionInfoQuery::kRepresentation) {
      zx::stream client_stream;
      zx_status_t status = stream_.duplicate(ZX_RIGHT_SAME_RIGHTS, &client_stream);
      if (status != ZX_OK) {
        completer.Close(ZX_ERR_INTERNAL);
        return;
      }
      fidl::Arena allocator;
      fio2::wire::ConnectionInfo info(allocator);
      info.set_representation(allocator,
                              fio2::wire::Representation::WithFile(allocator, allocator));
      info.representation().mutable_file().set_stream(allocator, std::move(client_stream));
      completer.Reply(std::move(info));
      return;
    }
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

 private:
  zx::vmo store_;
  zx::stream stream_;
};

TEST_F(FileV2, ReadWriteStream) {
  ASSERT_NO_FAILURES(StartServer<TestServerStream>());
  ASSERT_OK(OpenFile());
  ASSERT_NO_FAILURES(FileTestSuite::ReadWrite(&file_.io));
}

}  // namespace
