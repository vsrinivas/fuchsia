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
#include "src/lib/storage/fs_management/cpp/mkfs_with_default.h"
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

enum class Mode {
  // Use the old, non-component way of launching.
  kLegacy,

  // The old, non-component way but read-only.
  kReadOnly,

  // A statically routed component. If not supported, the old way will be used.
  kStatic,

  // A dynamically routed component. If not supported, the old way will be used.
  kDynamic,
};

constexpr char kTestFilePath[] = "test_file";

class OutgoingDirectoryFixture : public testing::Test {
 public:
  explicit OutgoingDirectoryFixture(DiskFormat format, Mode mode) : format_(format) {
    switch (mode) {
      case Mode::kLegacy:
        options_ = MountOptions();
        break;
      case Mode::kReadOnly:
        options_ = MountOptions{
            .readonly = true,
            // Remaining options are same as default values.
        };
        break;
      case Mode::kStatic:
        component_name_ = std::string("static-test-");
        component_name_.append(DiskFormatString(format));
        options_ = MountOptions{.component_child_name = component_name_.c_str()};
        break;
      case Mode::kDynamic:
        component_name_ = "dynamic-test-";
        component_name_.append(DiskFormatString(format));
        options_ = MountOptions{
            .component_child_name = component_name_.c_str(),
            .component_collection_name = "fs-collection",
        };
        // We can use the default for blobfs, but other filesystems need to come from our package
        // (if they run as a component).
        if (format != kDiskFormatBlobfs && !fs_management::DiskFormatComponentUrl(format).empty()) {
          std::string url = std::string("#meta/");
          url.append(DiskFormatString(format));
          url.append(".cm");
          options_.component_url = std::move(url);
        }
        break;
    }
  }

  void SetUp() override {
    auto ramdisk_or = storage::RamDisk::Create(512, 1 << 17);
    ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);
    ramdisk_ = std::move(*ramdisk_or);

    zx_status_t status;
    MkfsOptions mkfs_options{
        .component_child_name = options_.component_child_name,
        .component_collection_name = options_.component_collection_name,
        .component_url = options_.component_url,
    };
    if (format_ == kDiskFormatFxfs) {
      auto service = fs_test::GetCryptService();
      ASSERT_TRUE(service.is_ok());
      auto status = MkfsWithDefault(ramdisk_.path().c_str(), format_, LaunchStdioSync, mkfs_options,
                                    *std::move(service));
      ASSERT_TRUE(status.is_ok()) << status.status_string();
    } else {
      ASSERT_EQ(status = Mkfs(ramdisk_.path().c_str(), format_, LaunchStdioSync, mkfs_options),
                ZX_OK)
          << zx_status_get_string(status);
    }
    state_ = kFormatted;

    FsckOptions fsck_options{
        .component_child_name = options_.component_child_name,
        .component_collection_name = options_.component_collection_name,
        .component_url = options_.component_url,
    };
    ASSERT_EQ(status = Fsck(ramdisk_.path().c_str(), format_, fsck_options, LaunchStdioSync), ZX_OK)
        << zx_status_get_string(status);

