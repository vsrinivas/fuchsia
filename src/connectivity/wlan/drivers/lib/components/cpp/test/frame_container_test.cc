// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <wlan/drivers/components/frame.h>
#include <wlan/drivers/components/frame_container.h>
#include <wlan/drivers/components/frame_storage.h>
#include <zxtest/zxtest.h>

namespace {

using wlan::drivers::components::Frame;
using wlan::drivers::components::FrameContainer;
using wlan::drivers::components::FrameStorage;

uint8_t data[256];
Frame CreateTestFrame(FrameStorage* storage = nullptr) {
  static uint16_t buffer_id_counter = 123;
  constexpr uint8_t kVmoId = 13;
  constexpr size_t kVmoOffset = 0x0c00;
  constexpr uint8_t kPortId = 7;

  return Frame(storage, kVmoId, kVmoOffset, buffer_id_counter++, data, sizeof(data), kPortId);
}

TEST(FrameContainerTest, Constructible) {
  FrameContainer container;
  EXPECT_TRUE(container.empty());
}

TEST(FrameContainerTest, EmplaceBack) {
  FrameContainer container;
  container.emplace_back(CreateTestFrame());
  EXPECT_FALSE(container.empty());
  EXPECT_EQ(container.size(), 1u);
  container.emplace_back(CreateTestFrame());
  EXPECT_EQ(container.size(), 2u);
}

TEST(FrameContainerTest, MoveConstructible) {
  FrameContainer container;
  container.emplace_back(CreateTestFrame());
  const uint16_t buffer_id = container.back().BufferId();

  FrameContainer other(std::move(container));
  EXPECT_FALSE(other.empty());
  EXPECT_EQ(other.size(), 1u);
  EXPECT_EQ(other.back().BufferId(), buffer_id);
}

TEST(FrameContainerTest, MoveAssignable) {
  FrameContainer container;
  container.emplace_back(CreateTestFrame());
  const uint16_t buffer_id = container.back().BufferId();

  FrameContainer other;
  other.emplace_back(CreateTestFrame());
  other = std::move(container);
  EXPECT_FALSE(other.empty());
  EXPECT_EQ(other.size(), 1u);
  EXPECT_EQ(other.back().BufferId(), buffer_id);
}

TEST(FrameContainerTest, FrontAndBack) {
  FrameContainer container;
  Frame frame1 = CreateTestFrame();
  Frame frame2 = CreateTestFrame();
  Frame frame3 = CreateTestFrame();
  const uint16_t frame1_buffer_id = frame1.BufferId();
  const uint16_t frame3_buffer_id = frame3.BufferId();

  container.emplace_back(std::move(frame1));
  container.emplace_back(std::move(frame2));
  container.emplace_back(std::move(frame3));

  EXPECT_EQ(container.front().BufferId(), frame1_buffer_id);
  EXPECT_EQ(container.back().BufferId(), frame3_buffer_id);
}

TEST(FrameContainerTest, Iterators) {
  FrameContainer container;
  EXPECT_EQ(container.begin(), container.end());

  container.emplace_back(CreateTestFrame());
  container.emplace_back(CreateTestFrame());
  container.emplace_back(CreateTestFrame());

  auto it = container.begin();
  EXPECT_NE(container.end(), it);
  Frame& frame1 = *it;
  ++it;
  EXPECT_NE(container.end(), it);
  Frame& frame2 = *it;
  ++it;
  EXPECT_NE(container.end(), it);
  Frame& frame3 = *it;
  ++it;
  EXPECT_EQ(container.end(), it);

  EXPECT_NE(frame1.BufferId(), frame2.BufferId());
  EXPECT_NE(frame1.BufferId(), frame3.BufferId());
  EXPECT_NE(frame2.BufferId(), frame3.BufferId());
}

TEST(FrameContainerTest, Subscript) {
  FrameContainer container;

  container.emplace_back(CreateTestFrame());
  container.emplace_back(CreateTestFrame());
  container.emplace_back(CreateTestFrame());

  EXPECT_NE(container[0].BufferId(), container[1].BufferId());
  EXPECT_NE(container[0].BufferId(), container[2].BufferId());
  EXPECT_NE(container[1].BufferId(), container[2].BufferId());

  auto it = container.begin();
  EXPECT_EQ(&container[0], &*it);
  ++it;
  EXPECT_EQ(&container[1], &*it);
  ++it;
  EXPECT_EQ(&container[2], &*it);
}

TEST(FrameContainerTest, Clear) {
  FrameContainer container;
  container.emplace_back(CreateTestFrame());
  container.emplace_back(CreateTestFrame());
  container.emplace_back(CreateTestFrame());

  EXPECT_EQ(container.size(), 3u);

  container.clear();
  EXPECT_TRUE(container.empty());
  EXPECT_EQ(container.size(), 0u);
  EXPECT_EQ(container.begin(), container.end());
}

TEST(FrameContainerTest, Lifetime) {
  FrameStorage storage;
  {
    std::lock_guard lock(storage);
    EXPECT_TRUE(storage.empty());
  }

  {
    FrameContainer container;
    container.emplace_back(CreateTestFrame(&storage));
    container.emplace_back(CreateTestFrame(&storage));
    container.emplace_back(CreateTestFrame(&storage));
    // Container goes out of scope and should return frames to storage
  }

  std::lock_guard lock(storage);
  EXPECT_FALSE(storage.empty());
  EXPECT_EQ(storage.size(), 3u);
}

TEST(FrameContainerTest, MultipleStorages) {
  FrameStorage storage1, storage2;

  Frame storage1_frame = CreateTestFrame(&storage1);
  Frame storage2_frame1 = CreateTestFrame(&storage2);
  Frame storage2_frame2 = CreateTestFrame(&storage2);
  uint16_t storage1_frame_buffer_id = storage1_frame.BufferId();
  uint16_t storage2_frame1_buffer_id = storage2_frame1.BufferId();
  uint16_t storage2_frame2_buffer_id = storage2_frame2.BufferId();

  {
    // Put some frames in this container with different storages and no storages
    FrameContainer container;
    container.emplace_back(std::move(storage2_frame1));
    container.emplace_back(std::move(storage1_frame));
    container.emplace_back(CreateTestFrame(nullptr));
    container.emplace_back(std::move(storage2_frame2));
  }

  std::lock_guard lock1(storage1);
  std::lock_guard lock2(storage2);
  ASSERT_EQ(storage1.size(), 1u);
  ASSERT_EQ(storage2.size(), 2u);
  EXPECT_EQ(storage1.back().BufferId(), storage1_frame_buffer_id);
  EXPECT_EQ(storage2.front().BufferId(), storage2_frame1_buffer_id);
  EXPECT_EQ(storage2.back().BufferId(), storage2_frame2_buffer_id);
}

TEST(FrameContainerTest, MultipleStoragesNullFirst) {
  FrameStorage storage1, storage2;

  Frame storage1_frame = CreateTestFrame(&storage1);
  Frame storage2_frame1 = CreateTestFrame(&storage2);
  Frame storage2_frame2 = CreateTestFrame(&storage2);
  uint16_t storage1_frame_buffer_id = storage1_frame.BufferId();
  uint16_t storage2_frame1_buffer_id = storage2_frame1.BufferId();
  uint16_t storage2_frame2_buffer_id = storage2_frame2.BufferId();

  {
    // Put some frames in this container with different storages and no storages
    FrameContainer container;
    container.emplace_back(CreateTestFrame(nullptr));
    container.emplace_back(std::move(storage1_frame));
    container.emplace_back(std::move(storage2_frame1));
    container.emplace_back(CreateTestFrame(nullptr));
    container.emplace_back(std::move(storage2_frame2));
  }

  std::lock_guard lock1(storage1);
  std::lock_guard lock2(storage2);
  ASSERT_EQ(storage1.size(), 1u);
  ASSERT_EQ(storage2.size(), 2u);
  EXPECT_EQ(storage1.back().BufferId(), storage1_frame_buffer_id);
  EXPECT_EQ(storage2.front().BufferId(), storage2_frame1_buffer_id);
  EXPECT_EQ(storage2.back().BufferId(), storage2_frame2_buffer_id);
}

}  // namespace
