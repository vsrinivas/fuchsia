// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/directory.h>

#include <fs-management/admin.h>
#include <fs-management/format.h>
#include <ramdevice-client/ramdisk.h>
#include <zxtest/zxtest.h>

namespace fio = ::llcpp::fuchsia::io;

enum State {
  kEmpty,
  kFormatted,
  kStarted,
};

class OutgoingDirectoryTest : public zxtest::Test {
 public:
  explicit OutgoingDirectoryTest(disk_format_t format) : format_(format) {}

  void SetUp() final {
    ASSERT_OK(ramdisk_create(512, 1 << 16, &ramdisk_));
    const char* ramdisk_path = ramdisk_get_path(ramdisk_);
    ASSERT_OK(mkfs(ramdisk_path, format_, launch_stdio_sync, &default_mkfs_options));
    state_ = kFormatted;
  }

  void TearDown() final {
    if (state_ == kStarted) {
      StopFilesystem();
    }
    ASSERT_EQ(ramdisk_destroy(ramdisk_), 0);
  }

 protected:
  void GetExportRoot(zx::unowned_channel* root) {
    ASSERT_EQ(state_, kStarted);
    *root = zx::unowned(export_root_);
  }

  void GetDataRoot(zx::channel* root) {
    ASSERT_EQ(state_, kStarted);
    ASSERT_OK(fs_root_handle(export_root_.get(), root->reset_and_get_address()));
  }

  void CheckDataRoot() {
    const char* format_str = disk_format_string(format_);
    zx::channel data_root;
    GetDataRoot(&data_root);
    fio::DirectoryAdmin::SyncClient data_client(std::move(data_root));
    auto resp = data_client.QueryFilesystem();
    ASSERT_TRUE(resp.ok());
    ASSERT_OK(resp.value().s);
    ASSERT_EQ(strncmp(format_str, reinterpret_cast<char*>(resp.value().info->name.data()),
                      strlen(format_str)),
              0);
  }

  void StartFilesystem(const init_options_t* options) {
    ASSERT_EQ(state_, kFormatted);

    zx::channel device, device_server;
    const char* ramdisk_path = ramdisk_get_path(ramdisk_);
    ASSERT_OK(zx::channel::create(0, &device, &device_server));
    ASSERT_OK(fdio_service_connect(ramdisk_path, device_server.release()));

    ASSERT_OK(fs_init(device.release(), format_, options, export_root_.reset_and_get_address()));
    state_ = kStarted;
  }

  void StopFilesystem() {
    ASSERT_EQ(state_, kStarted);
    zx::channel data_root;
    GetDataRoot(&data_root);

    fio::DirectoryAdmin::SyncClient data_client(std::move(data_root));
    auto resp = data_client.Unmount();
    ASSERT_TRUE(resp.ok());
    ASSERT_OK(resp.value().s);

    state_ = kFormatted;
  }

  void WriteTestFile() {
    ASSERT_EQ(state_, kStarted);
    zx::channel data_root;
    GetDataRoot(&data_root);
    fio::Directory::SyncClient data_client(std::move(data_root));

    zx::channel test_file, test_file_server;
    ASSERT_OK(zx::channel::create(0, &test_file, &test_file_server));
    uint32_t file_flags =
        fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE | fio::OPEN_FLAG_CREATE;
    ASSERT_OK(data_client.Open(file_flags, 0, "test_file", std::move(test_file_server)).status());

    fio::File::SyncClient file_client(std::move(test_file));
    std::vector<uint8_t> content{1, 2, 3, 4};
    auto resp = file_client.Write(fidl::unowned_vec(content));
    ASSERT_OK(resp.status());
    ASSERT_OK(resp.value().s);
    ASSERT_EQ(resp.value().actual, content.size());

    auto resp2 = file_client.Close();
    ASSERT_OK(resp2.status());
    ASSERT_OK(resp2.value().s);
  }

 private:
  State state_ = kEmpty;
  ramdisk_client_t* ramdisk_ = nullptr;
  zx::channel export_root_;
  disk_format_t format_;
};

static constexpr init_options_t readonly_options = {
    .readonly = true,
    .verbose_mount = false,
    .collect_metrics = false,
    .wait_until_ready = true,
    .enable_journal = true,
    .enable_pager = false,
    .write_compression_algorithm = nullptr,
    .write_compression_level = -1,
    .callback = launch_stdio_async,
};

class OutgoingDirectoryBlobfs : public OutgoingDirectoryTest {
 public:
  OutgoingDirectoryBlobfs() : OutgoingDirectoryTest(DISK_FORMAT_BLOBFS) {}
};

