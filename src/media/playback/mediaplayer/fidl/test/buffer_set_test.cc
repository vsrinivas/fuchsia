// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/fidl/buffer_set.h"

#include "lib/gtest/real_loop_fixture.h"
#include "src/media/playback/mediaplayer/graph/payloads/vmo_payload_allocator.h"

namespace media_player {
namespace test {

class BufferSetTest : public ::gtest::RealLoopFixture {
 protected:
};

// Tests that a buffer set has the intended behavior when put through a nominal sequence of events.
TEST_F(BufferSetTest, NominalSequence) {
  BufferSetManager under_test;

  // No 'current set' initally.
  EXPECT_FALSE(under_test.has_current_set());

  uint64_t buffer_size = 1000;

  fuchsia::media::StreamBufferConstraints constraints;
  constraints.set_buffer_constraints_version_ordinal(3);

  EXPECT_TRUE(under_test.ApplyConstraints(constraints));
  EXPECT_TRUE(under_test.has_current_set());

  auto& current_set = under_test.current_set();

  fuchsia::sysmem::BufferCollectionTokenPtr token;
  auto request = token.NewRequest();

  auto partial_settings = current_set.PartialSettings(std::move(token));
  EXPECT_TRUE(partial_settings.has_buffer_lifetime_ordinal());
  EXPECT_EQ(1u, partial_settings.buffer_lifetime_ordinal());
  EXPECT_TRUE(partial_settings.has_buffer_constraints_version_ordinal());
  EXPECT_EQ(constraints.buffer_constraints_version_ordinal(),
            partial_settings.buffer_constraints_version_ordinal());
  EXPECT_TRUE(partial_settings.has_sysmem_token());
  EXPECT_TRUE(!!partial_settings.sysmem_token());

  EXPECT_EQ(1u, current_set.lifetime_ordinal());
  EXPECT_EQ(0u, current_set.buffer_count());

  uint32_t buffer_count = 13;

  // Set buffer count to establish actual buffers.
  current_set.SetBufferCount(buffer_count);
  EXPECT_EQ(buffer_count, current_set.buffer_count());

  auto allocator = VmoPayloadAllocator::Create();
  EXPECT_TRUE(!!allocator);
  for (size_t i = 0; i < buffer_count; ++i) {
    allocator->AddVmo(PayloadVmo::Create(buffer_size, 0));
  }

  std::vector<fbl::RefPtr<PayloadBuffer>> allocated_buffers;

  for (size_t i = 0; i < buffer_count; ++i) {
    EXPECT_TRUE(current_set.HasFreeBuffer(nullptr));
    allocated_buffers.push_back(current_set.AllocateBuffer(buffer_size, *allocator));
    EXPECT_TRUE(!!allocated_buffers.back());
  }

  bool has_free_buffer_callback_called = false;

  EXPECT_FALSE(current_set.HasFreeBuffer(
      [&has_free_buffer_callback_called]() { has_free_buffer_callback_called = true; }));

  allocated_buffers.pop_back();
  EXPECT_TRUE(has_free_buffer_callback_called);
  EXPECT_TRUE(!!allocated_buffers.back());
}

// Tests allocation methods relating to processor-owned buffers..
TEST_F(BufferSetTest, ProcessorOwnedBuffers) {
  BufferSetManager under_test;

  uint64_t buffer_size = 1000;

  fuchsia::media::StreamBufferConstraints constraints;
  constraints.set_buffer_constraints_version_ordinal(1);

  EXPECT_TRUE(under_test.ApplyConstraints(constraints));
  EXPECT_TRUE(under_test.has_current_set());

  auto& current_set = under_test.current_set();

  uint32_t buffer_count = 13;
  current_set.SetBufferCount(buffer_count);

  auto allocator = VmoPayloadAllocator::Create();
  EXPECT_TRUE(!!allocator);
  for (size_t i = 0; i < buffer_count; ++i) {
    allocator->AddVmo(PayloadVmo::Create(buffer_size, 0));
  }

  current_set.AllocateAllBuffersForProcessor(*allocator);
  EXPECT_FALSE(current_set.HasFreeBuffer(nullptr));

  EXPECT_TRUE(!!current_set.GetProcessorOwnedBuffer(0));
  EXPECT_FALSE(current_set.HasFreeBuffer(nullptr));

  // Take one of the processor's buffers and free it.
  auto buffer = current_set.TakeBufferFromProcessor(0);
  EXPECT_TRUE(!!buffer);
  EXPECT_FALSE(current_set.HasFreeBuffer(nullptr));
  buffer = nullptr;
  EXPECT_TRUE(current_set.HasFreeBuffer(nullptr));

  // Allocate the one free buffer and give it back to the processor.
  buffer = current_set.AllocateBuffer(buffer_size, *allocator);
  EXPECT_TRUE(!!buffer);
  current_set.AddRefBufferForProcessor(0, std::move(buffer));
  EXPECT_FALSE(current_set.HasFreeBuffer(nullptr));

  // Release all the buffers and then allocate them again to make sure they were freed.
  current_set.ReleaseAllProcessorOwnedBuffers();
  EXPECT_TRUE(current_set.HasFreeBuffer(nullptr));

  std::vector<fbl::RefPtr<PayloadBuffer>> allocated_buffers;

  for (size_t i = 0; i < buffer_count; ++i) {
    EXPECT_TRUE(current_set.HasFreeBuffer(nullptr));
    allocated_buffers.push_back(current_set.AllocateBuffer(buffer_size, *allocator));
    EXPECT_TRUE(!!allocated_buffers.back());
  }

  EXPECT_FALSE(current_set.HasFreeBuffer(nullptr));
}

// Tests the transition from one buffer set to another.
TEST_F(BufferSetTest, TwoSets) {
  BufferSetManager under_test;

  fbl::RefPtr<PayloadBuffer> buffer;

  {
    uint64_t buffer_size = 1000;

    fuchsia::media::StreamBufferConstraints constraints;
    constraints.set_buffer_constraints_version_ordinal(1);

    EXPECT_TRUE(under_test.ApplyConstraints(constraints));
    EXPECT_TRUE(under_test.has_current_set());

    auto& current_set = under_test.current_set();

    uint32_t buffer_count = 13;

    // Set buffer count to establish actual buffers.
    current_set.SetBufferCount(buffer_count);
    EXPECT_EQ(buffer_count, current_set.buffer_count());

    auto allocator = VmoPayloadAllocator::Create();
    EXPECT_TRUE(!!allocator);
    for (size_t i = 0; i < buffer_count; ++i) {
      allocator->AddVmo(PayloadVmo::Create(buffer_size, 0));
    }

    buffer = current_set.AllocateBuffer(buffer_size, *allocator);
    EXPECT_TRUE(!!buffer);
  }

  {
    fuchsia::media::StreamBufferConstraints constraints;
    constraints.set_buffer_constraints_version_ordinal(3);

    EXPECT_TRUE(under_test.ApplyConstraints(constraints));
    EXPECT_TRUE(under_test.has_current_set());

    auto& current_set = under_test.current_set();

    fuchsia::sysmem::BufferCollectionTokenPtr token;
    auto request = token.NewRequest();

    auto partial_settings = current_set.PartialSettings(std::move(token));
    EXPECT_TRUE(partial_settings.has_buffer_lifetime_ordinal());
    EXPECT_EQ(3u, partial_settings.buffer_lifetime_ordinal());
    EXPECT_TRUE(partial_settings.has_buffer_constraints_version_ordinal());
    EXPECT_EQ(constraints.buffer_constraints_version_ordinal(),
              partial_settings.buffer_constraints_version_ordinal());
    EXPECT_TRUE(partial_settings.has_sysmem_token());
    EXPECT_TRUE(!!partial_settings.sysmem_token());

    EXPECT_EQ(3u, current_set.lifetime_ordinal());
    EXPECT_EQ(0u, current_set.buffer_count());
  }

  // Free a buffer from the old set. The old set is kept around until all of its buffers are free.
  // This makes sure that happens without incident, though we have no positive indication that the
  // set has been deleted.
  buffer = nullptr;
}

}  // namespace test
}  // namespace media_player
