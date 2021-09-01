// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "filesystem-mounter.h"

#include <fidl/fuchsia.io/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>

#include <cobalt-client/cpp/in_memory_logger.h>
#include <gtest/gtest.h>

#include "src/lib/testing/predicates/status.h"
#include "src/storage/blobfs/mount.h"
#include "src/storage/fshost/block-watcher.h"
#include "src/storage/fshost/fs-manager.h"
#include "src/storage/fshost/fshost-fs-provider.h"
#include "src/storage/fshost/metrics_cobalt.h"

namespace fshost {
namespace {

std::unique_ptr<FsHostMetrics> MakeMetrics() {
  return std::make_unique<FsHostMetricsCobalt>(std::make_unique<cobalt_client::Collector>(
      std::make_unique<cobalt_client::InMemoryLogger>()));
}

class FilesystemMounterHarness : public testing::Test {
 public:
  FilesystemMounterHarness() : manager_(FshostBootArgs::Create(), MakeMetrics()) {}

 protected:
  FsManager& manager() {
    if (!watcher_) {
      watcher_.emplace(manager_, &config_);
      zx::channel dir_request, lifecycle_request;
      EXPECT_OK(manager_.Initialize(std::move(dir_request), std::move(lifecycle_request),
                                    zx::channel(), nullptr, *watcher_));
    }
    return manager_;
  }

  Config config_;

 private:
  FsManager manager_;
  std::optional<BlockWatcher> watcher_;
};

using MounterTest = FilesystemMounterHarness;

TEST_F(MounterTest, CreateFilesystemManager) { manager(); }

TEST_F(MounterTest, CreateFilesystemMounter) { FilesystemMounter mounter(manager(), &config_); }

TEST_F(MounterTest, PkgfsWillNotMountBeforeBlobAndData) {
  FilesystemMounter mounter(manager(), &config_);

  ASSERT_FALSE(mounter.BlobMounted());
  ASSERT_FALSE(mounter.DataMounted());
  mounter.TryMountPkgfs();
  EXPECT_FALSE(mounter.PkgfsMounted());
}

enum class FilesystemType {
  kBlobfs,
  kMinfs,
  kFactoryfs,
};

class TestMounter : public FilesystemMounter {
 public:
  class FakeNodeImpl : public fuchsia_io::testing::Node_TestBase {
    void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
      ADD_FAILURE() << "Unexpected call to " << name;
      completer.Close(ZX_ERR_NOT_SUPPORTED);
    }

    void Describe(DescribeRequestView request, DescribeCompleter::Sync& completer) override {
      fuchsia_io::wire::DirectoryObject dir;
      completer.Reply(fuchsia_io::wire::NodeInfo::WithDirectory(
          fidl::ObjectView<fuchsia_io::wire::DirectoryObject>::FromExternal(&dir)));
    }
  };

  template <typename... Args>
  explicit TestMounter(async_dispatcher_t* dispatcher, Args&&... args)
      : FilesystemMounter(std::forward<Args>(args)...), dispatcher_(dispatcher) {}

  void ExpectFilesystem(FilesystemType fs) { expected_filesystem_ = fs; }

  zx_status_t LaunchFs(int argc, const char** argv, zx_handle_t* hnd, uint32_t* ids, size_t len,
                       uint32_t fs_flags) final {
    if (argc != 2) {
      return ZX_ERR_INVALID_ARGS;
    }

    switch (expected_filesystem_) {
      case FilesystemType::kBlobfs:
        EXPECT_EQ(std::string_view(argv[0]), "/pkg/bin/blobfs");
        EXPECT_EQ(fs_flags, unsigned{FS_SVC | FS_SVC_BLOBFS});
        EXPECT_EQ(len, 2ul);
        break;
      case FilesystemType::kMinfs:
        EXPECT_EQ(std::string_view(argv[0]), "/pkg/bin/minfs");
        EXPECT_EQ(fs_flags, unsigned{FS_SVC});
        EXPECT_EQ(len, 2ul);
        break;
      case FilesystemType::kFactoryfs:
        EXPECT_EQ(std::string_view(argv[0]), "/pkg/bin/factoryfs");
        EXPECT_EQ(fs_flags, unsigned{FS_SVC});
        break;
      default:
        ADD_FAILURE() << "Unexpected filesystem type";
    }

    EXPECT_EQ(std::string_view(argv[1]), "mount");

    EXPECT_EQ(ids[0], PA_DIRECTORY_REQUEST);
    EXPECT_EQ(ids[1], FS_HANDLE_BLOCK_DEVICE_ID);

    fidl::BindServer(dispatcher_, fidl::ServerEnd<fuchsia_io::Node>(zx::channel(hnd[0])),
                     std::make_unique<FakeNodeImpl>());

    // Close all other handles.
    for (size_t i = 1; i < len; i++) {
      EXPECT_OK(zx_handle_close(hnd[i]));
    }

    return ZX_OK;
  }

