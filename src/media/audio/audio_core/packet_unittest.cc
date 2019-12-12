// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/packet.h"

#include <lib/gtest/test_loop_fixture.h>

#include <fbl/ref_ptr.h>

namespace media::audio {
namespace {

class PacketTest : public gtest::TestLoopFixture {
 protected:
  fbl::RefPtr<RefCountedVmoMapper> CreateVmoBufferWithSize(size_t buffer_size) {
    auto vmo_mapper = fbl::MakeRefCounted<RefCountedVmoMapper>();
    zx_status_t res = vmo_mapper->CreateAndMap(buffer_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
    if (res != ZX_OK) {
      return nullptr;
    }
    return vmo_mapper;
  }
};

TEST_F(PacketTest, PostCallbackToDispatcherOnDestruction) {
  auto vmo_mapper = CreateVmoBufferWithSize(128);
  ASSERT_TRUE(vmo_mapper);
  bool packet1_callback_received = false;
  bool packet2_callback_received = false;
  {
    Packet packet1(vmo_mapper, 0, FractionalFrames<uint32_t>(10), FractionalFrames<int64_t>(0),
                   dispatcher(),
                   [&packet1_callback_received] { packet1_callback_received = true; });
    EXPECT_EQ(0, packet1.start());
    EXPECT_EQ(10, packet1.end());
    EXPECT_EQ(10u, packet1.length());
    EXPECT_EQ(vmo_mapper->start(), packet1.payload());
    {
      Packet packet2(vmo_mapper, 64, FractionalFrames<uint32_t>(10), FractionalFrames<int64_t>(10),
                     dispatcher(),
                     [&packet2_callback_received] { packet2_callback_received = true; });
      EXPECT_EQ(10, packet2.start());
      EXPECT_EQ(20, packet2.end());
      EXPECT_EQ(10u, packet2.length());
      EXPECT_EQ(reinterpret_cast<uint8_t*>(vmo_mapper->start()) + 64, packet2.payload());

      // No callbacks yet.
      RunLoopUntilIdle();
      EXPECT_FALSE(packet1_callback_received);
      EXPECT_FALSE(packet2_callback_received);
    }
    // Packet2 should invoke callback in dtor.
    RunLoopUntilIdle();
    EXPECT_FALSE(packet1_callback_received);
    EXPECT_TRUE(packet2_callback_received);
  }
  // Packet1 should invoke callback in dtor.
  RunLoopUntilIdle();
  EXPECT_TRUE(packet1_callback_received);
  EXPECT_TRUE(packet2_callback_received);
}

TEST_F(PacketTest, NullCallback) {
  // Just verify we don't crash with a null callback.
  auto vmo_mapper = CreateVmoBufferWithSize(128);
  ASSERT_TRUE(vmo_mapper);
  Packet packet1(vmo_mapper, 0, FractionalFrames<uint32_t>(10), FractionalFrames<int64_t>(0),
                 dispatcher(), nullptr);
  Packet packet2(vmo_mapper, 0, FractionalFrames<uint32_t>(10), FractionalFrames<int64_t>(0),
                 nullptr, nullptr);
}

}  // namespace
}  // namespace media::audio
