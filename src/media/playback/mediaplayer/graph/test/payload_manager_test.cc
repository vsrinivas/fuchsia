// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/graph/payloads/payload_manager.h"

#include <fuchsia/sysmem/cpp/fidl.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/gtest/real_loop_fixture.h"
#include "src/media/playback/mediaplayer/graph/formatting.h"
#include "src/media/playback/mediaplayer/graph/test/fake_service_provider.h"

namespace media_player::test {

constexpr uint32_t kCpuUsageRead =
    fuchsia::sysmem::cpuUsageRead | fuchsia::sysmem::cpuUsageReadOften;
constexpr uint32_t kCpuUsageWrite =
    fuchsia::sysmem::cpuUsageWrite | fuchsia::sysmem::cpuUsageWriteOften;
constexpr uint32_t kCpuUsageReadWrite = kCpuUsageRead | kCpuUsageWrite;

class PayloadManagerTest : public ::gtest::RealLoopFixture {
 protected:
  // Loops until |under_test| is ready.
  void LoopUntilReady(const PayloadManager& under_test) {
    RunLoopUntil([&under_test]() { return under_test.ready(); });
  }

  // Provides VMOs via the collection referenced by |token|, verifying the buffer constraints
  // set on the collection.
  void ProvideSysmemVmos(FakeServiceProvider* service_provider,
                         fuchsia::sysmem::BufferCollectionTokenPtr token, uint32_t cpu_usage,
                         uint32_t constraint_payload_count, uint64_t constraint_payload_size,
                         uint32_t collection_payload_count, uint64_t collection_payload_size) {
    EXPECT_TRUE(!!token);

    bool callback_called = false;
    token->Sync([&callback_called]() { callback_called = true; });
    RunLoopUntil([&callback_called]() { return callback_called; });

    auto collection = service_provider->GetCollectionFromToken(std::move(token));
    RunLoopUntil([&collection]() { return !collection->constraints().empty(); });

    EXPECT_EQ(1u, collection->constraints().size());

    auto& constraints = collection->constraints()[0];
    EXPECT_EQ(0u, constraints.usage.none);
    EXPECT_EQ(cpu_usage, constraints.usage.cpu);
    EXPECT_EQ(0u, constraints.usage.vulkan);
    EXPECT_EQ(0u, constraints.usage.display);
    EXPECT_EQ(0u, constraints.usage.video);
    EXPECT_EQ(constraint_payload_count, constraints.min_buffer_count_for_camping);
    EXPECT_EQ(0u, constraints.min_buffer_count_for_dedicated_slack);
    EXPECT_EQ(0u, constraints.min_buffer_count_for_shared_slack);
    EXPECT_EQ(0u, constraints.min_buffer_count);
    EXPECT_EQ(0u, constraints.max_buffer_count);
    EXPECT_TRUE(constraints.has_buffer_memory_constraints);
    EXPECT_EQ(constraint_payload_size, constraints.buffer_memory_constraints.min_size_bytes);

    fuchsia::sysmem::BufferCollectionInfo_2 info;
    info.buffer_count = collection_payload_count;
    for (uint32_t i = 0; i < collection_payload_count; ++i) {
      zx::vmo vmo;
      EXPECT_EQ(ZX_OK, zx::vmo::create(collection_payload_size, 0, &vmo));
      info.buffers[i].vmo = std::move(vmo);
      info.buffers[i].vmo_usable_start = 0;
    }

    collection->SetBufferCollection(ZX_OK, std::move(info));
  }

  void ExpectVmoProvisioning(const PayloadVmos& payload_vmos, uint32_t expected_count,
                             uint64_t min_size) {
    auto vmos = payload_vmos.GetVmos();
    EXPECT_EQ(expected_count, vmos.size());
    for (size_t i = 0; i < expected_count; ++i) {
      // VMO is large enough and is mapped.
      EXPECT_LE(min_size, vmos[i]->size());
      EXPECT_NE(nullptr, vmos[i]->start());
    }
  }
};

///////////////////////////////////////////////////////////////////////////////////////////////////
// The following 20 tests address the 5x4 matrix of possible |PayloadMode| configurations.

// Tests behavior when both output and input modes are |kUsesLocalMemory|.
TEST_F(PayloadManagerTest, UsesLocal_UsesLocal) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyOutputConfiguration(
      PayloadConfig{.mode_ = PayloadMode::kUsesLocalMemory,
                    .max_aggregate_payload_size_ = 0,
                    .max_payload_count_ = 0,
                    .max_payload_size_ = 0,
                    .vmo_allocation_ = VmoAllocation::kNotApplicable,
                    .map_flags_ = ZX_VM_PERM_WRITE});
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesLocalMemory,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = 0,
                                                   .max_payload_size_ = 0,
                                                   .vmo_allocation_ = VmoAllocation::kNotApplicable,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr);
  LoopUntilReady(under_test);

  // A local memory allocator is used for allocation, and payloads are not copied.
  EXPECT_EQ(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_NE(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_FALSE(under_test.must_copy_for_testing());
}

// Tests behavior when output mode is |kProvidesLocalMemory| and input mode is |kUsesLocalMemory|.
TEST_F(PayloadManagerTest, ProvidesLocal_UsesLocal) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyOutputConfiguration(
      PayloadConfig{.mode_ = PayloadMode::kProvidesLocalMemory,
                    .max_aggregate_payload_size_ = 0,
                    .max_payload_count_ = 0,
                    .max_payload_size_ = 0,
                    .vmo_allocation_ = VmoAllocation::kNotApplicable,
                    .map_flags_ = 0});
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesLocalMemory,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = 0,
                                                   .max_payload_size_ = 0,
                                                   .vmo_allocation_ = VmoAllocation::kNotApplicable,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr);
  LoopUntilReady(under_test);

  // No allocators are created, and payloads are not copied.
  EXPECT_EQ(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_FALSE(under_test.must_copy_for_testing());
}

// Tests behavior when output mode is |kUsesVmos| and input mode is |kUsesLocalMemory|.
TEST_F(PayloadManagerTest, UsesVmos_UsesLocal) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  uint32_t output_payload_count = 3;
  uint64_t output_payload_size = 4000;
  uint32_t input_payload_count = 4;
  uint64_t input_payload_size = 3000;

  under_test.ApplyOutputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesVmos,
                                                    .max_aggregate_payload_size_ = 0,
                                                    .max_payload_count_ = output_payload_count,
                                                    .max_payload_size_ = output_payload_size,
                                                    .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                    .map_flags_ = ZX_VM_PERM_WRITE});
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesLocalMemory,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = input_payload_count,
                                                   .max_payload_size_ = input_payload_size,
                                                   .vmo_allocation_ = VmoAllocation::kNotApplicable,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr);
  LoopUntilReady(under_test);

  // One allocator is created and provisioned. Payloads are not copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_EQ(under_test.output_vmo_payload_allocator_for_testing(),
            under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_FALSE(under_test.must_copy_for_testing());

  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.output_vmo_payload_allocator_for_testing()->vmo_allocation());

  ExpectVmoProvisioning(under_test.output_vmos(), output_payload_count + input_payload_count,
                        std::max(output_payload_size, input_payload_size));
}

