// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/fs_management/cpp/admin.h"

#include <fcntl.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>

#include <vector>

#include <gtest/gtest.h>
#include <ramdevice-client/ramdisk.h>

#include "src/lib/storage/fs_management/cpp/format.h"
#include "src/lib/storage/fs_management/cpp/mount.h"
#include "src/storage/fs_test/crypt_service.h"
#include "src/storage/testing/ram_disk.h"

namespace fs_management {
namespace {

namespace fio = fuchsia_io;
using fuchsia_io::Directory;

enum State {
  kFormatted,
  kStarted,
};

constexpr MountOptions kEmptyOptions = {};

constexpr MountOptions kReadonlyOptions = {
    .readonly = true,
    // Remaining options are same as default values.
};

constexpr MountOptions kDynamicComponentOptions = {
    .component_child_name = "test-blobfs", .component_collection_name = "fs-collection",
    // Remaining options are same as default values.
};

constexpr MountOptions kStaticComponentOptions = {
    .component_child_name = "static-test-blobfs",
    // Remaining options are same as default values.
};

constexpr char kTestFilePath[] = "test_file";

class OutgoingDirectoryFixture : public testing::Test {
 public:
  explicit OutgoingDirectoryFixture(DiskFormat format, MountOptions options = {})
      : format_(format), options_(options) {}

  void SetUp() override {
    auto ramdisk_or = storage::RamDisk::Create(512, 1 << 17);
    ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);
    ramdisk_ = std::move(*ramdisk_or);

    zx_status_t status;
    MkfsOptions mkfs_options{
        .component_child_name = options_.component_child_name,
        .component_collection_name = options_.component_collection_name,
    };
    if (format_ == kDiskFormatFxfs) {
      if (auto service_or = fs_test::GetCryptService(); service_or.is_error()) {
        ADD_FAILURE() << "Unable to get crypt service";
      } else {
        mkfs_options.crypt_client = service_or->release();
      }
    }
    ASSERT_EQ(status = Mkfs(ramdisk_.path().c_str(), format_, LaunchStdioSync, mkfs_options), ZX_OK)
        << zx_status_get_string(status);
    state_ = kFormatted;

    FsckOptions fsck_options{
        .component_child_name = options_.component_child_name,
        .component_collection_name = options_.component_collection_name,
    };
    if (format_ == kDiskFormatFxfs) {
      if (auto service_or = fs_test::GetCryptService(); service_or.is_error()) {
        ADD_FAILURE() << "Unable to get crypt service";
      } else {
        fsck_options.crypt_client = service_or->release();
      }
    }
    ASSERT_EQ(status = Fsck(ramdisk_.path().c_str(), format_, fsck_options, LaunchStdioSync), ZX_OK)
        << zx_status_get_string(status);

    ASSERT_NO_FATAL_FAILURE(StartFilesystem(options_));
  }

  void TearDown() final { ASSERT_NO_FATAL_FAILURE(StopFilesystem()); }

  fidl::WireSyncClient<Directory>& DataRoot() {
    ZX_ASSERT(state_ == kStarted);  // Ensure this isn't used after stopping the filesystem.
    return data_client_;
  }

  fidl::WireSyncClient<Directory>& ExportRoot() {
    ZX_ASSERT(state_ == kStarted);  // Ensure this isn't used after stopping the filesystem.
    return export_client_;
  }

 protected:
  void StartFilesystem(const MountOptions& options) {
    ASSERT_EQ(state_, kFormatted);

    fbl::unique_fd device_fd(open(ramdisk_.path().c_str(), O_RDWR));
    ASSERT_TRUE(device_fd);

    MountOptions actual_options = options;
    if (format_ == kDiskFormatFxfs) {
      if (auto service_or = fs_test::GetCryptService(); service_or.is_error()) {
        ADD_FAILURE() << "Unable to get crypt service";
      } else {
        actual_options.crypt_client = service_or->release();
      }
    }

    auto fs_or = Mount(std::move(device_fd), nullptr, format_, actual_options, LaunchStdioAsync);
    ASSERT_TRUE(fs_or.is_ok()) << fs_or.status_string();
    export_client_ = fidl::WireSyncClient<Directory>(std::move(*fs_or).TakeExportRoot());

    auto data_root = FsRootHandle(export_client_.client_end());
    ASSERT_TRUE(data_root.is_ok()) << data_root.status_string();
    data_client_ = fidl::WireSyncClient<Directory>(std::move(data_root.value()));

    state_ = kStarted;
  }

