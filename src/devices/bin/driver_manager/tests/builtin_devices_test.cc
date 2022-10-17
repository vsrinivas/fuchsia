// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/builtin_devices.h"

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/fd.h>
#include <lib/fidl/cpp/wire/connect_service.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "lib/async-loop/loop.h"
#include "src/lib/storage/vfs/cpp/managed_vfs.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"

namespace {

namespace fio = fuchsia_io;

class BuiltinDevicesTest : public zxtest::Test {
 public:
  void SetUp() override {
    zx::result server = fidl::CreateEndpoints(&client_);
    ASSERT_OK(server.status_value());
    ASSERT_OK(dir_->AddEntry(kNullDevName, fbl::MakeRefCounted<BuiltinDevVnode>(true)));
    ASSERT_OK(dir_->AddEntry(kZeroDevName, fbl::MakeRefCounted<BuiltinDevVnode>(false)));
    ASSERT_OK(vfs_.ServeDirectory(dir_, std::move(server.value())));
    ASSERT_OK(loop_.StartThread("builtin-devices"));
  }

  zx::result<fidl::ClientEnd<fio::Node>> HandleOpen(fio::wire::OpenFlags flags,
                                                    std::string_view path) {
    zx::result endpoints = fidl::CreateEndpoints<fio::Node>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }
    auto& [client, server] = endpoints.value();
    if (const fidl::WireResult result = fidl::WireCall(client_)->Open(
            flags, 0, fidl::StringView::FromExternal(path), std::move(server));
        !result.ok()) {
      return zx::error(result.status());
    }
    return zx::ok(std::move(client));
  }

  void TearDown() override { loop_.Shutdown(); }

 private:
  async::Loop loop_{&kAsyncLoopConfigNeverAttachToThread};
  fs::ManagedVfs vfs_{loop_.dispatcher()};
  fbl::RefPtr<fs::PseudoDir> dir_ = fbl::MakeRefCounted<fs::PseudoDir>();
  fidl::ClientEnd<fio::Directory> client_;
};

TEST_F(BuiltinDevicesTest, ReadZero) {
  zx::result client = HandleOpen(
      fio::wire::OpenFlags::kNotDirectory | fio::wire::OpenFlags::kRightReadable, kZeroDevName);
  ASSERT_OK(client.status_value());

  fbl::unique_fd fd;
  ASSERT_OK(fdio_fd_create(client.value().TakeChannel().release(), fd.reset_and_get_address()));

  std::array<uint8_t, 100> buffer;
  buffer.fill(0x1);
  EXPECT_EQ(read(fd.get(), buffer.data(), buffer.size()), buffer.size());
  EXPECT_TRUE(std::all_of(buffer.begin(), buffer.end(), [](uint8_t b) { return b == 0; }));
}

TEST_F(BuiltinDevicesTest, WriteZero) {
  zx::result client = HandleOpen(
      fio::wire::OpenFlags::kNotDirectory | fio::wire::OpenFlags::kRightReadable, kZeroDevName);
  ASSERT_OK(client.status_value());

  fbl::unique_fd fd;
  ASSERT_OK(fdio_fd_create(client.value().TakeChannel().release(), fd.reset_and_get_address()));

  std::array<uint8_t, 100> buffer;
  buffer.fill(0x1);
  // Write fails.
  EXPECT_LT(write(fd.get(), buffer.data(), buffer.size()), 0);
}

TEST_F(BuiltinDevicesTest, ReadNull) {
  zx::result client = HandleOpen(
      fio::wire::OpenFlags::kNotDirectory | fio::wire::OpenFlags::kRightReadable, kNullDevName);

  fbl::unique_fd fd;
  ASSERT_OK(fdio_fd_create(client.value().TakeChannel().release(), fd.reset_and_get_address()));

  std::array<uint8_t, 100> buffer;
  buffer.fill(0x1);
  // Read will fail to read any bytes.
  EXPECT_EQ(read(fd.get(), buffer.data(), buffer.size()), 0);
  // Buffer is unchanged.
  EXPECT_TRUE(std::all_of(buffer.begin(), buffer.end(), [](uint8_t b) { return b == 0x1; }));
}

TEST_F(BuiltinDevicesTest, WriteNull) {
  zx::result client = HandleOpen(
      fio::wire::OpenFlags::kNotDirectory | fio::wire::OpenFlags::kRightWritable, kNullDevName);

  fbl::unique_fd fd;
  ASSERT_OK(fdio_fd_create(client.value().TakeChannel().release(), fd.reset_and_get_address()));

  std::array<uint8_t, 100> buffer;
  buffer.fill(0x1);
  EXPECT_EQ(write(fd.get(), buffer.data(), buffer.size()), buffer.size());
}

}  // namespace