// Tests behavior when output mode is |kProvidesVmos| and input mode is |kUsesLocalMemory|.
TEST_F(PayloadManagerTest, ProvidesVmos_UsesLocal) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyOutputConfiguration(PayloadConfig{.mode_ = PayloadMode::kProvidesVmos,
                                                    .max_aggregate_payload_size_ = 0,
                                                    .max_payload_count_ = 0,
                                                    .max_payload_size_ = 0,
                                                    .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                    .map_flags_ = ZX_VM_PERM_WRITE});
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesLocalMemory,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = 0,
                                                   .max_payload_size_ = 0,
                                                   .vmo_allocation_ = VmoAllocation::kNotApplicable,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr);
  LoopUntilReady(under_test);

  // One allocator is created but not provisioned. Payloads are not copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_EQ(under_test.output_vmo_payload_allocator_for_testing(),
            under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_FALSE(under_test.must_copy_for_testing());

  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.output_vmo_payload_allocator_for_testing()->vmo_allocation());

  ExpectVmoProvisioning(under_test.output_vmos(), 0, 0);

  // |output_external_vmos| should work.
  EXPECT_TRUE(under_test.output_external_vmos().GetVmos().empty());
}

// Tests behavior when output mode is |kUsesSysmemVmos| and input mode is |kUsesLocalMemory|.
TEST_F(PayloadManagerTest, UsesSysmemVmos_UsesLocal) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  uint32_t input_payload_count = 4;
  uint64_t input_payload_size = 3000;
  uint32_t payload_count = 5;
  uint64_t payload_size = 4000;

  FakeServiceProvider service_provider;

  under_test.ApplyOutputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesSysmemVmos,
                                                    .max_aggregate_payload_size_ = 0,
                                                    .max_payload_count_ = 0,
                                                    .max_payload_size_ = 0,
                                                    .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                    .map_flags_ = ZX_VM_PERM_WRITE},
                                      &service_provider);
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesLocalMemory,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = input_payload_count,
                                                   .max_payload_size_ = input_payload_size,
                                                   .vmo_allocation_ = VmoAllocation::kNotApplicable,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr);
  EXPECT_FALSE(under_test.ready());

  ProvideSysmemVmos(&service_provider, under_test.TakeOutputSysmemToken(), kCpuUsageRead,
                    input_payload_count, input_payload_size, payload_count, payload_size);
  LoopUntilReady(under_test);

  // One allocator is created and provisioned. Payloads are not copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_EQ(under_test.output_vmo_payload_allocator_for_testing(),
            under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_FALSE(under_test.must_copy_for_testing());

  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.output_vmo_payload_allocator_for_testing()->vmo_allocation());

  ExpectVmoProvisioning(under_test.output_vmos(), payload_count, payload_size);
}

// Tests behavior when output mode is |kUsesLocalMemory| and input mode is |kUsesVmos|.
TEST_F(PayloadManagerTest, UsesLocal_UsesVmos) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  uint32_t output_payload_count = 3;
  uint64_t output_payload_size = 4000;
  uint32_t input_payload_count = 4;
  uint64_t input_payload_size = 3000;

  under_test.ApplyOutputConfiguration(
      PayloadConfig{.mode_ = PayloadMode::kUsesLocalMemory,
                    .max_aggregate_payload_size_ = 0,
                    .max_payload_count_ = output_payload_count,
                    .max_payload_size_ = output_payload_size,
                    .vmo_allocation_ = VmoAllocation::kNotApplicable,
                    .map_flags_ = ZX_VM_PERM_WRITE});
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesVmos,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = input_payload_count,
                                                   .max_payload_size_ = input_payload_size,
                                                   .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr);
  LoopUntilReady(under_test);

  // One allocator is created and provisioned. Payloads are not copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_EQ(under_test.output_vmo_payload_allocator_for_testing(),
            under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_FALSE(under_test.must_copy_for_testing());

  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.output_vmo_payload_allocator_for_testing()->vmo_allocation());

  ExpectVmoProvisioning(under_test.input_vmos(), output_payload_count + input_payload_count,
                        std::max(output_payload_size, input_payload_size));
}

// Tests behavior when output mode is |kProvidesLocalMemory| and input mode is |kUsesVmos|.
TEST_F(PayloadManagerTest, ProvidesLocal_UsesVmos) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  uint32_t output_payload_count = 3;
  uint64_t output_payload_size = 4000;
  uint32_t input_payload_count = 4;
  uint64_t input_payload_size = 3000;

  under_test.ApplyOutputConfiguration(
      PayloadConfig{.mode_ = PayloadMode::kProvidesLocalMemory,
                    .max_aggregate_payload_size_ = 0,
                    .max_payload_count_ = output_payload_count,
                    .max_payload_size_ = output_payload_size,
                    .vmo_allocation_ = VmoAllocation::kNotApplicable,
                    .map_flags_ = 0});
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesVmos,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = input_payload_count,
                                                   .max_payload_size_ = input_payload_size,
                                                   .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr);
  LoopUntilReady(under_test);

  // One allocator is created and provisioned. Payloads are copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_EQ(under_test.output_vmo_payload_allocator_for_testing(),
            under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_TRUE(under_test.must_copy_for_testing());

  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.output_vmo_payload_allocator_for_testing()->vmo_allocation());

  // We only need enough VMOs to meet the input's requirements, because the payloads are copied as
  // the input consumes them. The payload sizes must meet the constraints of both the input and the
  // output.
  ExpectVmoProvisioning(under_test.input_vmos(), input_payload_count,
                        std::max(output_payload_size, input_payload_size));
}

// Tests behavior when output mode is |kUsesVmos| and input mode is |kUsesVmos|.
TEST_F(PayloadManagerTest, UsesVmos_UsesVmos) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  uint32_t output_payload_count = 3;
  uint64_t output_payload_size = 4000;
  uint32_t input_payload_count = 4;
  uint64_t input_payload_size = 3000;

  under_test.ApplyOutputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesVmos,
                                                    .max_aggregate_payload_size_ = 0,
                                                    .max_payload_count_ = output_payload_count,
                                                    .max_payload_size_ = output_payload_size,
                                                    .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                    .map_flags_ = ZX_VM_PERM_WRITE});
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesVmos,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = input_payload_count,
                                                   .max_payload_size_ = input_payload_size,
                                                   .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr);
  LoopUntilReady(under_test);

  // One allocator is created and provisioned. Payloads are not copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_EQ(under_test.output_vmo_payload_allocator_for_testing(),
            under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_FALSE(under_test.must_copy_for_testing());

  // |MaybeAllocatePayloadBufferForCopy| should indicate that copying is not required.
  EXPECT_FALSE(under_test.MaybeAllocatePayloadBufferForCopy(0, nullptr));

  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.output_vmo_payload_allocator_for_testing()->vmo_allocation());

  ExpectVmoProvisioning(under_test.output_vmos(), output_payload_count + input_payload_count,
                        std::max(output_payload_size, input_payload_size));

  // |input_vmos| should work.
  EXPECT_FALSE(under_test.input_vmos().GetVmos().empty());
}

// Tests behavior when output mode is |kProvidesVmos| and input mode is |kUsesVmos|.
TEST_F(PayloadManagerTest, ProvidesVmos_UsesVmos) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyOutputConfiguration(PayloadConfig{.mode_ = PayloadMode::kProvidesVmos,
                                                    .max_aggregate_payload_size_ = 0,
                                                    .max_payload_count_ = 0,
                                                    .max_payload_size_ = 0,
                                                    .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                    .map_flags_ = ZX_VM_PERM_WRITE});
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesVmos,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = 0,
                                                   .max_payload_size_ = 0,
                                                   .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr);
  LoopUntilReady(under_test);

  // One allocator is created but not provisioned. Payloads are not copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_EQ(under_test.output_vmo_payload_allocator_for_testing(),
            under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_FALSE(under_test.must_copy_for_testing());

  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.output_vmo_payload_allocator_for_testing()->vmo_allocation());

  ExpectVmoProvisioning(under_test.output_vmos(), 0, 0);

  // |output_external_vmos| and |input_vmos| should work.
  EXPECT_TRUE(under_test.output_external_vmos().GetVmos().empty());
  EXPECT_TRUE(under_test.input_vmos().GetVmos().empty());
}

