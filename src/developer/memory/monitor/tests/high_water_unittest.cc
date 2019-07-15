// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/monitor/high_water.h"

#include <gtest/gtest.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/namespace.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/memfs/memfs.h>
#include <src/lib/files/file.h>
#include <src/lib/fxl/logging.h>

#include "src/developer/memory/metrics/capture.h"
#include "src/developer/memory/metrics/tests/test_utils.h"

using namespace memory;

namespace monitor {
namespace {

const char kMemfsDir[] = "/data";

// Mounts a memfs filesystem at a given path and unmounts it when this object
// goes out of scope.
class ScopedMemfs {
 public:
  // Creates a new memfs filesystem at the given path.
  static zx_status_t InstallAt(const char* path, async_dispatcher_t* dispatcher,
                               std::unique_ptr<ScopedMemfs>* out) {
    fdio_ns_t* ns;
    zx_status_t status = fdio_ns_get_installed(&ns);
    if (status != ZX_OK) {
      return status;
    }

    memfs_filesystem_t* fs;
    zx_handle_t root;
    status = memfs_create_filesystem(dispatcher, &fs, &root);
    if (status != ZX_OK) {
      return status;
    }

    status = fdio_ns_bind(ns, path, root);
    if (status != ZX_OK) {
      memfs_free_filesystem(fs, nullptr);
      return status;
    }

    *out = std::make_unique<ScopedMemfs>(fs, path);
    return ZX_OK;
  }

  ScopedMemfs(memfs_filesystem_t* fs, const char* path) : fs_(fs), path_(path) {}

  ~ScopedMemfs() {
    fdio_ns_t* ns;
    sync_completion_t completion;
    memfs_free_filesystem(fs_, &completion);
    FXL_CHECK(sync_completion_wait(&completion, ZX_TIME_INFINITE) == ZX_OK)
        << "Failed to unmount memfs";
    FXL_CHECK(fdio_ns_get_installed(&ns) == ZX_OK) << "Failed to read namespaces";
    FXL_CHECK(fdio_ns_unbind(ns, path_) == ZX_OK) << "Failed to unbind memfs filesystem";
  }

 private:
  memfs_filesystem_t* fs_;
  const char* path_;
};

class HighWaterUnitTest : public gtest::RealLoopFixture {
 private:
  void SetUp() override {
    RealLoopFixture::SetUp();
    // Install memfs on a different async loop thread to resolve some deadlock
    // when doing blocking file operations on our test loop.
    FXL_CHECK(ScopedMemfs::InstallAt(kMemfsDir, memfs_loop_.dispatcher(), &data_) == ZX_OK);
    memfs_loop_.StartThread();
  }

  void TearDown() override {
    RealLoopFixture::TearDown();
    data_.reset();
    memfs_loop_.Shutdown();
  }

  //  private:
  async::Loop memfs_loop_{&kAsyncLoopConfigNoAttachToThread};
  std::unique_ptr<ScopedMemfs> data_;
};

TEST_F(HighWaterUnitTest, Basic) {
  CaptureSupplier cs({{
                          .kmem = {.free_bytes = 100},
                      },
                      {.kmem = {.free_bytes = 100},
                       .vmos =
                           {
                               {.koid = 1, .name = "v1", .committed_bytes = 101},
                           },
                       .processes = {
                           {.koid = 2, .name = "p1", .vmos = {1}},
                       }}});
  ASSERT_FALSE(files::IsFile(std::string(kMemfsDir) + "/latest.txt"));
  HighWater hw(kMemfsDir, zx::msec(10), 100, dispatcher(),
               [&cs](Capture& c, CaptureLevel l) { return cs.GetCapture(c, l); });
  ASSERT_FALSE(files::IsFile(std::string(kMemfsDir) + "/latest.txt"));
  ASSERT_FALSE(files::IsFile(std::string(kMemfsDir) + "/previous.txt"));
  RunLoopUntil([&cs] { return cs.empty(); });
  EXPECT_TRUE(files::IsFile(std::string(kMemfsDir) + "/latest.txt"));
  EXPECT_FALSE(hw.GetHighWater().empty());
}

TEST_F(HighWaterUnitTest, RunTwice) {
  ASSERT_FALSE(files::IsFile(std::string(kMemfsDir) + "/previous.txt"));
  ASSERT_FALSE(files::IsFile(std::string(kMemfsDir) + "/latest.txt"));
  {
    CaptureSupplier cs({{
                            .kmem = {.free_bytes = 100},
                        },
                        {.kmem = {.free_bytes = 100},
                         .vmos =
                             {
                                 {.koid = 1, .name = "v1", .committed_bytes = 101},
                             },
                         .processes = {
                             {.koid = 2, .name = "p1", .vmos = {1}},
                         }}});
    HighWater hw(kMemfsDir, zx::msec(10), 100, dispatcher(),
                 [&cs](Capture& c, CaptureLevel l) { return cs.GetCapture(c, l); });
    RunLoopUntil([&cs] { return cs.empty(); });
    EXPECT_FALSE(hw.GetHighWater().empty());
  }
  EXPECT_TRUE(files::IsFile(std::string(kMemfsDir) + "/latest.txt"));
  EXPECT_FALSE(files::IsFile(std::string(kMemfsDir) + "/previous.txt"));
  {
    CaptureSupplier cs({{
                            .kmem = {.free_bytes = 100},
                        },
                        {.kmem = {.free_bytes = 100},
                         .vmos =
                             {
                                 {.koid = 1, .name = "v1", .committed_bytes = 101},
                             },
                         .processes = {
                             {.koid = 2, .name = "p1", .vmos = {1}},
                         }}});
    HighWater hw(kMemfsDir, zx::msec(10), 100, dispatcher(),
                 [&cs](Capture& c, CaptureLevel l) { return cs.GetCapture(c, l); });
    RunLoopUntil([&cs] { return cs.empty(); });
    EXPECT_FALSE(hw.GetHighWater().empty());
    EXPECT_FALSE(hw.GetPreviousHighWater().empty());
  }
  EXPECT_TRUE(files::IsFile(std::string(kMemfsDir) + "/latest.txt"));
  EXPECT_TRUE(files::IsFile(std::string(kMemfsDir) + "/previous.txt"));
}

}  // namespace
}  // namespace monitor
