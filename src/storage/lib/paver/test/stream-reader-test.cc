// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/stream-reader.h"

#include <fcntl.h>
#include <fidl/fuchsia.paver/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>

#include <zxtest/zxtest.h>

#include "zircon/errors.h"

namespace {

constexpr char kFileData[] = "lalalala";

TEST(StreamReaderTest, InvalidChannel) { ASSERT_NOT_OK(paver::StreamReader::Create({})); }

class FakePayloadStream : public fidl::WireServer<fuchsia_paver::PayloadStream> {
 public:
  FakePayloadStream() : loop_(&kAsyncLoopConfigAttachToCurrentThread) {
    zx::result server = fidl::CreateEndpoints(&client_);
    ASSERT_OK(server.status_value());
    fidl::BindSingleInFlightOnly(loop_.dispatcher(), std::move(server.value()), this);
    loop_.StartThread("payload-stream-test-loop");
  }

  void ReadSuccess(ReadDataCompleter::Sync& completer) {
    vmo_.write(kFileData, 0, sizeof(kFileData));

    fuchsia_paver::wire::ReadInfo info{.offset = 0, .size = sizeof(kFileData)};
    completer.Reply(fuchsia_paver::wire::ReadResult::WithInfo(
        fidl::ObjectView<fuchsia_paver::wire::ReadInfo>::FromExternal(&info)));
  }

  static void ReadError(ReadDataCompleter::Sync& completer) {
    completer.Reply(fuchsia_paver::wire::ReadResult::WithErr(ZX_ERR_INTERNAL));
  }

  static void ReadEof(ReadDataCompleter::Sync& completer) {
    completer.Reply(fuchsia_paver::wire::ReadResult::WithEof(true));
  }

  void ReadData(ReadDataCompleter::Sync& completer) override {
    if (!vmo_) {
      completer.Reply(fuchsia_paver::wire::ReadResult::WithErr(ZX_ERR_BAD_STATE));
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

  void RegisterVmo(RegisterVmoRequestView request, RegisterVmoCompleter::Sync& completer) override {
    vmo_ = std::move(request->vmo);
    completer.Reply(ZX_OK);
  }

  fidl::ClientEnd<fuchsia_paver::PayloadStream> client() { return std::move(client_); }

  void ReturnErr() { return_err_ = true; }
  void ReturnEof() { return_eof_ = true; }

 private:
  fidl::ClientEnd<fuchsia_paver::PayloadStream> client_;
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