// Tests behavior when output mode is |kUsesSysmemVmos| and input mode is |kUsesVmos|.
TEST_F(PayloadManagerTest, UsesSysmemVmos_UsesVmos) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  uint32_t input_payload_count = 4;
  uint64_t input_payload_size = 3000;
  uint32_t payload_count = 5;
  uint64_t payload_size = 4000;

  FakeServiceProvider service_provider;

  under_test.ApplyOutputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesSysmemVmos,
                                                    .max_aggregate_payload_size_ = 0,
                                                    .max_payload_count_ = 0,
                                                    .max_payload_size_ = 0,
                                                    .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                    .map_flags_ = ZX_VM_PERM_WRITE},
                                      &service_provider);
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesVmos,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = input_payload_count,
                                                   .max_payload_size_ = input_payload_size,
                                                   .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr);
  EXPECT_FALSE(under_test.ready());

  ProvideSysmemVmos(&service_provider, under_test.TakeOutputSysmemToken(), kCpuUsageRead,
                    input_payload_count, input_payload_size, payload_count, payload_size);
  LoopUntilReady(under_test);

  // One allocator is created and provisioned. Payloads are not copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_EQ(under_test.output_vmo_payload_allocator_for_testing(),
            under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_FALSE(under_test.must_copy_for_testing());

  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.output_vmo_payload_allocator_for_testing()->vmo_allocation());

  ExpectVmoProvisioning(under_test.output_vmos(), payload_count, payload_size);

  // |input_vmos| should work.
  EXPECT_FALSE(under_test.input_vmos().GetVmos().empty());
}

// Tests behavior when output mode is |kUsesLocalMemory| and input mode is |kProvidesVmos|.
TEST_F(PayloadManagerTest, UsesLocal_ProvidesVmos) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  uint32_t output_payload_count = 3;
  uint64_t output_payload_size = 4000;
  uint32_t input_payload_count = 4;
  uint64_t input_payload_size = 3000;

  under_test.ApplyOutputConfiguration(
      PayloadConfig{.mode_ = PayloadMode::kUsesLocalMemory,
                    .max_aggregate_payload_size_ = 0,
                    .max_payload_count_ = output_payload_count,
                    .max_payload_size_ = output_payload_size,
                    .vmo_allocation_ = VmoAllocation::kNotApplicable,
                    .map_flags_ = ZX_VM_PERM_WRITE});
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kProvidesVmos,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = input_payload_count,
                                                   .max_payload_size_ = input_payload_size,
                                                   .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr);
  LoopUntilReady(under_test);

  // One allocator is created but not provisioned. Payloads are not copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_EQ(under_test.output_vmo_payload_allocator_for_testing(),
            under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_FALSE(under_test.must_copy_for_testing());

  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.output_vmo_payload_allocator_for_testing()->vmo_allocation());

  ExpectVmoProvisioning(under_test.input_vmos(), 0, 0);

  // |input_external_vmos| should work.
  EXPECT_TRUE(under_test.input_external_vmos().GetVmos().empty());
}

// Tests behavior when output mode is |kProvidesLocalMemory| and input mode is |kProvidesVmos|.
TEST_F(PayloadManagerTest, ProvidesLocal_ProvidesVmos) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  uint32_t output_payload_count = 3;
  uint64_t output_payload_size = 4000;
  uint32_t input_payload_count = 4;
  uint64_t input_payload_size = 3000;

  under_test.ApplyOutputConfiguration(
      PayloadConfig{.mode_ = PayloadMode::kProvidesLocalMemory,
                    .max_aggregate_payload_size_ = 0,
                    .max_payload_count_ = output_payload_count,
                    .max_payload_size_ = output_payload_size,
                    .vmo_allocation_ = VmoAllocation::kNotApplicable,
                    .map_flags_ = 0});
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kProvidesVmos,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = input_payload_count,
                                                   .max_payload_size_ = input_payload_size,
                                                   .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr);
  LoopUntilReady(under_test);

  // One allocator is created but not provisioned. Payloads are copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_EQ(under_test.output_vmo_payload_allocator_for_testing(),
            under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_TRUE(under_test.must_copy_for_testing());

  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.output_vmo_payload_allocator_for_testing()->vmo_allocation());

  ExpectVmoProvisioning(under_test.input_vmos(), 0, 0);

  // |input_external_vmos| should work.
  EXPECT_TRUE(under_test.input_external_vmos().GetVmos().empty());
}

// Tests behavior when output mode is |kUsesVmos| and input mode is |kProvidesVmos|.
TEST_F(PayloadManagerTest, UsesVmos_ProvidesVmos) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  uint32_t output_payload_count = 3;
  uint64_t output_payload_size = 4000;
  uint32_t input_payload_count = 4;
  uint64_t input_payload_size = 3000;

  under_test.ApplyOutputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesVmos,
                                                    .max_aggregate_payload_size_ = 0,
                                                    .max_payload_count_ = output_payload_count,
                                                    .max_payload_size_ = output_payload_size,
                                                    .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                    .map_flags_ = ZX_VM_PERM_WRITE});
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kProvidesVmos,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = input_payload_count,
                                                   .max_payload_size_ = input_payload_size,
                                                   .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr);
  LoopUntilReady(under_test);

  // One allocator is created but not provisioned. Payloads are not copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_EQ(under_test.output_vmo_payload_allocator_for_testing(),
            under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_FALSE(under_test.must_copy_for_testing());

  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.output_vmo_payload_allocator_for_testing()->vmo_allocation());

  ExpectVmoProvisioning(under_test.input_vmos(), 0, 0);

  // |output_vmos| and |input_external_vmos| should work.
  EXPECT_TRUE(under_test.output_vmos().GetVmos().empty());
  EXPECT_TRUE(under_test.input_external_vmos().GetVmos().empty());
}

// Tests behavior when output mode is |kProvidesVmos| and input mode is |kProvidesVmos|.
TEST_F(PayloadManagerTest, ProvidesVmos_ProvidesVmos) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyOutputConfiguration(PayloadConfig{.mode_ = PayloadMode::kProvidesVmos,
                                                    .max_aggregate_payload_size_ = 0,
                                                    .max_payload_count_ = 0,
                                                    .max_payload_size_ = 0,
                                                    .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                    .map_flags_ = ZX_VM_PERM_WRITE});
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kProvidesVmos,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = 0,
                                                   .max_payload_size_ = 0,
                                                   .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr);
  LoopUntilReady(under_test);

  // Two allocators are created but not provisioned. Payloads are copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_NE(nullptr, under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_NE(under_test.output_vmo_payload_allocator_for_testing(),
            under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_TRUE(under_test.must_copy_for_testing());

  // |MaybeAllocatePayloadBufferForCopy| should indicate that copying is required.
  EXPECT_TRUE(under_test.MaybeAllocatePayloadBufferForCopy(0, nullptr));

  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.output_vmo_payload_allocator_for_testing()->vmo_allocation());
  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.input_vmo_payload_allocator_for_testing()->vmo_allocation());

  ExpectVmoProvisioning(under_test.output_vmos(), 0, 0);
  ExpectVmoProvisioning(under_test.input_vmos(), 0, 0);

  // |output_external_vmos| and |input_external_vmos| should work.
  EXPECT_TRUE(under_test.output_external_vmos().GetVmos().empty());
  EXPECT_TRUE(under_test.input_external_vmos().GetVmos().empty());
}