 private:
  FilesystemType expected_filesystem_ = FilesystemType::kBlobfs;
  async_dispatcher_t* const dispatcher_;
};

class MounterTestWithDispatcher : public MounterTest {
 public:
  MounterTestWithDispatcher() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

 protected:
  void SetUp() override { ASSERT_OK(loop_.StartThread("filesystem-mounter-test")); }

  async_dispatcher_t* dispatcher() { return loop_.dispatcher(); }

 private:
  async::Loop loop_;
};

TEST_F(MounterTestWithDispatcher, DurableMount) {
  TestMounter mounter(dispatcher(), manager(), &config_);

  mounter.ExpectFilesystem(FilesystemType::kMinfs);
  ASSERT_OK(mounter.MountDurable(zx::channel(), MountOptions()));
  ASSERT_TRUE(mounter.DurableMounted());
}

TEST_F(MounterTestWithDispatcher, FactoryMount) {
  TestMounter mounter(dispatcher(), manager(), &config_);

  mounter.ExpectFilesystem(FilesystemType::kFactoryfs);
  ASSERT_OK(mounter.MountFactoryFs(zx::channel(), MountOptions()));

  ASSERT_TRUE(mounter.FactoryMounted());
}

TEST_F(MounterTestWithDispatcher, PkgfsWillNotMountBeforeData) {
  config_ = Config(Config::Options{{Config::kWaitForData, {}}});
  TestMounter mounter(dispatcher(), manager(), &config_);

  mounter.ExpectFilesystem(FilesystemType::kBlobfs);
  ASSERT_OK(mounter.MountBlob(zx::channel(), MountOptions()));

  ASSERT_TRUE(mounter.BlobMounted());
  ASSERT_FALSE(mounter.DataMounted());
  mounter.TryMountPkgfs();
  EXPECT_FALSE(mounter.PkgfsMounted());
}

TEST_F(MounterTestWithDispatcher, PkgfsWillNotMountBeforeDataUnlessExplicitlyRequested) {
  TestMounter mounter(dispatcher(), manager(), &config_);

  mounter.ExpectFilesystem(FilesystemType::kBlobfs);
  ASSERT_OK(mounter.MountBlob(zx::channel(), MountOptions()));

  ASSERT_TRUE(mounter.BlobMounted());
  ASSERT_FALSE(mounter.DataMounted());
  mounter.TryMountPkgfs();
  EXPECT_TRUE(mounter.PkgfsMounted());
}

TEST_F(MounterTestWithDispatcher, PkgfsWillNotMountBeforeBlob) {
  config_ = Config(Config::Options{{Config::kWaitForData, {}}});
  TestMounter mounter(dispatcher(), manager(), &config_);

  mounter.ExpectFilesystem(FilesystemType::kMinfs);
  ASSERT_OK(mounter.MountData(zx::channel(), MountOptions()));

  ASSERT_FALSE(mounter.BlobMounted());
  ASSERT_TRUE(mounter.DataMounted());
  mounter.TryMountPkgfs();
  EXPECT_FALSE(mounter.PkgfsMounted());
}

TEST_F(MounterTestWithDispatcher, PkgfsMountsWithBlobAndData) {
  config_ = Config(Config::Options{{Config::kWaitForData, {}}});
  TestMounter mounter(dispatcher(), manager(), &config_);

  mounter.ExpectFilesystem(FilesystemType::kBlobfs);
  ASSERT_OK(mounter.MountBlob(zx::channel(), MountOptions()));
  mounter.ExpectFilesystem(FilesystemType::kMinfs);
  ASSERT_OK(mounter.MountData(zx::channel(), MountOptions()));

  ASSERT_TRUE(mounter.BlobMounted());
  ASSERT_TRUE(mounter.DataMounted());
  mounter.TryMountPkgfs();
  EXPECT_TRUE(mounter.PkgfsMounted());
}

}  // namespace
}  // namespace fshost