    ASSERT_NO_FATAL_FAILURE(StartFilesystem(options_));
  }

  void TearDown() final { ASSERT_NO_FATAL_FAILURE(StopFilesystem()); }

  fidl::ClientEnd<Directory> DataRoot() {
    ZX_ASSERT(state_ == kStarted);  // Ensure this isn't used after stopping the filesystem.
    auto data = fs_->DataRoot();
    ZX_ASSERT_MSG(data.is_ok(), "Invalid data root: %s", data.status_string());
    return std::move(*data);
  }

  const fidl::ClientEnd<Directory>& ExportRoot() {
    ZX_ASSERT(state_ == kStarted);  // Ensure this isn't used after stopping the filesystem.
    return fs_->ExportRoot();
  }

 protected:
  void StartFilesystem(const MountOptions& options) {
    ASSERT_EQ(state_, kFormatted);

    fbl::unique_fd device_fd(open(ramdisk_.path().c_str(), O_RDWR));
    ASSERT_TRUE(device_fd);

    MountOptions actual_options = options;
    if (format_ == kDiskFormatFxfs) {
      actual_options.crypt_client = [] {
        if (auto service = fs_test::GetCryptService(); service.is_error()) {
          ADD_FAILURE() << "Unable to get crypt service";
          return zx::channel();
        } else {
          return *std::move(service);
        }
      };
      auto fs = MountMultiVolumeWithDefault(std::move(device_fd), format_, actual_options,
                                            LaunchStdioAsync);
      ASSERT_TRUE(fs.is_ok()) << fs.status_string();
      fs_ = std::make_unique<StartedSingleVolumeMultiVolumeFilesystem>(std::move(*fs));
    } else {
      auto fs = Mount(std::move(device_fd), format_, actual_options, LaunchStdioAsync);
      ASSERT_TRUE(fs.is_ok()) << fs.status_string();
      fs_ = std::make_unique<StartedSingleVolumeFilesystem>(std::move(*fs));
    }
    state_ = kStarted;
  }

  void StopFilesystem() {
    if (state_ != kStarted) {
      return;
    }

    ASSERT_EQ(fs_->Unmount().status_value(), ZX_OK);

    state_ = kFormatted;
  }

 private:
  State state_ = kFormatted;
  storage::RamDisk ramdisk_;
  DiskFormat format_;
  MountOptions options_ = {};
  std::unique_ptr<SingleVolumeFilesystemInterface> fs_;
  fidl::WireSyncClient<Directory> export_client_;
  fidl::WireSyncClient<Directory> data_client_;
  std::string component_name_;
};

// Generalized Admin Tests

struct OutgoingDirectoryTestParameters {
  DiskFormat format;
  MountOptions options;
};

std::string PrintTestSuffix(const testing::TestParamInfo<std::tuple<DiskFormat, Mode>> params) {
  std::stringstream out;
  out << DiskFormatString(std::get<0>(params.param));
  switch (std::get<1>(params.param)) {
    case Mode::kLegacy:
      break;
    case Mode::kReadOnly:
      out << "_readonly";
      break;
    case Mode::kDynamic:
      out << "_dynamic";
      __FALLTHROUGH;
    case Mode::kStatic:
      out << "_component";
      break;
  }
  return out.str();
}

// Generalized outgoing directory tests which should work in both mutable and read-only modes.
class OutgoingDirectoryTest : public OutgoingDirectoryFixture,
                              public testing::WithParamInterface<std::tuple<DiskFormat, Mode>> {
 public:
  OutgoingDirectoryTest()
      : OutgoingDirectoryFixture(std::get<0>(GetParam()), std::get<1>(GetParam())) {}
};

TEST_P(OutgoingDirectoryTest, DataRootIsValid) {
  std::string_view format_str = DiskFormatString(std::get<0>(GetParam()));
  auto resp = fidl::WireCall(DataRoot())->QueryFilesystem();
  ASSERT_TRUE(resp.ok()) << resp.status_string();
  ASSERT_EQ(resp.value().s, ZX_OK) << zx_status_get_string(resp.value().s);
  ASSERT_STREQ(format_str.data(), reinterpret_cast<char*>(resp.value().info->name.data()));
}

using Combinations = std::vector<std::tuple<DiskFormat, Mode>>;

Combinations TestCombinations() {
  Combinations c;

  auto add = [&](DiskFormat format, std::initializer_list<Mode> modes) {
    for (Mode mode : modes) {
      c.push_back(std::tuple(format, mode));
    }
  };

  add(kDiskFormatBlobfs, {Mode::kLegacy, Mode::kReadOnly, Mode::kDynamic, Mode::kStatic});
  add(kDiskFormatMinfs, {Mode::kLegacy, Mode::kReadOnly, Mode::kDynamic, Mode::kStatic});
  add(kDiskFormatFxfs, {Mode::kDynamic, Mode::kStatic});
  add(kDiskFormatF2fs, {Mode::kLegacy, Mode::kReadOnly, Mode::kDynamic, Mode::kStatic});

  return c;
}

INSTANTIATE_TEST_SUITE_P(OutgoingDirectoryTest, OutgoingDirectoryTest,
                         testing::ValuesIn(TestCombinations()), PrintTestSuffix);

