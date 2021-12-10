// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io.admin/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>

#include <vector>

#include <fs-management/admin.h>
#include <fs-management/format.h>
#include <gtest/gtest.h>
#include <ramdevice-client/ramdisk.h>

namespace fs_management {
namespace {

namespace fio = fuchsia_io;
using fuchsia_io_admin::DirectoryAdmin;

enum State {
  kEmpty,
  kFormatted,
  kStarted,
};

class OutgoingDirectoryTest : public testing::Test {
 public:
  explicit OutgoingDirectoryTest(DiskFormat format) : format_(format) {}

  void SetUp() final {
    ASSERT_EQ(ramdisk_create(512, 1 << 16, &ramdisk_), ZX_OK);
    const char* ramdisk_path = ramdisk_get_path(ramdisk_);

    ASSERT_EQ(Mkfs(ramdisk_path, format_, launch_stdio_sync, MkfsOptions()), ZX_OK);
    state_ = kFormatted;
  }

  void TearDown() final {
    if (state_ == kStarted) {
      StopFilesystem();
    }
    ASSERT_EQ(ramdisk_destroy(ramdisk_), 0);
  }

 protected:
  void GetExportRoot(fidl::UnownedClientEnd<DirectoryAdmin>* root) {
    ASSERT_EQ(state_, kStarted);
    *root = export_root_;
  }

  void GetDataRoot(fidl::ClientEnd<DirectoryAdmin>* root) {
    ASSERT_EQ(state_, kStarted);
    auto root_or = FsRootHandle(export_root_);
    ASSERT_EQ(root_or.status_value(), ZX_OK);
    *root = *std::move(root_or);
  }

  void CheckDataRoot() {
    std::string_view format_str = DiskFormatString(format_);
    fidl::ClientEnd<DirectoryAdmin> data_root;
    GetDataRoot(&data_root);
    fidl::WireSyncClient<DirectoryAdmin> data_client(std::move(data_root));
    auto resp = data_client->QueryFilesystem();
    ASSERT_TRUE(resp.ok());
    ASSERT_EQ(resp.value().s, ZX_OK);
    const auto& raw_name = resp.value().info->name;
    std::string_view name(
        reinterpret_cast<const char*>(raw_name.begin()),
        std::distance(raw_name.begin(), std::find(raw_name.begin(), raw_name.end(), 0)));
    ASSERT_EQ(format_str, name);
  }

  void StartFilesystem(const InitOptions& options) {
    ASSERT_EQ(state_, kFormatted);

    zx::channel device, device_server;
    const char* ramdisk_path = ramdisk_get_path(ramdisk_);
    ASSERT_EQ(zx::channel::create(0, &device, &device_server), ZX_OK);
    ASSERT_EQ(fdio_service_connect(ramdisk_path, device_server.release()), ZX_OK);

    auto export_root_or = FsInit(std::move(device), format_, options);
    ASSERT_EQ(export_root_or.status_value(), ZX_OK);
    export_root_ = *std::move(export_root_or);
    state_ = kStarted;
  }

  void StopFilesystem() {
    ASSERT_EQ(state_, kStarted);
    fidl::ClientEnd<DirectoryAdmin> data_root;
    GetDataRoot(&data_root);

    fidl::WireSyncClient<DirectoryAdmin> data_client(std::move(data_root));
    auto resp = data_client->Unmount();
    ASSERT_TRUE(resp.ok());
    ASSERT_EQ(resp.value().s, ZX_OK);

    state_ = kFormatted;
  }

  void WriteTestFile() {
    ASSERT_EQ(state_, kStarted);
    fidl::ClientEnd<DirectoryAdmin> data_root;
    GetDataRoot(&data_root);
    fidl::WireSyncClient<DirectoryAdmin> data_client(std::move(data_root));

    auto test_file_ends = fidl::CreateEndpoints<fio::File>();
    ASSERT_TRUE(test_file_ends.is_ok()) << test_file_ends.status_string();

    fidl::ServerEnd<fio::Node> test_file_server(test_file_ends->server.TakeChannel());

    uint32_t file_flags =
        fio::wire::kOpenRightReadable | fio::wire::kOpenRightWritable | fio::wire::kOpenFlagCreate;
    ASSERT_EQ(data_client->Open(file_flags, 0, "test_file", std::move(test_file_server)).status(),
              ZX_OK);

    fidl::WireSyncClient<fio::File> file_client(std::move(test_file_ends->client));
    std::vector<uint8_t> content{1, 2, 3, 4};
    auto resp = file_client->Write(fidl::VectorView<uint8_t>::FromExternal(content));
    ASSERT_EQ(resp.status(), ZX_OK);
    ASSERT_EQ(resp.value().s, ZX_OK);
    ASSERT_EQ(resp.value().actual, content.size());

    auto resp2 = file_client->Close();
    ASSERT_EQ(resp2.status(), ZX_OK);
    ASSERT_EQ(resp2.value().s, ZX_OK);
  }