// Tests behavior when output mode is |kUsesSysmemVmos| and input mode is |kProvidesVmos|.
TEST_F(PayloadManagerTest, UsesSysmemVmos_ProvidesVmos) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  uint32_t input_payload_count = 4;
  uint64_t input_payload_size = 3000;
  uint32_t payload_count = 5;
  uint64_t payload_size = 4000;

  FakeServiceProvider service_provider;

  under_test.ApplyOutputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesSysmemVmos,
                                                    .max_aggregate_payload_size_ = 0,
                                                    .max_payload_count_ = 0,
                                                    .max_payload_size_ = 0,
                                                    .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                    .map_flags_ = ZX_VM_PERM_WRITE},
                                      &service_provider);
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kProvidesVmos,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = input_payload_count,
                                                   .max_payload_size_ = input_payload_size,
                                                   .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr);
  EXPECT_FALSE(under_test.ready());

  ProvideSysmemVmos(&service_provider, under_test.TakeOutputSysmemToken(), kCpuUsageRead,
                    input_payload_count, input_payload_size, payload_count, payload_size);
  LoopUntilReady(under_test);

  // Two allocators are created, only the output allocator is provisioned. Payloads are copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_NE(nullptr, under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_NE(under_test.output_vmo_payload_allocator_for_testing(),
            under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_TRUE(under_test.must_copy_for_testing());

  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.output_vmo_payload_allocator_for_testing()->vmo_allocation());
  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.input_vmo_payload_allocator_for_testing()->vmo_allocation());

  ExpectVmoProvisioning(under_test.output_vmos(), payload_count, payload_size);
  ExpectVmoProvisioning(under_test.input_vmos(), 0, 0);

  // |input_external_vmos| should work.
  EXPECT_TRUE(under_test.input_external_vmos().GetVmos().empty());
}

// Tests behavior when output mode is |kUsesLocalMemory| and input mode is |kUsesSysmemVmos|.
TEST_F(PayloadManagerTest, UsesLocal_UsesSysmemVmos) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  uint32_t output_payload_count = 4;
  uint64_t output_payload_size = 3000;
  uint32_t payload_count = 5;
  uint64_t payload_size = 4000;

  FakeServiceProvider service_provider;

  under_test.ApplyOutputConfiguration(
      PayloadConfig{.mode_ = PayloadMode::kUsesLocalMemory,
                    .max_aggregate_payload_size_ = 0,
                    .max_payload_count_ = output_payload_count,
                    .max_payload_size_ = output_payload_size,
                    .vmo_allocation_ = VmoAllocation::kNotApplicable,
                    .map_flags_ = ZX_VM_PERM_WRITE});
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesSysmemVmos,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = 0,
                                                   .max_payload_size_ = 0,
                                                   .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr, &service_provider);

  ProvideSysmemVmos(&service_provider, under_test.TakeInputSysmemToken(), kCpuUsageWrite,
                    output_payload_count, output_payload_size, payload_count, payload_size);
  LoopUntilReady(under_test);

  // One allocator is created and provisioned. Payloads are not copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_EQ(under_test.output_vmo_payload_allocator_for_testing(),
            under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_FALSE(under_test.must_copy_for_testing());

  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.output_vmo_payload_allocator_for_testing()->vmo_allocation());

  ExpectVmoProvisioning(under_test.input_vmos(), payload_count, payload_size);
}

// Tests behavior when output mode is |kProvidesLocalMemory| and input mode is |kUsesSysmemVmos|.
TEST_F(PayloadManagerTest, ProvidesLocal_UsesSysmemVmos) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  uint32_t output_payload_count = 4;
  uint64_t output_payload_size = 3000;
  uint32_t payload_count = 5;
  uint64_t payload_size = 4000;

  FakeServiceProvider service_provider;

  under_test.ApplyOutputConfiguration(
      PayloadConfig{.mode_ = PayloadMode::kProvidesLocalMemory,
                    .max_aggregate_payload_size_ = 0,
                    .max_payload_count_ = output_payload_count,
                    .max_payload_size_ = output_payload_size,
                    .vmo_allocation_ = VmoAllocation::kNotApplicable,
                    .map_flags_ = 0});
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesSysmemVmos,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = 0,
                                                   .max_payload_size_ = 0,
                                                   .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr, &service_provider);

  ProvideSysmemVmos(&service_provider, under_test.TakeInputSysmemToken(), kCpuUsageWrite, 0,
                    output_payload_size, payload_count, payload_size);
  LoopUntilReady(under_test);

  // One allocator is created and provisioned. Payloads are copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_EQ(under_test.output_vmo_payload_allocator_for_testing(),
            under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_TRUE(under_test.must_copy_for_testing());

  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.output_vmo_payload_allocator_for_testing()->vmo_allocation());

  // We only need enough VMOs to meet the input's requirements, because the payloads are copied as
  // the input consumes them. The payload sizes must meet the constraints of both the input and the
  // output.
  ExpectVmoProvisioning(under_test.input_vmos(), payload_count, payload_size);
}

// Tests behavior when output mode is |kUsesVmos| and input mode is |kUsesSysmemVmos|.
TEST_F(PayloadManagerTest, UsesVmos_UsesSysmemVmos) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  uint32_t output_payload_count = 4;
  uint64_t output_payload_size = 3000;
  uint32_t payload_count = 5;
  uint64_t payload_size = 4000;

  FakeServiceProvider service_provider;

  under_test.ApplyOutputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesVmos,
                                                    .max_aggregate_payload_size_ = 0,
                                                    .max_payload_count_ = output_payload_count,
                                                    .max_payload_size_ = output_payload_size,
                                                    .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                    .map_flags_ = ZX_VM_PERM_WRITE});
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesSysmemVmos,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = 0,
                                                   .max_payload_size_ = 0,
                                                   .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr, &service_provider);

  ProvideSysmemVmos(&service_provider, under_test.TakeInputSysmemToken(), kCpuUsageWrite,
                    output_payload_count, output_payload_size, payload_count, payload_size);
  LoopUntilReady(under_test);

  // One allocator is created and provisioned. Payloads are not copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_EQ(under_test.output_vmo_payload_allocator_for_testing(),
            under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_FALSE(under_test.must_copy_for_testing());

  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.output_vmo_payload_allocator_for_testing()->vmo_allocation());

  ExpectVmoProvisioning(under_test.output_vmos(), payload_count, payload_size);

  // |input_vmos| should work.
  EXPECT_FALSE(under_test.input_vmos().GetVmos().empty());
}

