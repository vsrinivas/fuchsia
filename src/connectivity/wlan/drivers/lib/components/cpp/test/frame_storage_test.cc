// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <wlan/drivers/components/frame_storage.h>
#include <zxtest/zxtest.h>

namespace {

using wlan::drivers::components::Frame;
using wlan::drivers::components::FrameContainer;
using wlan::drivers::components::FrameStorage;

uint8_t data[256];
Frame CreateTestFrame(FrameStorage& storage) {
  static uint16_t buffer_id_counter = 123;
  constexpr uint8_t kVmoId = 13;
  constexpr size_t kVmoOffset = 0x0c00;
  constexpr uint8_t kPortId = 7;

  return Frame(&storage, kVmoId, kVmoOffset, buffer_id_counter++, data, sizeof(data), kPortId);
}

TEST(FrameStorageTest, Constructible) { FrameStorage storage; }

TEST(FrameStorageTest, Destructor) {
  // Construct a storage object in place to avoid the destructor being called at the end of the
  // test. We will call it manually instead so we can verify its behavior.
  std::aligned_storage<sizeof(FrameStorage), alignof(FrameStorage)>::type storage_location;
  FrameStorage* storage = new (&storage_location) FrameStorage;

  {
    std::lock_guard lock(*storage);
    storage->Store(CreateTestFrame(*storage));
  }

  storage->~FrameStorage();

  // The destructor must ensure that frames that are destructed in storage do not get returned
  // to the same storage in their destructor.
  std::lock_guard lock(*storage);
  EXPECT_TRUE(storage->empty());
}

TEST(FrameStorageTest, Store) {
  FrameStorage storage;
  std::lock_guard lock(storage);
  ASSERT_EQ(storage.size(), 0u);
  ASSERT_TRUE(storage.empty());
  storage.Store(CreateTestFrame(storage));
  ASSERT_EQ(storage.size(), 1u);
  ASSERT_FALSE(storage.empty());
  storage.Store(CreateTestFrame(storage));
  ASSERT_EQ(storage.size(), 2u);
  storage.Store(CreateTestFrame(storage));
  ASSERT_EQ(storage.size(), 3u);

  ASSERT_NE(&storage.front(), &storage.back());
  ASSERT_NE(storage.front().BufferId(), storage.back().BufferId());
}

TEST(FrameStorageTest, StoreRxSpaceBuffers) {
  constexpr size_t num_buffers = 4;
  constexpr size_t buffer_size = 64;
  rx_space_buffer_t buffers[num_buffers];
  uint8_t vmo_data[num_buffers];
  uint8_t* vmo_addrs[] = {vmo_data};

  auto fill_rx_space_buffer = [&](rx_space_buffer_t& buffer) {
    static uint32_t buffer_id = 0;
    buffer.region.offset = static_cast<uint64_t>(buffer_id) * buffer_size;
    buffer.region.length = buffer_size;
    buffer.region.vmo = 0;
    buffer.id = buffer_id++;
  };

  fill_rx_space_buffer(buffers[0]);
  fill_rx_space_buffer(buffers[1]);
  fill_rx_space_buffer(buffers[2]);
  fill_rx_space_buffer(buffers[3]);

  FrameStorage storage;
  FrameContainer frames;
  {
    std::lock_guard lock(storage);
    storage.Store(buffers, std::size(buffers), vmo_addrs);
    frames = storage.Acquire(num_buffers);
  }

  auto frame_and_buffer_equal = [&](const Frame& frame, const rx_space_buffer_t& buffer) {
    return frame.BufferId() == buffer.id && frame.VmoOffset() == buffer.region.offset &&
           frame.Size() == buffer.region.length && frame.VmoId() == buffer.region.vmo &&
           frame.Data() == vmo_addrs[frame.VmoId()] + frame.VmoOffset() && frame.Headroom() == 0;
  };

  auto frame = frames.begin();
  ASSERT_TRUE(frame_and_buffer_equal(*frame, buffers[0]));
  ++frame;
  ASSERT_TRUE(frame_and_buffer_equal(*frame, buffers[1]));
  ++frame;
  ASSERT_TRUE(frame_and_buffer_equal(*frame, buffers[2]));
  ++frame;
  ASSERT_TRUE(frame_and_buffer_equal(*frame, buffers[3]));
}

TEST(FrameStorageTest, Clear) {
  FrameStorage storage;
  std::lock_guard lock(storage);
  storage.Store(CreateTestFrame(storage));

  // Clear has the same behavior as destruction, frames should not be returned to storage as they
  // are being destructed during clear.
  storage.clear();
  EXPECT_TRUE(storage.empty());
}

struct FrameStorageFilledTest : public ::zxtest::Test {
  void SetUp() override {
    frame1_ = CreateTestFrame(storage_);
    frame2_ = CreateTestFrame(storage_);
    frame3_ = CreateTestFrame(storage_);
    std::lock_guard lock(storage_);
    storage_.Store(std::move(frame1_));
    storage_.Store(std::move(frame2_));
    storage_.Store(std::move(frame3_));
  }

  void TearDown() override {
    std::lock_guard lock(storage_);
    storage_.clear();
  }