  void StopFilesystem() {
    if (state_ != kStarted) {
      return;
    }

    ASSERT_EQ(fs_management::Shutdown(export_client_.client_end().borrow()).status_value(), ZX_OK);

    state_ = kFormatted;
  }

 private:
  State state_ = kFormatted;
  storage::RamDisk ramdisk_;
  DiskFormat format_;
  MountOptions options_ = {};
  fidl::WireSyncClient<Directory> export_client_;
  fidl::WireSyncClient<Directory> data_client_;
};

// Generalized Admin Tests

struct OutgoingDirectoryTestParameters {
  DiskFormat format;
  MountOptions options;
};

std::string PrintTestSuffix(
    const testing::TestParamInfo<std::tuple<DiskFormat, MountOptions>> params) {
  std::stringstream out;
  out << DiskFormatString(std::get<0>(params.param));
  if (std::get<1>(params.param).readonly) {
    out << "_readonly";
  }
  if (std::get<1>(params.param).component_collection_name != nullptr) {
    out << "_dynamic";
  }
  if (std::get<1>(params.param).component_child_name != nullptr) {
    out << "_component";
  }
  return out.str();
}

// Generalized outgoing directory tests which should work in both mutable and read-only modes.
class OutgoingDirectoryTest
    : public OutgoingDirectoryFixture,
      public testing::WithParamInterface<std::tuple<DiskFormat, MountOptions>> {
 public:
  OutgoingDirectoryTest()
      : OutgoingDirectoryFixture(std::get<0>(GetParam()), std::get<1>(GetParam())) {}
};

TEST_P(OutgoingDirectoryTest, DataRootIsValid) {
  std::string_view format_str = DiskFormatString(std::get<0>(GetParam()));
  auto resp = DataRoot()->QueryFilesystem();
  ASSERT_TRUE(resp.ok()) << resp.status_string();
  ASSERT_EQ(resp.value().s, ZX_OK) << zx_status_get_string(resp.value().s);
  ASSERT_STREQ(format_str.data(), reinterpret_cast<char*>(resp.value().info->name.data()));
}

INSTANTIATE_TEST_SUITE_P(OutgoingDirectoryTest, OutgoingDirectoryTest,
                         testing::Combine(testing::Values(kDiskFormatBlobfs, kDiskFormatMinfs,
                                                          kDiskFormatFxfs, kDiskFormatF2fs),
                                          // Filesystems that don't yet support launching as
                                          // components should be able to fall back to launching
                                          // the old way.
                                          testing::Values(kEmptyOptions, kReadonlyOptions,
                                                          kStaticComponentOptions,
                                                          kDynamicComponentOptions)),
                         PrintTestSuffix);

// Minfs-Specific Tests (can be generalized to work with any mutable filesystem by parameterizing
// on the disk format if required).
// Launches the filesystem and creates a file called kTestFilePath in the data root.
class OutgoingDirectoryMinfs : public OutgoingDirectoryFixture {
 public:
  OutgoingDirectoryMinfs() : OutgoingDirectoryFixture(kDiskFormatMinfs) {}

  void SetUp() final {
    // Make sure we invoke the base fixture's SetUp method before we continue.
    ASSERT_NO_FATAL_FAILURE(OutgoingDirectoryFixture::SetUp());
    // Since we initialize the fixture with the default writable options, we should always
    // be able to create an initial test file.
    ASSERT_NO_FATAL_FAILURE(WriteTestFile());
  }