// Tests behavior when output mode is |kProvidesVmos| and input mode is |kUsesSysmemVmos|.
TEST_F(PayloadManagerTest, ProvidesVmos_UsesSysmemVmos) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  uint32_t output_payload_count = 4;
  uint64_t output_payload_size = 3000;
  uint32_t payload_count = 5;
  uint64_t payload_size = 4000;

  FakeServiceProvider service_provider;

  under_test.ApplyOutputConfiguration(PayloadConfig{.mode_ = PayloadMode::kProvidesVmos,
                                                    .max_aggregate_payload_size_ = 0,
                                                    .max_payload_count_ = output_payload_count,
                                                    .max_payload_size_ = output_payload_size,
                                                    .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                    .map_flags_ = ZX_VM_PERM_WRITE});
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesSysmemVmos,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = 0,
                                                   .max_payload_size_ = 0,
                                                   .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr, &service_provider);

  ProvideSysmemVmos(&service_provider, under_test.TakeInputSysmemToken(), kCpuUsageWrite, 0,
                    output_payload_size, payload_count, payload_size);
  LoopUntilReady(under_test);

  // Two allocators are created but only the input allocator is provisioned. Payloads are copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_NE(nullptr, under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_NE(under_test.output_vmo_payload_allocator_for_testing(),
            under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_TRUE(under_test.must_copy_for_testing());

  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.output_vmo_payload_allocator_for_testing()->vmo_allocation());
  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.input_vmo_payload_allocator_for_testing()->vmo_allocation());

  ExpectVmoProvisioning(under_test.output_vmos(), 0, 0);
  ExpectVmoProvisioning(under_test.input_vmos(), payload_count, payload_size);

  // |output_external_vmos| should work.
  EXPECT_TRUE(under_test.output_external_vmos().GetVmos().empty());
}

// Tests behavior when output mode is |kUsesSysmemVmos| and input mode is |kUsesSysmemVmos|.
TEST_F(PayloadManagerTest, UsesSysmemVmos_UsesSysmemVmos) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  uint32_t output_payload_count = 4;
  uint64_t output_payload_size = 3000;
  uint32_t input_payload_count = 5;
  uint64_t input_payload_size = 4000;
  uint32_t payload_count = 6;
  uint64_t payload_size = 5000;

  FakeServiceProvider service_provider;

  under_test.ApplyOutputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesSysmemVmos,
                                                    .max_aggregate_payload_size_ = 0,
                                                    .max_payload_count_ = output_payload_count,
                                                    .max_payload_size_ = output_payload_size,
                                                    .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                    .map_flags_ = ZX_VM_PERM_WRITE},
                                      &service_provider);
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesSysmemVmos,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = input_payload_count,
                                                   .max_payload_size_ = input_payload_size,
                                                   .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr, &service_provider);

  // No allocators are created, and output and input share the same sysmem collection.
  bool callback_called = false;
  auto output_token = under_test.TakeOutputSysmemToken();
  output_token->Sync([&callback_called]() { callback_called = true; });
  RunLoopUntil([&callback_called]() { return callback_called; });

  callback_called = false;
  auto input_token = under_test.TakeInputSysmemToken();
  input_token->Sync([&callback_called]() { callback_called = true; });
  RunLoopUntil([&callback_called]() { return callback_called; });

  auto output_collection = service_provider.GetCollectionFromToken(std::move(output_token));
  auto input_collection = service_provider.GetCollectionFromToken(std::move(input_token));
  EXPECT_EQ(output_collection, input_collection);

  // No constraints are set...the nodes are supposed to do that.

  fuchsia::sysmem::BufferCollectionInfo_2 info;
  info.buffer_count = payload_count;
  for (uint32_t i = 0; i < payload_count; ++i) {
    zx::vmo vmo;
    EXPECT_EQ(ZX_OK, zx::vmo::create(payload_size, 0, &vmo));
    info.buffers[i].vmo = std::move(vmo);
    info.buffers[i].vmo_usable_start = 0;
  }

  input_collection->SetBufferCollection(ZX_OK, std::move(info));

  LoopUntilReady(under_test);

  // One allocator (so we know how many buffers are in the collection). Payloads are not copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_NE(nullptr, under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_FALSE(under_test.must_copy_for_testing());
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// The following tests address cases in the 5x4 matrix of possible |PayloadMode| configurations in
// which the configurations are not compatible.

// Tests behavior when output mode is |kProvidesVmos| and input mode is |kUsesVmos|, and the
// configurations are not compatible.
TEST_F(PayloadManagerTest, ProvidesVmos_UsesVmos_NotCompatible) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  uint64_t output_payload_size = 5000;
  uint32_t input_payload_count = 5;
  uint64_t input_payload_size = 4000;

  under_test.ApplyOutputConfiguration(PayloadConfig{.mode_ = PayloadMode::kProvidesVmos,
                                                    .max_aggregate_payload_size_ = 0,
                                                    .max_payload_count_ = 0,
                                                    .max_payload_size_ = output_payload_size,
                                                    .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                    .map_flags_ = ZX_VM_PERM_WRITE});
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesVmos,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = input_payload_count,
                                                   .max_payload_size_ = input_payload_size,
                                                   .vmo_allocation_ = VmoAllocation::kSingleVmo,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr);
  LoopUntilReady(under_test);

  // Two allocators are created, but only the input allocator provisioned. Payloads are copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_NE(nullptr, under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_NE(under_test.output_vmo_payload_allocator_for_testing(),
            under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_TRUE(under_test.must_copy_for_testing());

  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.output_vmo_payload_allocator_for_testing()->vmo_allocation());
  EXPECT_EQ(VmoAllocation::kSingleVmo,
            under_test.input_vmo_payload_allocator_for_testing()->vmo_allocation());

  ExpectVmoProvisioning(under_test.input_vmos(), 1,
                        std::max(output_payload_size, input_payload_size) * input_payload_count);

  // |output_vmos| and |output_external_vmos| should work.
  EXPECT_TRUE(under_test.output_vmos().GetVmos().empty());
  EXPECT_TRUE(under_test.output_external_vmos().GetVmos().empty());
}

// Tests behavior when output mode is |kUsesSysmemVmos| and input mode is |kUsesVmos|, and the
// configurations are not compatible.
TEST_F(PayloadManagerTest, UsesSysmemVmos_UsesVmos_NotCompatible) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  uint32_t output_payload_count = 5;
  uint64_t output_payload_size = 4000;
  uint32_t input_payload_count = 4;
  uint64_t input_payload_size = 3000;
  uint32_t payload_count = 1;
  uint64_t payload_size = std::max(output_payload_size, input_payload_size) *
                          (output_payload_count + input_payload_count);

  FakeServiceProvider service_provider;

  under_test.ApplyOutputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesSysmemVmos,
                                                    .max_aggregate_payload_size_ = 0,
                                                    .max_payload_count_ = output_payload_count,
                                                    .max_payload_size_ = output_payload_size,
                                                    .vmo_allocation_ = VmoAllocation::kSingleVmo,
                                                    .map_flags_ = ZX_VM_PERM_WRITE},
                                      &service_provider);
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesVmos,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = input_payload_count,
                                                   .max_payload_size_ = input_payload_size,
                                                   .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr);
  EXPECT_FALSE(under_test.ready());

  ProvideSysmemVmos(&service_provider, under_test.TakeOutputSysmemToken(), kCpuUsageRead,
                    input_payload_count, input_payload_size, payload_count, payload_size);
  LoopUntilReady(under_test);

  // Two allocators are created and provisioned. Payloads are not copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_NE(nullptr, under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_NE(under_test.output_vmo_payload_allocator_for_testing(),
            under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_TRUE(under_test.must_copy_for_testing());

  EXPECT_EQ(VmoAllocation::kSingleVmo,
            under_test.output_vmo_payload_allocator_for_testing()->vmo_allocation());
  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.input_vmo_payload_allocator_for_testing()->vmo_allocation());

  ExpectVmoProvisioning(under_test.output_vmos(), payload_count, payload_size);
  ExpectVmoProvisioning(under_test.input_vmos(), input_payload_count,
                        std::max(output_payload_size, input_payload_size));
}