class OutgoingDirectoryMinfs : public OutgoingDirectoryTest {
 public:
  OutgoingDirectoryMinfs() : OutgoingDirectoryTest(DISK_FORMAT_MINFS) {}
};

TEST_F(OutgoingDirectoryBlobfs, OutgoingDirectoryReadWriteDataRootIsValidBlobfs) {
  StartFilesystem(&default_init_options);
  CheckDataRoot();
}

TEST_F(OutgoingDirectoryBlobfs, OutgoingDirectoryReadOnlyDataRootIsValidBlobfs) {
  StartFilesystem(&readonly_options);
  CheckDataRoot();
}

// TODO(http://fxbug.dev/60818): Re-enable the test
TEST_F(OutgoingDirectoryBlobfs, DISABLED_RegisterOutgoingDirectoryWithFSHostRegistry) {
  StartFilesystem(&default_init_options);
  zx::unowned_channel export_root;
  GetExportRoot(&export_root);
  ASSERT_OK(fs_register(export_root->get()));
}

TEST_F(OutgoingDirectoryMinfs, OutgoingDirectoryReadWriteDataRootIsValidMinfs) {
  StartFilesystem(&default_init_options);
  CheckDataRoot();
}

TEST_F(OutgoingDirectoryMinfs, OutgoingDirectoryReadOnlyDataRootIsValidMinfs) {
  StartFilesystem(&readonly_options);
  CheckDataRoot();
}

TEST_F(OutgoingDirectoryMinfs, CanWriteToReadWriteMinfsDataRoot) {
  StartFilesystem(&default_init_options);
  WriteTestFile();
}

TEST_F(OutgoingDirectoryMinfs, CannotWriteToReadOnlyMinfsDataRoot) {
  // write an initial test file onto a writable filesystem
  StartFilesystem(&default_init_options);
  WriteTestFile();
  StopFilesystem();

  // start the filesystem in read-only mode
  StartFilesystem(&readonly_options);
  zx::channel data_root;
  GetDataRoot(&data_root);
  fio::Directory::SyncClient data_client(std::move(data_root));

  zx::channel fail_test_file, fail_test_file_server;
  ASSERT_OK(zx::channel::create(0, &fail_test_file, &fail_test_file_server));
  uint32_t fail_file_flags = fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE;
  // open "succeeds" but...
  ASSERT_OK(
      data_client.Open(fail_file_flags, 0, "test_file", std::move(fail_test_file_server)).status());

  // ...we can't actually use the channel
  fio::File::SyncClient fail_file_client(std::move(fail_test_file));
  auto resp = fail_file_client.Read(4);
  ASSERT_EQ(resp.status(), ZX_ERR_PEER_CLOSED);

  // the channel will be valid if we open the file read-only though
  zx::channel test_file, test_file_server;
  ASSERT_OK(zx::channel::create(0, &test_file, &test_file_server));
  uint32_t file_flags = fio::OPEN_RIGHT_READABLE;
  ASSERT_OK(data_client.Open(file_flags, 0, "test_file", std::move(test_file_server)).status());

  fio::File::SyncClient file_client(std::move(test_file));
  auto resp2 = file_client.Read(4);
  ASSERT_OK(resp2.status());
  ASSERT_OK(resp2.value().s);
  ASSERT_EQ(resp2.value().data.data()[0], 1);

  auto resp3 = file_client.Close();
  ASSERT_OK(resp3.status());
  ASSERT_OK(resp3.value().s);
}

TEST_F(OutgoingDirectoryMinfs, CannotWriteToOutgoingDirectory) {
  StartFilesystem(&default_init_options);
  zx::unowned_channel export_root;
  GetExportRoot(&export_root);

  auto test_file_name = std::string("test_file");
  zx::channel test_file, test_file_server;
  ASSERT_OK(zx::channel::create(0, &test_file, &test_file_server));
  uint32_t file_flags = fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE | fio::OPEN_FLAG_CREATE;
  ASSERT_OK(fio::Directory::Call::Open(std::move(export_root), file_flags, 0,
                                       fidl::unowned_str(test_file_name),
                                       std::move(test_file_server))
                .status());

  fio::File::SyncClient file_client(std::move(test_file));
  std::vector<uint8_t> content{1, 2, 3, 4};
  auto resp = file_client.Write(fidl::unowned_vec(content));
  ASSERT_EQ(resp.status(), ZX_ERR_PEER_CLOSED);
}