 private:
  void WriteTestFile() {
    auto test_file_ends = fidl::CreateEndpoints<fio::File>();
    ASSERT_TRUE(test_file_ends.is_ok()) << test_file_ends.status_string();
    fidl::ServerEnd<fio::Node> test_file_server(test_file_ends->server.TakeChannel());

    fio::wire::OpenFlags file_flags = fio::wire::OpenFlags::kRightReadable |
                                      fio::wire::OpenFlags::kRightWritable |
                                      fio::wire::OpenFlags::kCreate;
    ASSERT_EQ(DataRoot()->Open(file_flags, 0, kTestFilePath, std::move(test_file_server)).status(),
              ZX_OK);

    fidl::WireSyncClient<fio::File> file_client(std::move(test_file_ends->client));
    std::vector<uint8_t> content{1, 2, 3, 4};
    const fidl::WireResult res =
        file_client->Write(fidl::VectorView<uint8_t>::FromExternal(content));
    ASSERT_TRUE(res.ok()) << res.status_string();
    const fidl::WireResponse resp = res.value();
    ASSERT_TRUE(resp.result.is_response()) << zx_status_get_string(resp.result.err());
    ASSERT_EQ(resp.result.response().actual_count, content.size());

    auto resp2 = file_client->Close();
    ASSERT_TRUE(resp2.ok()) << resp2.status_string();
    ASSERT_TRUE(resp2.value().result.is_response())
        << zx_status_get_string(resp2.value().result.err());
  }
};

TEST_F(OutgoingDirectoryMinfs, CannotWriteToReadOnlyDataRoot) {
  // restart the filesystem in read-only mode
  ASSERT_NO_FATAL_FAILURE(StopFilesystem());
  ASSERT_NO_FATAL_FAILURE(StartFilesystem(kReadonlyOptions));

  auto fail_file_ends = fidl::CreateEndpoints<fio::File>();
  ASSERT_TRUE(fail_file_ends.is_ok()) << fail_file_ends.status_string();
  fidl::ServerEnd<fio::Node> fail_test_file_server(fail_file_ends->server.TakeChannel());

  fio::wire::OpenFlags fail_file_flags =
      fio::wire::OpenFlags::kRightReadable | fio::wire::OpenFlags::kRightWritable;
  // open "succeeds" but...
  auto open_resp =
      DataRoot()->Open(fail_file_flags, 0, kTestFilePath, std::move(fail_test_file_server));
  ASSERT_TRUE(open_resp.ok()) << open_resp.status_string();

  // ...we can't actually use the channel
  fidl::WireSyncClient<fio::File> fail_file_client(std::move(fail_file_ends->client));
  const fidl::WireResult res1 = fail_file_client->Read(4);
  ASSERT_EQ(res1.status(), ZX_ERR_PEER_CLOSED) << res1.status_string();

  // the channel will be valid if we open the file read-only though
  auto test_file_ends = fidl::CreateEndpoints<fio::File>();
  ASSERT_TRUE(test_file_ends.is_ok()) << test_file_ends.status_string();
  fidl::ServerEnd<fio::Node> test_file_server(test_file_ends->server.TakeChannel());

  fio::wire::OpenFlags file_flags = fio::wire::OpenFlags::kRightReadable;
  auto open_resp2 = DataRoot()->Open(file_flags, 0, kTestFilePath, std::move(test_file_server));
  ASSERT_TRUE(open_resp2.ok()) << open_resp2.status_string();

  fidl::WireSyncClient<fio::File> file_client(std::move(test_file_ends->client));
  const fidl::WireResult res2 = file_client->Read(4);
  ASSERT_TRUE(res2.ok()) << res2.status_string();
  const fidl::WireResponse resp2 = res2.value();
  ASSERT_TRUE(resp2.result.is_response()) << zx_status_get_string(resp2.result.err());
  ASSERT_EQ(resp2.result.response().data[0], 1);

  auto close_resp = file_client->Close();
  ASSERT_TRUE(close_resp.ok()) << close_resp.status_string();
  ASSERT_TRUE(close_resp.value().result.is_response())
      << zx_status_get_string(close_resp.value().result.err());
}

TEST_F(OutgoingDirectoryMinfs, CannotWriteToOutgoingDirectory) {
  auto test_file_ends = fidl::CreateEndpoints<fio::File>();
  ASSERT_TRUE(test_file_ends.is_ok()) << test_file_ends.status_string();
  fidl::ServerEnd<fio::Node> test_file_server(test_file_ends->server.TakeChannel());

  fio::wire::OpenFlags file_flags = fio::wire::OpenFlags::kRightReadable |
                                    fio::wire::OpenFlags::kRightWritable |
                                    fio::wire::OpenFlags::kCreate;
  auto open_resp = ExportRoot()->Open(file_flags, 0, kTestFilePath, std::move(test_file_server));
  ASSERT_TRUE(open_resp.ok()) << open_resp.status_string();

  fidl::WireSyncClient<fio::File> file_client(std::move(test_file_ends->client));
  std::vector<uint8_t> content{1, 2, 3, 4};
  auto write_resp = file_client->Write(fidl::VectorView<uint8_t>::FromExternal(content));
  ASSERT_EQ(write_resp.status(), ZX_ERR_PEER_CLOSED) << write_resp.status_string();
}

}  // namespace
}  // namespace fs_management
