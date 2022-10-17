// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "filesystem-mounter.h"

#include <fidl/fuchsia.io/cpp/wire_test_base.h>
#include <fidl/fuchsia.io/cpp/wire_types.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/wire/server.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>
#include <zircon/time.h>

#include <mutex>

#include <gtest/gtest.h>

#include "fidl/fuchsia.io/cpp/markers.h"
#include "lib/fidl/cpp/wire/internal/transport_channel.h"
#include "src/lib/storage/fs_management/cpp/format.h"
#include "src/lib/storage/fs_management/cpp/mount.h"
#include "src/lib/testing/predicates/status.h"
#include "src/storage/blobfs/mount.h"
#include "src/storage/fshost/block-watcher.h"
#include "src/storage/fshost/config.h"
#include "src/storage/fshost/constants.h"
#include "src/storage/fshost/fs-manager.h"

namespace fshost {
namespace {
class FilesystemMounterHarness : public testing::Test {
 public:
  FilesystemMounterHarness() : config_(DefaultConfig()), manager_(FshostBootArgs::Create()) {}

 protected:
  FsManager& manager() {
    if (!watcher_) {
      watcher_.emplace(manager_, &config_);
      EXPECT_OK(manager_.Initialize({}, {}, config_, *watcher_));
      manager_.ReadyForShutdown();
    }
    return manager_;
  }

  fshost_config::Config config_;

 private:
  FsManager manager_;
  std::optional<BlockWatcher> watcher_;
};

using MounterTest = FilesystemMounterHarness;

TEST_F(MounterTest, CreateFilesystemManager) { manager(); }

TEST_F(MounterTest, CreateFilesystemMounter) { FilesystemMounter mounter(manager(), &config_); }

enum class FilesystemType {
  kBlobfs,
  kData,
  kDurable,
  kFactoryfs,
};

class TestMounter : public FilesystemMounter {
 public:
  class FakeDirectoryImpl : public fidl::testing::WireTestBase<fuchsia_io::Directory> {
   public:
    void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
      ADD_FAILURE() << "Unexpected call to " << name;
      completer.Close(ZX_ERR_NOT_SUPPORTED);
    }

    void DescribeDeprecated(DescribeDeprecatedCompleter::Sync& completer) override {
      completer.Reply(
          fuchsia_io::wire::NodeInfoDeprecated::WithDirectory(fuchsia_io::wire::DirectoryObject()));
    }

    void Open(OpenRequestView request, OpenCompleter::Sync& _completer) override {}
  };

  template <typename... Args>
  explicit TestMounter(Args&&... args)
      : FilesystemMounter(std::forward<Args>(args)...),
        loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    loop_.StartThread("filesystem-mounter-test");
  }

  void ExpectFilesystem(FilesystemType fs) { expected_filesystem_ = fs; }

  zx::result<StartedFilesystem> LaunchFs(zx::channel block_device,
                                         const fs_management::MountOptions& options,
                                         fs_management::DiskFormat format) const final {
    switch (expected_filesystem_) {
      case FilesystemType::kBlobfs:
        EXPECT_EQ(format, fs_management::kDiskFormatBlobfs);
        break;
      case FilesystemType::kData:
        EXPECT_EQ(format, fs_management::kDiskFormatMinfs);
        break;
      default:
        ADD_FAILURE() << "Unexpected filesystem type";
    }

    return zx::ok(fs_management::StartedSingleVolumeFilesystem());
  }

  zx::result<> LaunchFsNative(fidl::ServerEnd<fuchsia_io::Directory> server, const char* binary,
                              zx::channel block_device,
                              const fs_management::MountOptions& options) const final {
    switch (expected_filesystem_) {
      case FilesystemType::kDurable:
        EXPECT_EQ(std::string_view(binary), kMinfsPath);
        break;
      case FilesystemType::kFactoryfs:
        EXPECT_EQ(std::string_view(binary), kFactoryfsPath);
        break;
      default:
        ADD_FAILURE() << "Unexpected filesystem type";
    }

    fidl::BindServer(loop_.dispatcher(), std::move(server), std::make_unique<FakeDirectoryImpl>());

    return zx::ok();
  }

  zx_status_t RouteData(fidl::UnownedClientEnd<fuchsia_io::Directory> export_root,
                        std::string_view device_path) override {
    return ZX_OK;
  }

 private:
  FilesystemType expected_filesystem_ = FilesystemType::kBlobfs;
  std::mutex flags_lock_;
  async::Loop loop_;
};

TEST_F(MounterTest, DurableMount) {
  config_.durable() = true;
  TestMounter mounter(manager(), &config_);

  mounter.ExpectFilesystem(FilesystemType::kDurable);
  ASSERT_EQ(mounter.MountDurable(zx::channel(), fs_management::MountOptions()), ZX_OK);
  ASSERT_TRUE(mounter.DurableMounted());
}

TEST_F(MounterTest, FactoryMount) {
  config_.factory() = true;
  TestMounter mounter(manager(), &config_);

  mounter.ExpectFilesystem(FilesystemType::kFactoryfs);
  ASSERT_OK(mounter.MountFactoryFs(zx::channel(), fs_management::MountOptions()));

  ASSERT_TRUE(mounter.FactoryMounted());
}

TEST_F(MounterTest, DataMount) {
  TestMounter mounter(manager(), &config_);
  mounter.ExpectFilesystem(FilesystemType::kData);
  ASSERT_OK(mounter.MountData(zx::channel(), std::nullopt, fs_management::MountOptions(),
                              fs_management::DiskFormat::kDiskFormatMinfs));
  ASSERT_TRUE(mounter.DataMounted());
}

TEST_F(MounterTest, BlobfsMount) {
  TestMounter mounter(manager(), &config_);
  mounter.ExpectFilesystem(FilesystemType::kBlobfs);
  ASSERT_OK(mounter.MountBlob(zx::channel(), fs_management::MountOptions()));
  ASSERT_TRUE(mounter.BlobMounted());
}

}  // namespace
}  // namespace fshost
