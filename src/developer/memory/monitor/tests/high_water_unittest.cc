// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/monitor/high_water.h"

#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/namespace.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>
#include <src/lib/files/file.h>
#include <src/lib/files/path.h>

#include "src/developer/memory/metrics/capture.h"
#include "src/developer/memory/metrics/tests/test_utils.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/storage/memfs/scoped_memfs.h"

using namespace memory;

namespace monitor {
namespace {

const char kMemfsDir[] = "/data";

class HighWaterUnitTest : public gtest::RealLoopFixture {
 protected:
  int memfs_dir_;

 private:
  void SetUp() override {
    RealLoopFixture::SetUp();

    // Install memfs on a different async loop thread to resolve some deadlock when doing blocking
    // file operations on our test loop.
    memfs_loop_.StartThread();
    zx::result<ScopedMemfs> memfs =
        ScopedMemfs::CreateMountedAt(memfs_loop_.dispatcher(), kMemfsDir);
    ASSERT_TRUE(memfs.is_ok());
    data_ = std::make_unique<ScopedMemfs>(std::move(*memfs));

    memfs_dir_ = open(kMemfsDir, O_RDONLY | O_DIRECTORY);
    ASSERT_LT(0, memfs_dir_);
  }

  void TearDown() override {
    RealLoopFixture::TearDown();

    close(memfs_dir_);
    data_.reset();
    memfs_loop_.Shutdown();
  }

  //  private:
  async::Loop memfs_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
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
  ASSERT_FALSE(files::IsFileAt(memfs_dir_, "latest.txt"));
  HighWater hw(
      kMemfsDir, zx::msec(10), 100, dispatcher(),
      [&cs](Capture* c, CaptureLevel l) { return cs.GetCapture(c, l); },
      [](const Capture& c, Digest* d) { return Digester({}).Digest(c, d); });
  ASSERT_FALSE(files::IsFileAt(memfs_dir_, "latest.txt"));
  ASSERT_FALSE(files::IsFileAt(memfs_dir_, "previous.txt"));
  ASSERT_FALSE(files::IsFileAt(memfs_dir_, "latest_digest.txt"));
  ASSERT_FALSE(files::IsFileAt(memfs_dir_, "previous_digest.txt"));
  RunLoopUntil([&cs] { return cs.empty(); });
  EXPECT_TRUE(files::IsFileAt(memfs_dir_, "latest.txt"));
  EXPECT_TRUE(files::IsFileAt(memfs_dir_, "latest_digest.txt"));
  EXPECT_FALSE(hw.GetHighWater().empty());
  EXPECT_FALSE(hw.GetHighWaterDigest().empty());
}

TEST_F(HighWaterUnitTest, RunTwice) {
  ASSERT_FALSE(files::IsFileAt(memfs_dir_, "previous.txt"));
  ASSERT_FALSE(files::IsFileAt(memfs_dir_, "latest.txt"));
  ASSERT_FALSE(files::IsFileAt(memfs_dir_, "previous_digest.txt"));
  ASSERT_FALSE(files::IsFileAt(memfs_dir_, "latest_digest.txt"));
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
    HighWater hw(
        kMemfsDir, zx::msec(10), 100, dispatcher(),
        [&cs](Capture* c, CaptureLevel l) { return cs.GetCapture(c, l); },
        [](const Capture& c, Digest* d) { return Digester({}).Digest(c, d); });
    RunLoopUntil([&cs] { return cs.empty(); });
    EXPECT_FALSE(hw.GetHighWater().empty());
  }
  EXPECT_TRUE(files::IsFileAt(memfs_dir_, "latest.txt"));
  EXPECT_TRUE(files::IsFileAt(memfs_dir_, "latest_digest.txt"));
  EXPECT_FALSE(files::IsFileAt(memfs_dir_, "previous.txt"));
  EXPECT_FALSE(files::IsFileAt(memfs_dir_, "previous_digest.txt"));
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
    HighWater hw(
        kMemfsDir, zx::msec(10), 100, dispatcher(),
        [&cs](Capture* c, CaptureLevel l) { return cs.GetCapture(c, l); },
        [](const Capture& c, Digest* d) { return Digester({}).Digest(c, d); });
    RunLoopUntil([&cs] { return cs.empty(); });
    EXPECT_FALSE(hw.GetHighWater().empty());
    EXPECT_FALSE(hw.GetPreviousHighWater().empty());
    EXPECT_FALSE(hw.GetHighWaterDigest().empty());
    EXPECT_FALSE(hw.GetPreviousHighWaterDigest().empty());
  }
  EXPECT_TRUE(files::IsFileAt(memfs_dir_, "latest.txt"));
  EXPECT_TRUE(files::IsFileAt(memfs_dir_, "latest_digest.txt"));
  EXPECT_TRUE(files::IsFileAt(memfs_dir_, "previous.txt"));
  EXPECT_TRUE(files::IsFileAt(memfs_dir_, "previous_digest.txt"));
}

}  // namespace
}  // namespace monitor
