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

#include "file_test_suite.h"

namespace {

namespace fio2 = ::llcpp::fuchsia::io2;

class TestServerBase : public fio2::File::Interface {
 public:
  TestServerBase() = default;

  // Exercised by |zxio_close|.
  void Close(CloseCompleter::Sync& completer) override {
    num_close_.fetch_add(1);
    completer.Close(ZX_OK);
  }

  void Reopen(fio2::ConnectionOptions options, ::zx::channel object_request,
              ReopenCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Describe(fio2::ConnectionInfoQuery query, DescribeCompleter::Sync& completer) override {
    if (query == fio2::ConnectionInfoQuery::REPRESENTATION) {
      auto file_info_builder = fio2::FileInfo::UnownedBuilder();
      fio2::FileInfo file_info = file_info_builder.build();
      auto representation = fio2::Representation::WithFile(fidl::unowned_ptr(&file_info));
      auto info_builder = fio2::ConnectionInfo::UnownedBuilder();
      info_builder.set_representation(fidl::unowned_ptr(&representation));
      completer.Reply(info_builder.build());
      return;
    }
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void GetToken(GetTokenCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void GetAttributes(fio2::NodeAttributesQuery query,
                     GetAttributesCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void UpdateAttributes(fio2::NodeAttributes attributes,
                        UpdateAttributesCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Sync(SyncCompleter::Sync& completer) override { completer.Close(ZX_ERR_NOT_SUPPORTED); }

  void Read(uint64_t count, ReadCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void ReadAt(uint64_t count, uint64_t offset, ReadAtCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Write(fidl::VectorView<uint8_t> data, WriteCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void WriteAt(fidl::VectorView<uint8_t> data, uint64_t offset,
               WriteAtCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Seek(fio2::SeekOrigin origin, int64_t offset, SeekCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Resize(uint64_t length, ResizeCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void GetMemRange(fio2::VmoFlags flags, GetMemRangeCompleter::Sync& completer) override {
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
    zx::channel client_end, server_end;
    zx_status_t status = zx::channel::create(0, &client_end, &server_end);
    if (status != ZX_OK) {
      return status;
    }

    status =
        fidl::BindSingleInFlightOnly(loop_->dispatcher(), std::move(server_end), server_.get());
    if (status != ZX_OK) {
      return status;
    }

    auto result =
        fio2::File::Call::Describe(client_end.borrow(), fio2::ConnectionInfoQuery::REPRESENTATION);

    if (result.status() != ZX_OK) {
      return status;
    }

    zx::event observer;
    zx::stream stream;
    if (result->info.has_representation()) {
      EXPECT_TRUE(result->info.representation().is_file());
      fio2::FileInfo& file = result->info.representation().mutable_file();
      if (file.has_observer()) {
        observer = std::move(file.observer());
      }
      if (file.has_stream()) {
        stream = std::move(file.stream());
      }
    }

    return zxio_file_v2_init(&file_, client_end.release(), observer.release(), stream.release());
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

  void Describe(fio2::ConnectionInfoQuery query, DescribeCompleter::Sync& completer) override {
    if (query == fio2::ConnectionInfoQuery::REPRESENTATION) {
      auto file_info_builder = fio2::FileInfo::UnownedBuilder();
      zx::event client_observer;
      zx_status_t status = observer_.duplicate(ZX_RIGHTS_BASIC, &client_observer);
      if (status != ZX_OK) {
        completer.Close(ZX_ERR_INTERNAL);
        return;
      }
      file_info_builder.set_observer(fidl::unowned_ptr(&client_observer));
      fio2::FileInfo file_info = file_info_builder.build();
      auto representation = fio2::Representation::WithFile(fidl::unowned_ptr(&file_info));
      auto info_builder = fio2::ConnectionInfo::UnownedBuilder();
      info_builder.set_representation(fidl::unowned_ptr(&representation));
      completer.Reply(info_builder.build());
      return;
    }
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

 private:
  zx::event observer_;
};

TEST_F(FileV2, WaitTimeOut) {
  TestServerEvent* server = nullptr;
  ASSERT_NO_FAILURES(server = StartServer<TestServerEvent>());
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
  ASSERT_OK(server->observer().signal(ZX_SIGNAL_NONE,
                                      static_cast<zx_signals_t>(fio2::FileSignal::READABLE)));
  ASSERT_OK(zxio_wait_one(&file_.io, ZXIO_SIGNAL_READABLE, ZX_TIME_INFINITE_PAST, &observed));
  EXPECT_EQ(ZXIO_SIGNAL_READABLE, observed);
}

TEST_F(FileV2, WaitForWritable) {
  TestServerEvent* server = nullptr;
  ASSERT_NO_FAILURES(server = StartServer<TestServerEvent>());
  ASSERT_OK(OpenFile());
  zxio_signals_t observed = ZX_SIGNAL_NONE;
  // Signal writability on the server end.
  ASSERT_OK(server->observer().signal(ZX_SIGNAL_NONE,
                                      static_cast<zx_signals_t>(fio2::FileSignal::WRITABLE)));
  ASSERT_OK(zxio_wait_one(&file_.io, ZXIO_SIGNAL_WRITABLE, ZX_TIME_INFINITE_PAST, &observed));
  EXPECT_EQ(ZXIO_SIGNAL_WRITABLE, observed);
}

class TestServerChannel final : public TestServerBase {
 public:
  TestServerChannel() {
    ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &store_));
    ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_READ | ZX_STREAM_MODE_WRITE, store_, 0, &stream_));
  }

  void Read(uint64_t count, ReadCompleter::Sync& completer) override {
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
    completer.ReplySuccess(fidl::VectorView(fidl::unowned_ptr(buffer), actual));
  }

  void ReadAt(uint64_t count, uint64_t offset, ReadAtCompleter::Sync& completer) override {
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
    completer.ReplySuccess(fidl::VectorView(fidl::unowned_ptr(buffer), actual));
  }

  void Write(fidl::VectorView<uint8_t> data, WriteCompleter::Sync& completer) override {
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
               WriteAtCompleter::Sync& completer) override {
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

  void Seek(fio2::SeekOrigin origin, int64_t offset, SeekCompleter::Sync& completer) override {
    zx_off_t seek = 0u;
    zx_status_t status = stream_.seek(static_cast<zx_stream_seek_origin_t>(origin), offset, &seek);
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

  void Describe(fio2::ConnectionInfoQuery query, DescribeCompleter::Sync& completer) override {
    if (query == fio2::ConnectionInfoQuery::REPRESENTATION) {
      auto file_info_builder = fio2::FileInfo::UnownedBuilder();
      zx::stream client_stream;
      zx_status_t status = stream_.duplicate(ZX_RIGHT_SAME_RIGHTS, &client_stream);
      if (status != ZX_OK) {
        completer.Close(ZX_ERR_INTERNAL);
        return;
      }
      file_info_builder.set_stream(fidl::unowned_ptr(&client_stream));
      fio2::FileInfo file_info = file_info_builder.build();
      auto representation = fio2::Representation::WithFile(fidl::unowned_ptr(&file_info));
      auto info_builder = fio2::ConnectionInfo::UnownedBuilder();
      info_builder.set_representation(fidl::unowned_ptr(&representation));
      completer.Reply(info_builder.build());
      return;
    }
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

 private:
  zx::vmo store_;
  zx::stream stream_;
};

TEST_F(FileV2, ReadWriteStream) {
  TestServerStream* server = nullptr;
  ASSERT_NO_FAILURES(server = StartServer<TestServerStream>());
  ASSERT_OK(OpenFile());
  ASSERT_NO_FAILURES(FileTestSuite::ReadWrite(&file_.io));
}

}  // namespace
