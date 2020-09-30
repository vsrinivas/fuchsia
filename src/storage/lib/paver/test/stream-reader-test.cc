// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/stream-reader.h"

#include <fcntl.h>
#include <fuchsia/paver/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>

#include <zxtest/zxtest.h>

namespace {

constexpr char kFileData[] = "lalalala";

TEST(StreamReaderTest, InvalidChannel) {
  ASSERT_NOT_OK(paver::StreamReader::Create(zx::channel()));
}

class FakePayloadStream : public ::llcpp::fuchsia::paver::PayloadStream::Interface {
 public:
  FakePayloadStream() : loop_(&kAsyncLoopConfigAttachToCurrentThread) {
    zx::channel server;
    ASSERT_OK(zx::channel::create(0, &client_, &server));
    fidl::BindSingleInFlightOnly(loop_.dispatcher(), std::move(server), this);
    loop_.StartThread("payload-stream-test-loop");
  }

  void ReadSuccess(ReadDataCompleter::Sync& completer) {
    vmo_.write(kFileData, 0, sizeof(kFileData));

    ::llcpp::fuchsia::paver::ReadInfo info{.offset = 0, .size = sizeof(kFileData)};
    completer.Reply(::llcpp::fuchsia::paver::ReadResult::WithInfo(fidl::unowned_ptr(&info)));
  }

  void ReadError(ReadDataCompleter::Sync& completer) {
    ::llcpp::fuchsia::paver::ReadResult result;
    zx_status_t status = ZX_ERR_INTERNAL;
    result.set_err(fidl::unowned_ptr(&status));

    completer.Reply(std::move(result));
  }

  void ReadEof(ReadDataCompleter::Sync& completer) {
    ::llcpp::fuchsia::paver::ReadResult result;
    fidl::aligned<bool> eof = true;
    result.set_eof(fidl::unowned_ptr(&eof));

    completer.Reply(std::move(result));
  }

  void ReadData(ReadDataCompleter::Sync& completer) {
    if (!vmo_) {
      ::llcpp::fuchsia::paver::ReadResult result;
      zx_status_t status = ZX_ERR_BAD_STATE;
      result.set_err(fidl::unowned_ptr(&status));
      completer.Reply(std::move(result));
      return;
    }

    if (return_err_) {
      ReadError(completer);
    } else if (return_eof_) {
      ReadEof(completer);
    } else {
      ReadSuccess(completer);
    }
  }

  void RegisterVmo(zx::vmo vmo, RegisterVmoCompleter::Sync& completer) {
    vmo_ = std::move(vmo);
    completer.Reply(ZX_OK);
  }

  zx::channel client() { return std::move(client_); }

  void ReturnErr() { return_err_ = true; }
  void ReturnEof() { return_eof_ = true; }

 private:
  zx::channel client_;
  async::Loop loop_;
  zx::vmo vmo_;

  bool return_err_ = false;
  bool return_eof_ = false;
};

class StreamReaderTest : public zxtest::Test {
 protected:
  FakePayloadStream stream_;
};

TEST_F(StreamReaderTest, Create) { ASSERT_OK(paver::StreamReader::Create(stream_.client())); }

TEST_F(StreamReaderTest, ReadError) {
  auto status = paver::StreamReader::Create(stream_.client());
  ASSERT_OK(status);
  std::unique_ptr<paver::StreamReader> reader = std::move(status.value());

  stream_.ReturnErr();

  char buffer[sizeof(kFileData)] = {};
  size_t actual;
  ASSERT_NE(reader->Read(buffer, sizeof(buffer), &actual), ZX_OK);
}

TEST_F(StreamReaderTest, ReadEof) {
  auto status = paver::StreamReader::Create(stream_.client());
  ASSERT_OK(status);
  std::unique_ptr<paver::StreamReader> reader = std::move(status.value());

  stream_.ReturnEof();

  char buffer[sizeof(kFileData)] = {};
  size_t actual;
  ASSERT_OK(reader->Read(buffer, sizeof(buffer), &actual));
  ASSERT_EQ(actual, 0);
}

TEST_F(StreamReaderTest, ReadSingle) {
  auto status = paver::StreamReader::Create(stream_.client());
  ASSERT_OK(status);
  std::unique_ptr<paver::StreamReader> reader = std::move(status.value());

  char buffer[sizeof(kFileData)] = {};
  size_t actual;
  ASSERT_OK(reader->Read(buffer, sizeof(buffer), &actual));
  ASSERT_EQ(actual, sizeof(buffer));
  ASSERT_EQ(memcmp(buffer, kFileData, sizeof(buffer)), 0);

  stream_.ReturnEof();

  ASSERT_OK(reader->Read(buffer, sizeof(buffer), &actual));
  ASSERT_EQ(actual, 0);
}

TEST_F(StreamReaderTest, ReadMultiple) {
  auto status = paver::StreamReader::Create(stream_.client());
  ASSERT_OK(status);
  std::unique_ptr<paver::StreamReader> reader = std::move(status.value());

  char buffer[sizeof(kFileData)] = {};
  size_t actual;
  ASSERT_OK(reader->Read(buffer, sizeof(buffer), &actual));
  ASSERT_EQ(actual, sizeof(buffer));
  ASSERT_EQ(memcmp(buffer, kFileData, sizeof(buffer)), 0);

  ASSERT_OK(reader->Read(buffer, sizeof(buffer), &actual));
  ASSERT_EQ(actual, sizeof(buffer));
  ASSERT_EQ(memcmp(buffer, kFileData, sizeof(buffer)), 0);

  stream_.ReturnEof();

  ASSERT_OK(reader->Read(buffer, sizeof(buffer), &actual));
  ASSERT_EQ(actual, 0);
}

TEST_F(StreamReaderTest, ReadPartial) {
  auto status = paver::StreamReader::Create(stream_.client());
  ASSERT_OK(status);
  std::unique_ptr<paver::StreamReader> reader = std::move(status.value());

  constexpr size_t kBufferSize = sizeof(kFileData) - 3;
  char buffer[kBufferSize] = {};
  size_t actual;
  ASSERT_OK(reader->Read(buffer, sizeof(buffer), &actual));
  ASSERT_EQ(actual, sizeof(buffer));
  ASSERT_EQ(memcmp(buffer, kFileData, sizeof(buffer)), 0);

  stream_.ReturnEof();

  ASSERT_OK(reader->Read(buffer, sizeof(buffer), &actual));
  ASSERT_EQ(actual, 3);
  ASSERT_EQ(memcmp(buffer, kFileData + kBufferSize, 3), 0);

  ASSERT_OK(reader->Read(buffer, sizeof(buffer), &actual));
  ASSERT_EQ(actual, 0);
}

}  // namespace