// Tests behavior when output mode is |kUsesVmos| and input mode is |kProvidesVmos|, and the
// configurations are not compatible.
TEST_F(PayloadManagerTest, UsesVmos_ProvidesVmos_NotCompatible) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  uint32_t output_payload_count = 3;
  uint64_t output_payload_size = 4000;
  uint32_t input_payload_count = 4;
  uint64_t input_payload_size = 3000;

  under_test.ApplyOutputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesVmos,
                                                    .max_aggregate_payload_size_ = 0,
                                                    .max_payload_count_ = output_payload_count,
                                                    .max_payload_size_ = output_payload_size,
                                                    .vmo_allocation_ = VmoAllocation::kSingleVmo,
                                                    .map_flags_ = ZX_VM_PERM_WRITE});
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kProvidesVmos,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = input_payload_count,
                                                   .max_payload_size_ = input_payload_size,
                                                   .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr);
  LoopUntilReady(under_test);

  // Two allocators are created, but only the output allocator is provisioned. Payloads are copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_NE(nullptr, under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_NE(under_test.output_vmo_payload_allocator_for_testing(),
            under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_TRUE(under_test.must_copy_for_testing());

  EXPECT_EQ(VmoAllocation::kSingleVmo,
            under_test.output_vmo_payload_allocator_for_testing()->vmo_allocation());
  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.input_vmo_payload_allocator_for_testing()->vmo_allocation());

  ExpectVmoProvisioning(under_test.output_vmos(), 1,
                        std::max(output_payload_size, input_payload_size) * output_payload_count);
  ExpectVmoProvisioning(under_test.input_vmos(), 0, 0);

  // |input_external_vmos| should work.
  EXPECT_TRUE(under_test.input_external_vmos().GetVmos().empty());
}

// Tests behavior when output mode is |kUsesVmos| and input mode is |kUsesSysmemVmos|, and the
// configurations are not compatible.
TEST_F(PayloadManagerTest, UsesVmos_UsesSysmemVmos_NotCompatible) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  uint32_t output_payload_count = 4;
  uint64_t output_payload_size = 5000;
  uint32_t input_payload_count = 2;
  uint64_t input_payload_size = 4000;
  uint32_t payload_count = 6;
  uint64_t payload_size = 5000;

  FakeServiceProvider service_provider;

  under_test.ApplyOutputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesVmos,
                                                    .max_aggregate_payload_size_ = 0,
                                                    .max_payload_count_ = output_payload_count,
                                                    .max_payload_size_ = output_payload_size,
                                                    .vmo_allocation_ = VmoAllocation::kSingleVmo,
                                                    .map_flags_ = ZX_VM_PERM_WRITE});
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesSysmemVmos,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = input_payload_count,
                                                   .max_payload_size_ = input_payload_size,
                                                   .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr, &service_provider);

  ProvideSysmemVmos(&service_provider, under_test.TakeInputSysmemToken(), kCpuUsageWrite, 0u,
                    output_payload_size, payload_count, payload_size);
  LoopUntilReady(under_test);

  // Two allocators are created and provisioned. Payloads are copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_NE(nullptr, under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_NE(under_test.output_vmo_payload_allocator_for_testing(),
            under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_TRUE(under_test.must_copy_for_testing());

  EXPECT_EQ(VmoAllocation::kSingleVmo,
            under_test.output_vmo_payload_allocator_for_testing()->vmo_allocation());
  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.input_vmo_payload_allocator_for_testing()->vmo_allocation());

  ExpectVmoProvisioning(under_test.output_vmos(), 1,
                        std::max(output_payload_size, input_payload_size) * output_payload_count);
  ExpectVmoProvisioning(under_test.input_vmos(), payload_count, payload_size);
}

// Tests behavior when output mode is |kUsesSysmemVmos| and input mode is |kUsesSysmemVmos|, and the
// configurations are not compatible.
TEST_F(PayloadManagerTest, UsesSysmemVmos_UsesSysmemVmos_NotCompatible) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  uint32_t output_payload_count = 4;
  uint64_t output_payload_size = 3000;
  uint32_t input_payload_count = 5;
  uint64_t input_payload_size = 4000;
  uint32_t payload_count = 6;
  uint64_t payload_size = 5000;

  FakeServiceProvider service_provider;

  under_test.ApplyOutputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesSysmemVmos,
                                                    .max_aggregate_payload_size_ = 0,
                                                    .max_payload_count_ = output_payload_count,
                                                    .max_payload_size_ = output_payload_size,
                                                    .vmo_allocation_ = VmoAllocation::kSingleVmo,
                                                    .map_flags_ = ZX_VM_PERM_WRITE},
                                      &service_provider);
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesSysmemVmos,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = input_payload_count,
                                                   .max_payload_size_ = input_payload_size,
                                                   .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr, &service_provider);

  ProvideSysmemVmos(&service_provider, under_test.TakeOutputSysmemToken(), kCpuUsageRead,
                    input_payload_count, input_payload_size, 1, payload_size);
  ProvideSysmemVmos(&service_provider, under_test.TakeInputSysmemToken(), kCpuUsageWrite, 0,
                    output_payload_size, payload_count, payload_size);
  LoopUntilReady(under_test);

  // Two allocators are created and provisioned. Payloads are copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_NE(nullptr, under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_NE(under_test.output_vmo_payload_allocator_for_testing(),
            under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_TRUE(under_test.must_copy_for_testing());

  EXPECT_EQ(VmoAllocation::kSingleVmo,
            under_test.output_vmo_payload_allocator_for_testing()->vmo_allocation());
  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.input_vmo_payload_allocator_for_testing()->vmo_allocation());

  ExpectVmoProvisioning(under_test.output_vmos(), 1, payload_size);
  ExpectVmoProvisioning(under_test.input_vmos(), payload_count, payload_size);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// The following tests address miscellaneous features of |PayloadManager| not addressed above.

// Tests behavior when both output and input modes are |kUsesLocalMemory|, configuring the input
// first.
TEST_F(PayloadManagerTest, UsesLocal_UsesLocal_InputFirst) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesLocalMemory,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = 0,
                                                   .max_payload_size_ = 0,
                                                   .vmo_allocation_ = VmoAllocation::kNotApplicable,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr);
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyOutputConfiguration(
      PayloadConfig{.mode_ = PayloadMode::kUsesLocalMemory,
                    .max_aggregate_payload_size_ = 0,
                    .max_payload_count_ = 0,
                    .max_payload_size_ = 0,
                    .vmo_allocation_ = VmoAllocation::kNotApplicable,
                    .map_flags_ = ZX_VM_PERM_WRITE});
  LoopUntilReady(under_test);

  // A local memory allocator is used for allocation, and payloads are not copied.
  EXPECT_EQ(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_NE(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_FALSE(under_test.must_copy_for_testing());
}

// Tests behavior when output mode is |kProvidesLocalMemory| and input mode is |kUsesVmos|.
// The VMO allocation for the input is given as |kUnrestricted|, which should be resolved to
// |kSingleVmo|.
TEST_F(PayloadManagerTest, ProvidesLocal_UsesVmos_UnrestrictedBecomesSingleVmo) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  uint32_t input_payload_count = 4;
  uint64_t input_payload_size = 3000;

  under_test.ApplyOutputConfiguration(
      PayloadConfig{.mode_ = PayloadMode::kProvidesLocalMemory,
                    .max_aggregate_payload_size_ = 0,
                    .max_payload_count_ = 0,
                    .max_payload_size_ = 0,
                    .vmo_allocation_ = VmoAllocation::kNotApplicable,
                    .map_flags_ = 0});
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesVmos,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = input_payload_count,
                                                   .max_payload_size_ = input_payload_size,
                                                   .vmo_allocation_ = VmoAllocation::kUnrestricted,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr);
  LoopUntilReady(under_test);

  // One allocator is created and provisioned. Payloads are copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_EQ(under_test.output_vmo_payload_allocator_for_testing(),
            under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_TRUE(under_test.must_copy_for_testing());

  EXPECT_EQ(VmoAllocation::kSingleVmo,
            under_test.input_vmo_payload_allocator_for_testing()->vmo_allocation());
}