// Minfs-Specific Tests (can be generalized to work with any mutable filesystem by parameterizing
// on the disk format if required).
// Launches the filesystem and creates a file called kTestFilePath in the data root.
class OutgoingDirectoryMinfs : public OutgoingDirectoryFixture {
 public:
  OutgoingDirectoryMinfs() : OutgoingDirectoryFixture(kDiskFormatMinfs, Mode::kLegacy) {}

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
    ASSERT_EQ(fidl::WireCall(DataRoot())
                  ->Open(file_flags, 0, kTestFilePath, std::move(test_file_server))
                  .status(),
              ZX_OK);

    fidl::WireSyncClient<fio::File> file_client(std::move(test_file_ends->client));
    std::vector<uint8_t> content{1, 2, 3, 4};
    const fidl::WireResult res =
        file_client->Write(fidl::VectorView<uint8_t>::FromExternal(content));
    ASSERT_TRUE(res.ok()) << res.status_string();
    const fit::result resp = res.value();
    ASSERT_TRUE(resp.is_ok()) << zx_status_get_string(resp.error_value());
    ASSERT_EQ(resp.value()->actual_count, content.size());

    auto resp2 = file_client->Close();
    ASSERT_TRUE(resp2.ok()) << resp2.status_string();
    ASSERT_TRUE(resp2->is_ok()) << zx_status_get_string(resp2->error_value());
  }
};

TEST_F(OutgoingDirectoryMinfs, CannotWriteToReadOnlyDataRoot) {
  // restart the filesystem in read-only mode
  ASSERT_NO_FATAL_FAILURE(StopFilesystem());
  ASSERT_NO_FATAL_FAILURE(StartFilesystem(MountOptions{.readonly = true}));

  auto data_root = DataRoot();

  auto fail_file_ends = fidl::CreateEndpoints<fio::File>();
  ASSERT_TRUE(fail_file_ends.is_ok()) << fail_file_ends.status_string();
  fidl::ServerEnd<fio::Node> fail_test_file_server(fail_file_ends->server.TakeChannel());

  fio::wire::OpenFlags fail_file_flags =
      fio::wire::OpenFlags::kRightReadable | fio::wire::OpenFlags::kRightWritable;
  // open "succeeds" but...
  auto open_resp = fidl::WireCall(data_root)->Open(fail_file_flags, 0, kTestFilePath,
                                                   std::move(fail_test_file_server));
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
  auto open_resp2 =
      fidl::WireCall(data_root)->Open(file_flags, 0, kTestFilePath, std::move(test_file_server));
  ASSERT_TRUE(open_resp2.ok()) << open_resp2.status_string();

  fidl::WireSyncClient<fio::File> file_client(std::move(test_file_ends->client));
  const fidl::WireResult res2 = file_client->Read(4);
  ASSERT_TRUE(res2.ok()) << res2.status_string();
  const fit::result resp2 = res2.value();
  ASSERT_TRUE(resp2.is_ok()) << zx_status_get_string(resp2.error_value());
  ASSERT_EQ(resp2.value()->data[0], 1);

  auto close_resp = file_client->Close();
  ASSERT_TRUE(close_resp.ok()) << close_resp.status_string();
  ASSERT_TRUE(close_resp->is_ok()) << zx_status_get_string(close_resp->error_value());
}

TEST_F(OutgoingDirectoryMinfs, CannotWriteToOutgoingDirectory) {
  auto test_file_ends = fidl::CreateEndpoints<fio::File>();
  ASSERT_TRUE(test_file_ends.is_ok()) << test_file_ends.status_string();
  fidl::ServerEnd<fio::Node> test_file_server(test_file_ends->server.TakeChannel());

  fio::wire::OpenFlags file_flags = fio::wire::OpenFlags::kRightReadable |
                                    fio::wire::OpenFlags::kRightWritable |
                                    fio::wire::OpenFlags::kCreate;
  auto open_resp =
      fidl::WireCall(ExportRoot())->Open(file_flags, 0, kTestFilePath, std::move(test_file_server));
  ASSERT_TRUE(open_resp.ok()) << open_resp.status_string();

  fidl::WireSyncClient<fio::File> file_client(std::move(test_file_ends->client));
  std::vector<uint8_t> content{1, 2, 3, 4};
  auto write_resp = file_client->Write(fidl::VectorView<uint8_t>::FromExternal(content));
  ASSERT_EQ(write_resp.status(), ZX_ERR_PEER_CLOSED) << write_resp.status_string();
}

}  // namespace
}  // namespace fs_management
