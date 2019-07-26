// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "payload-streamer.h"

#include <optional>

#include <fcntl.h>

#include <lib/async-loop/cpp/loop.h>
#include <zxtest/zxtest.h>

namespace {

constexpr char kFileData[] = "lalalala";

TEST(PayloadStreamerTest, TrivialLifetime) {
  disk_pave::PayloadStreamer streamer(zx::channel, fbl::unique_fd);
}

class PayloadStreamerTest : public zxtest::Test {
 public:
  PayloadStreamerTest() : loop_(&kAsyncLoopConfigAttachToThread) {
    mktemp(tempfile_name_);
    ASSERT_NE(strlen(tempfile_name_), 0);

    fbl::unique_fd src(open(tempfile_name_, O_RDWR | O_CREAT));
    ASSERT_EQ(write(src.get(), kFileData, sizeof(kFileData)), sizeof(kFileData));
    lseek(src.get(), 0, SEEK_SET);

    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    streamer_.emplace(std::move(server), std::move(src));
    client_.emplace(std::move(client));
    loop_.StartThread("payload-stream-test-loop");
  }

  ~PayloadStreamerTest() { unlink(tempfile_name_); }

 protected:
  async::Loop loop_;
  std::optional<disk_pave::PayloadStreamer> streamer_;
  std::optional<::llcpp::fuchsia::paver::PayloadStream::SyncClient> client_;

 private:
  char tempfile_name_[20] = "/tmp/payload.XXXXXX";
};

TEST_F(PayloadStreamerTest, RegisterVmo) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo));
  zx_status_t status;
  ASSERT_OK(client_->RegisterVmo_Deprecated(std::move(vmo), &status));
  EXPECT_OK(status);
}

TEST_F(PayloadStreamerTest, RegisterInvalidVmo) {
  zx_status_t status;
  EXPECT_NE(client_->RegisterVmo_Deprecated(zx::vmo(), &status), ZX_OK);
}

TEST_F(PayloadStreamerTest, ReadNoVmoRegistered) {
  ::llcpp::fuchsia::paver::ReadResult result;
  ASSERT_OK(client_->ReadData_Deprecated(&result));
  ASSERT_TRUE(result.is_err());
  EXPECT_NE(result.err(), ZX_OK);
}

TEST_F(PayloadStreamerTest, ReadData) {
  zx::vmo vmo, dup;
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo));
  ASSERT_OK(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
  zx_status_t status;
  ASSERT_OK(client_->RegisterVmo_Deprecated(std::move(dup), &status));
  EXPECT_OK(status);

  ::llcpp::fuchsia::paver::ReadResult result;
  ASSERT_OK(client_->ReadData_Deprecated(&result));
  ASSERT_TRUE(result.is_info());

  char buffer[sizeof(kFileData)] = {};
  ASSERT_EQ(result.info().size, sizeof(buffer));
  ASSERT_OK(vmo.read(buffer, result.info().offset, result.info().size));
  ASSERT_EQ(memcmp(kFileData, buffer, sizeof(buffer)), 0);
}

TEST_F(PayloadStreamerTest, ReadEof) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo));
  zx_status_t status;
  ASSERT_OK(client_->RegisterVmo_Deprecated(std::move(vmo), &status));
  EXPECT_OK(status);

  ::llcpp::fuchsia::paver::ReadResult result;
  ASSERT_OK(client_->ReadData_Deprecated(&result));
  ASSERT_TRUE(result.is_info());

  ASSERT_OK(client_->ReadData_Deprecated(&result));
  ASSERT_TRUE(result.is_eof());

  ASSERT_OK(client_->ReadData_Deprecated(&result));
  ASSERT_TRUE(result.is_eof());
}

}  // namespace