// Tests behavior when output mode is |kProvidesLocalMemory| and input mode is |kUsesVmos|.
// The input allocation mode is |kVmoPerBuffer|. |max_aggregate_payload_size_| is specified and
// and |max_payload_count_| is not, which should be resolved by performing the division.
TEST_F(PayloadManagerTest, ProvidesLocal_UsesVmos_SizeFromCountAndAggregateSize) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  uint64_t input_aggregate_payload_size = 2999;
  uint32_t input_payload_count = 3;
  uint64_t payload_size = 1000;

  under_test.ApplyOutputConfiguration(
      PayloadConfig{.mode_ = PayloadMode::kProvidesLocalMemory,
                    .max_aggregate_payload_size_ = 0,
                    .max_payload_count_ = 0,
                    .max_payload_size_ = 0,
                    .vmo_allocation_ = VmoAllocation::kNotApplicable,
                    .map_flags_ = 0});
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(
      PayloadConfig{.mode_ = PayloadMode::kUsesVmos,
                    .max_aggregate_payload_size_ = input_aggregate_payload_size,
                    .max_payload_count_ = input_payload_count,
                    .max_payload_size_ = 0,
                    .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                    .map_flags_ = ZX_VM_PERM_READ},
      nullptr);
  LoopUntilReady(under_test);

  // One allocator is created and provisioned. Payloads are copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_EQ(under_test.output_vmo_payload_allocator_for_testing(),
            under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_TRUE(under_test.must_copy_for_testing());

  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.input_vmo_payload_allocator_for_testing()->vmo_allocation());

  ExpectVmoProvisioning(under_test.input_vmos(), input_payload_count, payload_size);
}

// Tests behavior when output mode is |kProvidesLocalMemory| and input mode is |kUsesVmos|.
// The input allocation mode is |kVmoPerBuffer|. |max_aggregate_payload_size_| is specified and
// |max_payload_size_| is not, which should be resolved by performing the division.
TEST_F(PayloadManagerTest, ProvidesLocal_UsesVmos_CountFromSizeAndAggregateSize) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  uint64_t input_aggregate_payload_size = 3001;
  uint64_t input_payload_size = 1000;
  uint32_t payload_count = 4;

  under_test.ApplyOutputConfiguration(
      PayloadConfig{.mode_ = PayloadMode::kProvidesLocalMemory,
                    .max_aggregate_payload_size_ = 0,
                    .max_payload_count_ = 0,
                    .max_payload_size_ = 0,
                    .vmo_allocation_ = VmoAllocation::kNotApplicable,
                    .map_flags_ = 0});
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(
      PayloadConfig{.mode_ = PayloadMode::kUsesVmos,
                    .max_aggregate_payload_size_ = input_aggregate_payload_size,
                    .max_payload_count_ = 0,
                    .max_payload_size_ = input_payload_size,
                    .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                    .map_flags_ = ZX_VM_PERM_READ},
      nullptr);
  LoopUntilReady(under_test);

  // One allocator is created and provisioned. Payloads are copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_EQ(under_test.output_vmo_payload_allocator_for_testing(),
            under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_TRUE(under_test.must_copy_for_testing());

  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.input_vmo_payload_allocator_for_testing()->vmo_allocation());

  ExpectVmoProvisioning(under_test.input_vmos(), payload_count, input_payload_size);
}

// Tests behavior when output mode is |kProvidesLocalMemory| and input mode is |kUsesVmos|.
// The input allocation mode is |kVmoPerBuffer|. |max_aggregate_payload_size_| and
// |input_payload_size| are specified, and the VMO size should be aligned up to the payload size.
TEST_F(PayloadManagerTest, ProvidesLocal_UsesVmos_AggregateSizeAlignedUp) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  uint64_t input_aggregate_payload_size = 3001;
  uint64_t input_payload_size = 1000;
  uint64_t vmo_size = 4000;

  under_test.ApplyOutputConfiguration(
      PayloadConfig{.mode_ = PayloadMode::kProvidesLocalMemory,
                    .max_aggregate_payload_size_ = 0,
                    .max_payload_count_ = 0,
                    .max_payload_size_ = 0,
                    .vmo_allocation_ = VmoAllocation::kNotApplicable,
                    .map_flags_ = 0});
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(
      PayloadConfig{.mode_ = PayloadMode::kUsesVmos,
                    .max_aggregate_payload_size_ = input_aggregate_payload_size,
                    .max_payload_count_ = 0,
                    .max_payload_size_ = input_payload_size,
                    .vmo_allocation_ = VmoAllocation::kSingleVmo,
                    .map_flags_ = ZX_VM_PERM_READ},
      nullptr);
  LoopUntilReady(under_test);

  // One allocator is created and provisioned. Payloads are copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_EQ(under_test.output_vmo_payload_allocator_for_testing(),
            under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_TRUE(under_test.must_copy_for_testing());

  EXPECT_EQ(VmoAllocation::kSingleVmo,
            under_test.input_vmo_payload_allocator_for_testing()->vmo_allocation());

  ExpectVmoProvisioning(under_test.input_vmos(), 1, vmo_size);
}

// Tests behavior when both output and input modes are |kUsesLocalMemory|. Verifies that
// |RegisterReadyCallbacks| works.
TEST_F(PayloadManagerTest, UsesLocal_UsesLocal_CallbacksCalled) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  bool output_callback_called = false;
  bool input_callback_called = false;
  under_test.RegisterReadyCallbacks([&output_callback_called]() { output_callback_called = true; },
                                    [&input_callback_called]() { input_callback_called = true; });

  under_test.ApplyOutputConfiguration(
      PayloadConfig{.mode_ = PayloadMode::kUsesLocalMemory,
                    .max_aggregate_payload_size_ = 0,
                    .max_payload_count_ = 0,
                    .max_payload_size_ = 0,
                    .vmo_allocation_ = VmoAllocation::kNotApplicable,
                    .map_flags_ = ZX_VM_PERM_WRITE});
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesLocalMemory,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = 0,
                                                   .max_payload_size_ = 0,
                                                   .vmo_allocation_ = VmoAllocation::kNotApplicable,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr);

  RunLoopUntil([&output_callback_called, &input_callback_called]() {
    return output_callback_called && input_callback_called;
  });
  EXPECT_TRUE(under_test.ready());

  output_callback_called = false;
  input_callback_called = false;

  // Make sure we get the callbacks again when we reconfigure.
  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesLocalMemory,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = 0,
                                                   .max_payload_size_ = 0,
                                                   .vmo_allocation_ = VmoAllocation::kNotApplicable,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr);

  RunLoopUntil([&output_callback_called, &input_callback_called]() {
    return output_callback_called && input_callback_called;
  });
}