  Frame frame1_;
  Frame frame2_;
  Frame frame3_;
  FrameStorage storage_;
};

TEST(FrameStorageTest, AcquireEmpty) {
  FrameStorage storage;
  std::lock_guard lock(storage);

  std::optional<Frame> frame = storage.Acquire();
  EXPECT_FALSE(frame.has_value());
}

TEST_F(FrameStorageFilledTest, AcquireSingle) {
  std::optional<Frame> frame1;
  std::optional<Frame> frame2;
  std::optional<Frame> frame3;
  {
    std::lock_guard lock(storage_);
    frame1 = storage_.Acquire();
    frame2 = storage_.Acquire();
    frame3 = storage_.Acquire();

    // No more frames available
    EXPECT_FALSE(storage_.Acquire().has_value());
  }

  EXPECT_TRUE(frame1.has_value());
  EXPECT_TRUE(frame2.has_value());
  EXPECT_TRUE(frame3.has_value());

  EXPECT_NE(frame1->BufferId(), frame2->BufferId());
  EXPECT_NE(frame1->BufferId(), frame3->BufferId());
  EXPECT_NE(frame2->BufferId(), frame3->BufferId());
}

TEST_F(FrameStorageFilledTest, AcquireMultiple) {
  const size_t num_frames = [&] {
    std::lock_guard lock(storage_);
    return storage_.size();
  }();

  FrameContainer frames;
  {
    std::lock_guard lock(storage_);
    frames = storage_.Acquire(num_frames);
    EXPECT_TRUE(storage_.empty());
    EXPECT_TRUE(storage_.Acquire(num_frames).empty());
  }
  ASSERT_EQ(frames.size(), num_frames);
  // The rest of the test assumes there are at least 3 frames, make sure that that's true.
  ASSERT_GE(frames.size(), 3u);

  auto it = frames.begin();
  Frame& frame1 = *it;
  ++it;
  Frame& frame2 = *it;
  ++it;
  Frame& frame3 = *it;

  // We can verify that we received unique frames but we should not make assumptions about the order
  // that they are acquired in.
  EXPECT_NE(frame1.BufferId(), frame2.BufferId());
  EXPECT_NE(frame1.BufferId(), frame3.BufferId());
  EXPECT_NE(frame2.BufferId(), frame3.BufferId());
}

TEST_F(FrameStorageFilledTest, AcquireMoreThanAvailable) {
  const size_t num_frames = [&] {
    std::lock_guard lock(storage_);
    return storage_.size();
  }();

  std::lock_guard lock(storage_);
  FrameContainer frames = storage_.Acquire(num_frames + 1);
  EXPECT_TRUE(frames.empty());
  EXPECT_EQ(storage_.size(), num_frames);
}

TEST_F(FrameStorageFilledTest, ReturnSingle) {
  std::lock_guard lock(storage_);
  std::optional<Frame> frame = storage_.Acquire();
  ASSERT_TRUE(frame.has_value());

  EXPECT_EQ(storage_.size(), 2u);
  storage_.Store(std::move(*frame));
  EXPECT_EQ(storage_.size(), 3u);
  // At this point the frame should no longer be returned on its destruction. If that were to happen
  // this test should deadlock because we're holding the storage mutex which the Frame's destructor
  // would also try to acquire if it decided to return itself to storage.
}

TEST_F(FrameStorageFilledTest, ReturnMultiple) {
  std::lock_guard lock(storage_);
  const size_t initial_size = storage_.size();
  FrameContainer frames = storage_.Acquire(2);
  EXPECT_EQ(frames.size(), 2u);
  EXPECT_EQ(storage_.size(), initial_size - 2u);

  storage_.Store(std::move(frames));
  EXPECT_EQ(storage_.size(), initial_size);
}

TEST_F(FrameStorageFilledTest, AcquireReturnAcquire) {
  const size_t initial_size = [&] {
    std::lock_guard lock(storage_);
    return storage_.size();
  }();
  {
    // Acquire these frames and then let them go out of scope
    FrameContainer frames;
    {
      std::lock_guard lock(storage_);
      frames = storage_.Acquire(2);
      EXPECT_EQ(storage_.size(), initial_size - 2u);
    }
    EXPECT_EQ(frames.size(), 2u);
  }
  FrameContainer frames;
  {
    // Size should now have returned to the initial size
    std::lock_guard lock(storage_);
    EXPECT_EQ(storage_.size(), initial_size);
    // So we should be able to acquire more frames
    frames = storage_.Acquire(3);
    EXPECT_EQ(storage_.size(), initial_size - 3u);
  }
}

TEST(FrameStorageTest, EraseFramesWithVmoId) {
  FrameStorage storage;
  constexpr uint8_t kVmoOne = 1u;
  constexpr uint8_t kVmoTwo = 2u;
  constexpr size_t kOffset = 0;
  constexpr uint8_t kPort = 0;
  uint16_t buf_id = 0;
  uint8_t data[256];

  FrameContainer remaining_frames;
  {
    std::lock_guard lock(storage);
    storage.Store(Frame(&storage, kVmoOne, kOffset, buf_id++, data, sizeof(data), kPort));
    storage.Store(Frame(&storage, kVmoOne, kOffset, buf_id++, data, sizeof(data), kPort));
    storage.Store(Frame(&storage, kVmoTwo, kOffset, buf_id++, data, sizeof(data), kPort));
    storage.Store(Frame(&storage, kVmoTwo, kOffset, buf_id++, data, sizeof(data), kPort));
    storage.Store(Frame(&storage, kVmoOne, kOffset, buf_id++, data, sizeof(data), kPort));

    storage.EraseFramesWithVmoId(kVmoOne);
    ASSERT_EQ(storage.size(), 2u);
    remaining_frames = storage.Acquire(2u);
  }
  ASSERT_EQ(remaining_frames.size(), 2u);

  for (auto& frame : remaining_frames) {
    EXPECT_EQ(frame.VmoId(), kVmoTwo);
  }
}

}  // namespace