 private:
  State state_ = kEmpty;
  ramdisk_client_t* ramdisk_ = nullptr;
  fidl::ClientEnd<DirectoryAdmin> export_root_;
  DiskFormat format_;
};

static constexpr InitOptions kReadonlyOptions = {
    .readonly = true,
    .verbose_mount = false,
    .collect_metrics = false,
    .wait_until_ready = true,
    .write_compression_algorithm = nullptr,
    .write_compression_level = -1,
    .callback = launch_stdio_async,
};

class OutgoingDirectoryBlobfs : public OutgoingDirectoryTest {
 public:
  OutgoingDirectoryBlobfs() : OutgoingDirectoryTest(kDiskFormatBlobfs) {}
};

class OutgoingDirectoryMinfs : public OutgoingDirectoryTest {
 public:
  OutgoingDirectoryMinfs() : OutgoingDirectoryTest(kDiskFormatMinfs) {}
};

TEST_F(OutgoingDirectoryBlobfs, OutgoingDirectoryReadWriteDataRootIsValidBlobfs) {
  StartFilesystem(InitOptions());
  CheckDataRoot();
}

TEST_F(OutgoingDirectoryBlobfs, OutgoingDirectoryReadOnlyDataRootIsValidBlobfs) {
  StartFilesystem(kReadonlyOptions);
  CheckDataRoot();
}

TEST_F(OutgoingDirectoryMinfs, OutgoingDirectoryReadWriteDataRootIsValidMinfs) {
  StartFilesystem(InitOptions());
  CheckDataRoot();
}

TEST_F(OutgoingDirectoryMinfs, OutgoingDirectoryReadOnlyDataRootIsValidMinfs) {
  StartFilesystem(kReadonlyOptions);
  CheckDataRoot();
}

TEST_F(OutgoingDirectoryMinfs, CanWriteToReadWriteMinfsDataRoot) {
  StartFilesystem(InitOptions());
  WriteTestFile();
}

TEST_F(OutgoingDirectoryMinfs, CannotWriteToReadOnlyMinfsDataRoot) {
  // write an initial test file onto a writable filesystem
  StartFilesystem(InitOptions());
  WriteTestFile();
  StopFilesystem();

  // start the filesystem in read-only mode
  StartFilesystem(kReadonlyOptions);
  fidl::ClientEnd<DirectoryAdmin> data_root;
  GetDataRoot(&data_root);
  fidl::WireSyncClient<DirectoryAdmin> data_client(std::move(data_root));

  auto fail_file_ends = fidl::CreateEndpoints<fio::File>();
  ASSERT_TRUE(fail_file_ends.is_ok()) << fail_file_ends.status_string();
  fidl::ServerEnd<fio::Node> fail_test_file_server(fail_file_ends->server.TakeChannel());

  uint32_t fail_file_flags = fio::wire::kOpenRightReadable | fio::wire::kOpenRightWritable;
  // open "succeeds" but...
  ASSERT_EQ(
      data_client->Open(fail_file_flags, 0, "test_file", std::move(fail_test_file_server)).status(),
      ZX_OK);

  // ...we can't actually use the channel
  fidl::WireSyncClient<fio::File> fail_file_client(std::move(fail_file_ends->client));
  auto resp = fail_file_client->Read(4);
  ASSERT_EQ(resp.status(), ZX_ERR_PEER_CLOSED);

  // the channel will be valid if we open the file read-only though
  auto test_file_ends = fidl::CreateEndpoints<fio::File>();
  ASSERT_TRUE(test_file_ends.is_ok()) << test_file_ends.status_string();
  fidl::ServerEnd<fio::Node> test_file_server(test_file_ends->server.TakeChannel());

  uint32_t file_flags = fio::wire::kOpenRightReadable;
  ASSERT_EQ(data_client->Open(file_flags, 0, "test_file", std::move(test_file_server)).status(),
            ZX_OK);

  fidl::WireSyncClient<fio::File> file_client(std::move(test_file_ends->client));
  auto resp2 = file_client->Read(4);
  ASSERT_EQ(resp2.status(), ZX_OK);
  ASSERT_EQ(resp2.value().s, ZX_OK);
  ASSERT_EQ(resp2.value().data.data()[0], 1);

  auto resp3 = file_client->Close();
  ASSERT_EQ(resp3.status(), ZX_OK);
  ASSERT_EQ(resp3.value().s, ZX_OK);
}

TEST_F(OutgoingDirectoryMinfs, CannotWriteToOutgoingDirectory) {
  StartFilesystem(InitOptions());
  fidl::UnownedClientEnd<DirectoryAdmin> export_root(FIDL_HANDLE_INVALID);
  GetExportRoot(&export_root);

  auto test_file_name = std::string("test_file");

  auto test_file_ends = fidl::CreateEndpoints<fio::File>();
  ASSERT_TRUE(test_file_ends.is_ok()) << test_file_ends.status_string();
  fidl::ServerEnd<fio::Node> test_file_server(test_file_ends->server.TakeChannel());

  uint32_t file_flags =
      fio::wire::kOpenRightReadable | fio::wire::kOpenRightWritable | fio::wire::kOpenFlagCreate;
  ASSERT_EQ(fidl::WireCall<DirectoryAdmin>(std::move(export_root))
                ->Open(file_flags, 0, fidl::StringView::FromExternal(test_file_name),
                       std::move(test_file_server))
                .status(),
            ZX_OK);

  fidl::WireSyncClient<fio::File> file_client(std::move(test_file_ends->client));
  std::vector<uint8_t> content{1, 2, 3, 4};
  auto resp = file_client->Write(fidl::VectorView<uint8_t>::FromExternal(content));
  ASSERT_EQ(resp.status(), ZX_ERR_PEER_CLOSED);
}

}  // namespace
}  // namespace fs_management
