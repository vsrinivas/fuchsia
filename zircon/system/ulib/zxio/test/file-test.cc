// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/ops.h>

#include <atomic>
#include <memory>

#include <zxtest/zxtest.h>

namespace {

class TestServer final : public llcpp::fuchsia::io::File::Interface {
 public:
  TestServer() = default;

  // Exercised by |zxio_close|.
  void Close(CloseCompleter::Sync completer) override {
    num_close_.fetch_add(1);
    completer.Reply(ZX_OK);
  }

  void Clone(uint32_t flags, zx::channel object, CloneCompleter::Sync completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Describe(DescribeCompleter::Sync completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Sync(SyncCompleter::Sync completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void GetAttr(GetAttrCompleter::Sync completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

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
  void SetUp() final {
    ASSERT_OK(zx::channel::create(0, &control_client_end_, &control_server_end_));
    ASSERT_OK(zx::event::create(0, &event_on_server_));
    ASSERT_OK(event_on_server_.duplicate(ZX_RIGHT_SAME_RIGHTS, &event_to_client_));
    ASSERT_OK(zxio_file_init(&file_, control_client_end_.release(), event_to_client_.release()));
    server_ = std::make_unique<TestServer>();
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    ASSERT_OK(loop_->StartThread("fake-filesystem"));
    ASSERT_OK(fidl::Bind(loop_->dispatcher(), std::move(control_server_end_), server_.get()));
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
  std::unique_ptr<TestServer> server_;
  std::unique_ptr<async::Loop> loop_;
};

TEST_F(File, WaitTimeOut) {
  zxio_signals_t observed = ZX_SIGNAL_NONE;
  ASSERT_EQ(ZX_ERR_TIMED_OUT,
            zxio_wait_one(&file_.io, ZXIO_SIGNAL_ALL, ZX_TIME_INFINITE_PAST, &observed));
  EXPECT_EQ(ZXIO_SIGNAL_NONE, observed);
}

TEST_F(File, WaitForReadable) {
  zxio_signals_t observed = ZX_SIGNAL_NONE;
  ASSERT_OK(event_on_server_.signal(ZX_SIGNAL_NONE, llcpp::fuchsia::io::FILE_SIGNAL_READABLE));
  ASSERT_OK(zxio_wait_one(&file_.io, ZXIO_SIGNAL_READABLE, ZX_TIME_INFINITE_PAST, &observed));
  EXPECT_EQ(ZXIO_SIGNAL_READABLE, observed);
}

TEST_F(File, WaitForWritable) {
  zxio_signals_t observed = ZX_SIGNAL_NONE;
  ASSERT_OK(event_on_server_.signal(ZX_SIGNAL_NONE, llcpp::fuchsia::io::FILE_SIGNAL_WRITABLE));
  ASSERT_OK(zxio_wait_one(&file_.io, ZXIO_SIGNAL_WRITABLE, ZX_TIME_INFINITE_PAST, &observed));
  EXPECT_EQ(ZXIO_SIGNAL_WRITABLE, observed);
}

}  // namespace