// Tests behavior when output mode is |kUsesSysmemVmos| and input mode is |kUsesVmos|. The input
// specifies map flags of 0, which should cause the VMOs to not be mapped.
TEST_F(PayloadManagerTest, UsesSysmemVmos_UsesVmos_NoMapping) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  uint32_t input_payload_count = 4;
  uint64_t input_payload_size = 3000;
  uint32_t payload_count = 5;
  uint64_t payload_size = 4000;

  FakeServiceProvider service_provider;

  under_test.ApplyOutputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesSysmemVmos,
                                                    .max_aggregate_payload_size_ = 0,
                                                    .max_payload_count_ = 0,
                                                    .max_payload_size_ = 0,
                                                    .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                    .map_flags_ = ZX_VM_PERM_WRITE},
                                      &service_provider);
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesVmos,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = input_payload_count,
                                                   .max_payload_size_ = input_payload_size,
                                                   .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                   .map_flags_ = 0},
                                     nullptr);
  EXPECT_FALSE(under_test.ready());

  // Note that |kCpuUsageReadWrite| is the result of a hack to allow image pipe to access the
  // VMOS.
  ProvideSysmemVmos(&service_provider, under_test.TakeOutputSysmemToken(), kCpuUsageReadWrite,
                    input_payload_count, input_payload_size, payload_count, payload_size);
  LoopUntilReady(under_test);

  // One allocator is created and provisioned. Payloads are not copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_EQ(under_test.output_vmo_payload_allocator_for_testing(),
            under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_FALSE(under_test.must_copy_for_testing());

  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.output_vmo_payload_allocator_for_testing()->vmo_allocation());

  auto vmos = under_test.output_vmos().GetVmos();
  EXPECT_EQ(payload_count, vmos.size());
  for (size_t i = 0; i < payload_count; ++i) {
    // VMO is large enough and is NOT mapped.
    EXPECT_LE(payload_size, vmos[i]->size());
    EXPECT_EQ(nullptr, vmos[i]->start());
  }

  // |input_vmos| should work.
  EXPECT_FALSE(under_test.input_vmos().GetVmos().empty());
}

// Tests behavior when output mode is |kUsesVmos| and input mode is |kUsesVmos|. Output doesn't
// specify payload size, and input doesn't specify payload count, and values are combined to
// configure the allocator.
TEST_F(PayloadManagerTest, UsesVmos_UsesVmos_CrossConfig) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  uint32_t output_payload_count = 3;
  uint64_t output_payload_size = 0;
  uint32_t input_payload_count = 0;
  uint64_t input_payload_size = 3000;

  under_test.ApplyOutputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesVmos,
                                                    .max_aggregate_payload_size_ = 0,
                                                    .max_payload_count_ = output_payload_count,
                                                    .max_payload_size_ = output_payload_size,
                                                    .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                    .map_flags_ = ZX_VM_PERM_WRITE});
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesVmos,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = input_payload_count,
                                                   .max_payload_size_ = input_payload_size,
                                                   .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr);
  LoopUntilReady(under_test);

  // One allocator is created and provisioned. Payloads are not copied.
  EXPECT_NE(nullptr, under_test.output_vmo_payload_allocator_for_testing());
  EXPECT_EQ(nullptr, under_test.output_local_memory_payload_allocator_for_testing());
  EXPECT_EQ(under_test.output_vmo_payload_allocator_for_testing(),
            under_test.input_vmo_payload_allocator_for_testing());
  EXPECT_FALSE(under_test.must_copy_for_testing());

  EXPECT_EQ(VmoAllocation::kVmoPerBuffer,
            under_test.output_vmo_payload_allocator_for_testing()->vmo_allocation());

  ExpectVmoProvisioning(under_test.output_vmos(), output_payload_count + input_payload_count,
                        std::max(output_payload_size, input_payload_size));

  // |input_vmos| should work.
  EXPECT_FALSE(under_test.input_vmos().GetVmos().empty());
}

// Tests behavior when output mode is |kUsesVmos| and input mode is |kUsesVmos|. Verifies that
// |AllocatePayloadBufferForOutput| works as expected.
TEST_F(PayloadManagerTest, UsesVmos_UsesVmos_Allocation) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  uint32_t output_payload_count = 3;
  uint64_t output_payload_size = 4000;
  uint32_t input_payload_count = 4;
  uint64_t input_payload_size = 3000;

  under_test.ApplyOutputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesVmos,
                                                    .max_aggregate_payload_size_ = 0,
                                                    .max_payload_count_ = output_payload_count,
                                                    .max_payload_size_ = output_payload_size,
                                                    .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                    .map_flags_ = ZX_VM_PERM_WRITE});
  EXPECT_FALSE(under_test.ready());

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesVmos,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = input_payload_count,
                                                   .max_payload_size_ = input_payload_size,
                                                   .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     nullptr);
  LoopUntilReady(under_test);

  uint64_t max_payload_size = std::max(output_payload_size, input_payload_size);

  // Allocation too large.
  EXPECT_EQ(nullptr, under_test.AllocatePayloadBufferForOutput(max_payload_size * 2).get());

  // Valid allocations.
  std::vector<fbl::RefPtr<PayloadBuffer>> buffers;
  for (size_t i = 0; i < output_payload_count + input_payload_count; ++i) {
    buffers.push_back(under_test.AllocatePayloadBufferForOutput(max_payload_size));
    EXPECT_NE(nullptr, buffers.back().get());
    EXPECT_EQ(max_payload_size, buffers.back()->size());
  }

  // Allocator exhausted.
  EXPECT_EQ(nullptr, under_test.AllocatePayloadBufferForOutput(max_payload_size).get());

  buffers.pop_back();

  // Valid allocation.
  auto buffer = under_test.AllocatePayloadBufferForOutput(max_payload_size);
  EXPECT_NE(nullptr, buffer.get());
  EXPECT_EQ(max_payload_size, buffer->size());
}

// Tests behavior when output mode is |kUsesVmos| and input mode is |kUsesVmos|. Ensure that the
// allocate callback supplied to |ApplyInputConfiguration| is used.
TEST_F(PayloadManagerTest, UsesVmos_UsesVmos_AllocateCallback) {
  PayloadManager under_test;
  EXPECT_FALSE(under_test.ready());

  uint32_t output_payload_count = 3;
  uint64_t output_payload_size = 4000;
  uint32_t input_payload_count = 4;
  uint64_t input_payload_size = 3000;

  under_test.ApplyOutputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesVmos,
                                                    .max_aggregate_payload_size_ = 0,
                                                    .max_payload_count_ = output_payload_count,
                                                    .max_payload_size_ = output_payload_size,
                                                    .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                    .map_flags_ = ZX_VM_PERM_WRITE});
  EXPECT_FALSE(under_test.ready());

  uint64_t allocation_size = 1000;
  uint64_t allocation_actual_size = 0;

  under_test.ApplyInputConfiguration(PayloadConfig{.mode_ = PayloadMode::kUsesVmos,
                                                   .max_aggregate_payload_size_ = 0,
                                                   .max_payload_count_ = input_payload_count,
                                                   .max_payload_size_ = input_payload_size,
                                                   .vmo_allocation_ = VmoAllocation::kVmoPerBuffer,
                                                   .map_flags_ = ZX_VM_PERM_READ},
                                     [&allocation_actual_size](uint64_t size, const PayloadVmos&) {
                                       allocation_actual_size = size;
                                       return PayloadBuffer::CreateWithMalloc(size);
                                     });
  LoopUntilReady(under_test);

  auto buffer = under_test.AllocatePayloadBufferForOutput(allocation_size);
  EXPECT_NE(nullptr, buffer.get());
  EXPECT_EQ(allocation_size, buffer->size());

  EXPECT_EQ(allocation_actual_size, allocation_size);
}

}  // namespace media_player::test
