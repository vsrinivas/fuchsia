// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/zx/vmo.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <gtest/gtest.h>

#include "src/media/sounds/soundplayer/ogg_demux.h"

namespace soundplayer {
namespace test {

static constexpr uint64_t kGoldenHashArm64 = 3820812293088111280u;
static constexpr uint64_t kGoldenHashX64 = 15504583706996406662u;

// Demuxes and decodes a test opus file.
TEST(OggOpusTests, DemuxDecodeTestFile) {
  int fd = open("/pkg/data/testfile.ogg", O_RDONLY);
  EXPECT_NE(fd, -1);

  OggDemux demux;
  auto result = demux.Process(fd);
  close(fd);

  EXPECT_TRUE(result.is_ok());
  EXPECT_TRUE(!!result.value().vmo());
  EXPECT_EQ(530592u, result.value().size());
  EXPECT_EQ(2u, result.value().stream_type().channels);

  auto size = result.value().size();

  // Do a simple hash of the data and compare against a golden value.
  std::unique_ptr<int16_t[]> buffer(new int16_t[size / sizeof(int16_t)]);
  EXPECT_EQ(ZX_OK, result.value().vmo().read(buffer.get(), 0, size));

  int16_t* p = buffer.get();
  uint64_t hash = 0;
  for (uint64_t i = 0; i < size / sizeof(int16_t); ++i) {
    hash = ((hash << 5) + hash) + *p;
    ++p;
  }

  if (hash != kGoldenHashArm64 && hash != kGoldenHashX64) {
    // This will fail, displaying the value of |hash|. Note that either golden value passes
    // the test.
    EXPECT_EQ(kGoldenHashArm64, hash);
  }
}

}  // namespace test
}  // namespace soundplayer
