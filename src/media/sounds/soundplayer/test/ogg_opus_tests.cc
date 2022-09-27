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

class TestDiscardableSound : public DiscardableSound {
 public:
  TestDiscardableSound(fbl::unique_fd fd) : DiscardableSound(std::move(fd)) {}

  const zx::vmo& LockForRead() {
    auto& result = DiscardableSound::LockForRead();
    Restore();
    return result;
  }
};

static constexpr uint64_t kGoldenHashArm64 = 3820812293088111280u;
static constexpr uint64_t kGoldenHashX64 = 15504583706996406662u;

// Demuxes and decodes a test opus file.
TEST(OggOpusTests, DemuxDecodeTestFile) {
  fbl::unique_fd fd = fbl::unique_fd(open("/pkg/data/testfile.ogg", O_RDONLY));
  EXPECT_NE(fd.get(), -1);

  OggDemux demux;
  DiscardableSound sound(std::move(fd));
  zx_status_t status = demux.Process(sound);

  EXPECT_EQ(ZX_OK, status);
  auto& vmo = sound.LockForRead();
  EXPECT_TRUE(!!vmo);
  EXPECT_EQ(530592u, sound.size());
  EXPECT_EQ(2u, sound.stream_type().channels);

  auto size = sound.size();

  // Do a simple hash of the data and compare against a golden value.
  auto buffer = std::make_unique<int16_t[]>(size / sizeof(int16_t));
  EXPECT_EQ(ZX_OK, vmo.read(buffer.get(), 0, size));

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

  sound.Unlock();
}

// Demuxes and decodes a test opus file.
TEST(OggOpusTests, RestoreDemuxDecodeTestFile) {
  fbl::unique_fd fd = fbl::unique_fd(open("/pkg/data/testfile.ogg", O_RDONLY));
  EXPECT_NE(fd.get(), -1);

  OggDemux demux;
  // |TestDiscardableSound| always restores on |LockForRead|.
  TestDiscardableSound sound(std::move(fd));
  zx_status_t status = demux.Process(sound);

  EXPECT_EQ(ZX_OK, status);
  auto& vmo = sound.LockForRead();
  EXPECT_TRUE(!!vmo);
  EXPECT_EQ(530592u, sound.size());
  EXPECT_EQ(2u, sound.stream_type().channels);

  auto size = sound.size();

  // Do a simple hash of the data and compare against a golden value.
  auto buffer = std::make_unique<int16_t[]>(size / sizeof(int16_t));
  EXPECT_EQ(ZX_OK, vmo.read(buffer.get(), 0, size));

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

  sound.Unlock();
}

}  // namespace test
}  // namespace soundplayer
