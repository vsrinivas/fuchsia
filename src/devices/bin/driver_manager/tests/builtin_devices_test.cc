// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/builtin_devices.h"

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/fd.h>
#include <lib/fidl/llcpp/connect_service.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "lib/async-loop/loop.h"

namespace {

namespace fio = fuchsia_io;

class BuiltinDevicesTest : public zxtest::Test {
 public:
  void SetUp() override {
    ASSERT_OK(loop_.StartThread("builtin-devices"));
    builtin_ = BuiltinDevices::Get(loop_.dispatcher());
  }

  void TearDown() override {
    loop_.Shutdown();
    BuiltinDevices::Reset();
  }

 protected:
  async::Loop loop_{&kAsyncLoopConfigNeverAttachToThread};
  BuiltinDevices* builtin_;
};

TEST_F(BuiltinDevicesTest, OpenDevice) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Node>();
  ASSERT_OK(endpoints.status_value());
  ASSERT_OK(builtin_->HandleOpen(fio::OpenFlags(), std::move(endpoints->server), kNullDevName));
}

TEST_F(BuiltinDevicesTest, ReadZero) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Node>();
  ASSERT_OK(endpoints.status_value());
  ASSERT_OK(builtin_->HandleOpen(
      fio::wire::OpenFlags::kNotDirectory | fio::wire::OpenFlags::kRightReadable,
      std::move(endpoints->server), kZeroDevName));

  fbl::unique_fd fd;
  ASSERT_OK(fdio_fd_create(endpoints->client.TakeChannel().release(), fd.reset_and_get_address()));

  std::array<uint8_t, 100> buffer;
  buffer.fill(0x1);
  EXPECT_EQ(read(fd.get(), buffer.data(), buffer.size()), buffer.size());
  EXPECT_TRUE(std::all_of(buffer.begin(), buffer.end(), [](uint8_t b) { return b == 0; }));
}

TEST_F(BuiltinDevicesTest, WriteZero) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Node>();
  ASSERT_OK(endpoints.status_value());
  ASSERT_OK(builtin_->HandleOpen(
      fio::wire::OpenFlags::kNotDirectory | fio::wire::OpenFlags::kRightReadable,
      std::move(endpoints->server), kZeroDevName));

  fbl::unique_fd fd;
  ASSERT_OK(fdio_fd_create(endpoints->client.TakeChannel().release(), fd.reset_and_get_address()));

  std::array<uint8_t, 100> buffer;
  buffer.fill(0x1);
  // Write fails.
  EXPECT_LT(write(fd.get(), buffer.data(), buffer.size()), 0);
}

TEST_F(BuiltinDevicesTest, ReadNull) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Node>();
  ASSERT_OK(endpoints.status_value());
  ASSERT_OK(builtin_->HandleOpen(
      fio::wire::OpenFlags::kNotDirectory | fio::wire::OpenFlags::kRightReadable,
      std::move(endpoints->server), kNullDevName));

  fbl::unique_fd fd;
  ASSERT_OK(fdio_fd_create(endpoints->client.TakeChannel().release(), fd.reset_and_get_address()));

  std::array<uint8_t, 100> buffer;
  buffer.fill(0x1);
  // Read will fail to read any bytes.
  EXPECT_EQ(read(fd.get(), buffer.data(), buffer.size()), 0);
  // Buffer is unchanged.
  EXPECT_TRUE(std::all_of(buffer.begin(), buffer.end(), [](uint8_t b) { return b == 0x1; }));
}

TEST_F(BuiltinDevicesTest, WriteNull) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Node>();
  ASSERT_OK(endpoints.status_value());
  ASSERT_OK(builtin_->HandleOpen(
      fio::wire::OpenFlags::kNotDirectory | fio::wire::OpenFlags::kRightWritable,
      std::move(endpoints->server), kNullDevName));

  fbl::unique_fd fd;
  ASSERT_OK(fdio_fd_create(endpoints->client.TakeChannel().release(), fd.reset_and_get_address()));

  std::array<uint8_t, 100> buffer;
  buffer.fill(0x1);
  EXPECT_EQ(write(fd.get(), buffer.data(), buffer.size()), buffer.size());
}

}  // namespace
