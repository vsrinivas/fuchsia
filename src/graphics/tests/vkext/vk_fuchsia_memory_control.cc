// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <unistd.h>

#include <memory>

#include <fbl/algorithm.h>
#include <gtest/gtest.h>

#include "src/graphics/tests/common/vulkan_context.h"

#include <vulkan/vulkan.hpp>

namespace {

static inline uint32_t page_size() {
#ifdef PAGE_SIZE
  return PAGE_SIZE;
#else
  long page_size = sysconf(_SC_PAGESIZE);
  DASSERT(page_size > 0);
  return to_uint32(page_size);
#endif
}

constexpr uint8_t kDefaultValue = 0x7f;

class MemoryControl : public testing::TestWithParam<uint32_t> {
  void SetUp() override;

 protected:
  vk::UniqueDeviceMemory AllocateAndInitializeDeviceMemory(
      vk::MemoryOpFlagsFUCHSIA supportedOperations);
  std::unique_ptr<VulkanContext> ctx_;
  vk::DispatchLoaderDynamic loader_;
  vk::PhysicalDeviceMemoryControlPropertiesFUCHSIA control_properties_;
  VkDeviceSize allocation_size_{};
  VkDeviceSize expected_memory_size_{};
  uint8_t* mapped_data_{};
  uint32_t memory_type_{};
  bool protected_memory_ = false;
  bool host_visible_memory_ = false;
  bool lazily_allocated_memory_ = false;
};

void MemoryControl::SetUp() {
  constexpr size_t kPhysicalDeviceIndex = 0;
  vk::ApplicationInfo app_info;
  app_info.pApplicationName = "vkext";
  app_info.apiVersion = VK_API_VERSION_1_1;
  vk::InstanceCreateInfo instance_info;
  instance_info.pApplicationInfo = &app_info;
  ctx_ = std::make_unique<VulkanContext>(kPhysicalDeviceIndex);
  ctx_->set_instance_info(instance_info);
  ASSERT_TRUE(ctx_->InitInstance());

  loader_.init(*ctx_->instance(), vkGetInstanceProcAddr);
  ASSERT_TRUE(ctx_->InitQueueFamily());

  auto [result, extensions] = ctx_->physical_device().enumerateDeviceExtensionProperties();
  EXPECT_EQ(result, vk::Result::eSuccess);

  bool found_extension = false;
  for (auto& extension : extensions) {
    if (strcmp(extension.extensionName, VK_FUCHSIA_MEMORY_CONTROL_EXTENSION_NAME) == 0) {
      EXPECT_GE(extension.specVersion, 1u);
      found_extension = true;
      break;
    }
  }
  if (!found_extension) {
    printf(VK_FUCHSIA_MEMORY_CONTROL_EXTENSION_NAME " not found\n");
    GTEST_SKIP();
  }

  vk::PhysicalDeviceProperties2 physical_device_properties;
  physical_device_properties.pNext = &control_properties_;

  ctx_->physical_device().getProperties2(&physical_device_properties);

  if (!control_properties_.wholeMemoryOperations) {
    printf("No memory control operations supported\n");
    GTEST_SKIP();
  }

  EXPECT_NE(0u, control_properties_.memoryTypeBits);
  memory_type_ = GetParam();
  if (!(control_properties_.memoryTypeBits & (1u << memory_type_))) {
    printf("Memory control operations not supported on memory type\n");
    GTEST_SKIP();
  }
  auto memory_properties = ctx_->physical_device().getMemoryProperties();
  auto memory_property_flags = memory_properties.memoryTypes[memory_type_].propertyFlags;
  protected_memory_ =
      static_cast<bool>(memory_property_flags & vk::MemoryPropertyFlagBits::eProtected);
  host_visible_memory_ =
      static_cast<bool>(memory_property_flags & vk::MemoryPropertyFlagBits::eHostVisible);
  lazily_allocated_memory_ =
      static_cast<bool>(memory_property_flags & vk::MemoryPropertyFlagBits::eLazilyAllocated);

  vk::PhysicalDeviceFeatures2 features2;
  vk::PhysicalDeviceProtectedMemoryFeatures protected_features;
  features2.pNext = &protected_features;

  std::vector<const char*> enabled_device_extensions{VK_FUCHSIA_MEMORY_CONTROL_EXTENSION_NAME};
  vk::DeviceCreateInfo device_info;
  device_info.pQueueCreateInfos = &ctx_->queue_info();
  device_info.queueCreateInfoCount = 1;
  device_info.enabledExtensionCount = static_cast<uint32_t>(enabled_device_extensions.size());
  device_info.ppEnabledExtensionNames = enabled_device_extensions.data();

  if (protected_memory_) {
    protected_features.protectedMemory = VK_TRUE;
    device_info.setPNext(&features2);
  }

  ctx_->set_device_info(device_info);
  ASSERT_TRUE(ctx_->InitDevice());
}

vk::UniqueDeviceMemory MemoryControl::AllocateAndInitializeDeviceMemory(
    vk::MemoryOpFlagsFUCHSIA supportedOperations) {
  vk::StructureChain<vk::MemoryAllocateInfo, vk::ControlOpsMemoryAllocateInfoFUCHSIA> alloc_info;
  auto& allocate = alloc_info.get<vk::MemoryAllocateInfo>();
  auto& control = alloc_info.get<vk::ControlOpsMemoryAllocateInfoFUCHSIA>();
  control.supportedOperations = supportedOperations;

  // Test that allocations that aren't page-aligned in size work.
  allocate.allocationSize = control_properties_.memoryOperationGranularity * 1024 + 1;
  allocation_size_ = allocate.allocationSize;
  allocate.memoryTypeIndex = memory_type_;
  auto [result, vk_device_memory] =
      ctx_->device()->allocateMemoryUnique(alloc_info.get<vk::MemoryAllocateInfo>());

  expected_memory_size_ = fbl::round_up(allocate.allocationSize, page_size());

  EXPECT_EQ(vk::Result::eSuccess, result);

  if (host_visible_memory_) {
    auto [map_result, data] =
        ctx_->device()->mapMemory(vk_device_memory.get(), 0, VK_WHOLE_SIZE, {});

    EXPECT_EQ(vk::Result::eSuccess, map_result);
    memset(data, kDefaultValue, allocate.allocationSize);
    mapped_data_ = static_cast<uint8_t*>(data);
  }
  return std::move(vk_device_memory);
}

TEST_P(MemoryControl, Whole) {
  auto vk_device_memory =
      AllocateAndInitializeDeviceMemory(control_properties_.wholeMemoryOperations);

  vk::MemoryRangeFUCHSIA range;
  range.memory = vk_device_memory.get();
  range.offset = 0;
  range.size = allocation_size_;

  EXPECT_EQ(vk::Result::eSuccess,
            ctx_->device()
                ->modifyMemoryRangeFUCHSIA(vk::MemoryOpFlagBitsFUCHSIA::eUnpin, range, loader_)
                .result);

  if (host_visible_memory_) {
    EXPECT_EQ(kDefaultValue, mapped_data_[0]);
  }
  VkDeviceSize memory_size = ctx_->device()->getMemoryCommitment(vk_device_memory.get());
  // Memory is not 0, so the zero page decommitter can't decommit it.
  EXPECT_EQ(memory_size, expected_memory_size_);

  // Additionally test using VK_WHOLE_SIZE instead of a specific size.
  range.size = VK_WHOLE_SIZE;
  if (control_properties_.wholeMemoryOperations & vk::MemoryOpFlagBitsFUCHSIA::eDecommit) {
    EXPECT_EQ(vk::Result::eSuccess,
              ctx_->device()
                  ->modifyMemoryRangeFUCHSIA(vk::MemoryOpFlagBitsFUCHSIA::eDecommit, range, loader_)
                  .result);

    memory_size = ctx_->device()->getMemoryCommitment(vk_device_memory.get());
    EXPECT_EQ(memory_size, 0u);

    EXPECT_EQ(vk::Result::eSuccess,
              ctx_->device()->modifyMemoryRangesFUCHSIA(vk::MemoryOpFlagBitsFUCHSIA::eCommit, 1,
                                                        &range, nullptr, loader_));

    // Memory commitment may still technically be zero depending on whether the kernel detects that
    // the pages are zero.
    memory_size = ctx_->device()->getMemoryCommitment(vk_device_memory.get());
    EXPECT_LE(memory_size, expected_memory_size_);
  } else {
    printf("Skipping Decommit part of test\n");
  }

  if (host_visible_memory_) {
    EXPECT_EQ(0u, mapped_data_[0]);
  }

  EXPECT_EQ(vk::Result::eSuccess,
            ctx_->device()
                ->modifyMemoryRangeFUCHSIA(vk::MemoryOpFlagBitsFUCHSIA::ePin, range, loader_)
                .result);

  memory_size = ctx_->device()->getMemoryCommitment(vk_device_memory.get());
  EXPECT_EQ(memory_size, expected_memory_size_);
}

TEST_P(MemoryControl, Partial) {
  if (!(control_properties_.endMemoryOperations & vk::MemoryOpFlagBitsFUCHSIA::eUnpin)) {
    printf("Can't unpin from end\n");
    GTEST_SKIP();
  }
  if (!(control_properties_.startMemoryOperations & vk::MemoryOpFlagBitsFUCHSIA::ePin)) {
    printf("Can't pin from beginning\n");
    GTEST_SKIP();
  }
  auto vk_device_memory = AllocateAndInitializeDeviceMemory(
      control_properties_.endMemoryOperations | control_properties_.startMemoryOperations);

  uint32_t end_of_committed_region =
      fbl::round_up(expected_memory_size_ / 2, control_properties_.memoryOperationGranularity);
  uint32_t end_of_committed_region2 =
      end_of_committed_region + control_properties_.memoryOperationGranularity;

  vk::MemoryRangeFUCHSIA range;
  range.memory = vk_device_memory.get();
  range.offset = end_of_committed_region;
  range.size = allocation_size_ - end_of_committed_region;

  EXPECT_EQ(vk::Result::eSuccess,
            ctx_->device()
                ->modifyMemoryRangeFUCHSIA(vk::MemoryOpFlagBitsFUCHSIA::eUnpin, range, loader_)
                .result);

  if (host_visible_memory_) {
    EXPECT_EQ(kDefaultValue, mapped_data_[end_of_committed_region]);
  }

  VkDeviceSize memory_size = ctx_->device()->getMemoryCommitment(vk_device_memory.get());
  // Memory is not 0, so the zero page decommitter can't decommit it.
  EXPECT_EQ(memory_size, expected_memory_size_);

  if (control_properties_.endMemoryOperations & vk::MemoryOpFlagBitsFUCHSIA::eDecommit) {
    // Additionally test using VK_WHOLE_SIZE instead of a specific size.
    range.size = VK_WHOLE_SIZE;

    EXPECT_EQ(vk::Result::eSuccess,
              ctx_->device()
                  ->modifyMemoryRangeFUCHSIA(vk::MemoryOpFlagBitsFUCHSIA::eDecommit, range, loader_)
                  .result);

    memory_size = ctx_->device()->getMemoryCommitment(vk_device_memory.get());
    EXPECT_EQ(memory_size, end_of_committed_region);

    // Commit a slightly larger region, but not the whole buffer.
    range.offset = 0;
    range.size = end_of_committed_region2;
    EXPECT_EQ(vk::Result::eSuccess,
              ctx_->device()->modifyMemoryRangesFUCHSIA(vk::MemoryOpFlagBitsFUCHSIA::eCommit, 1,
                                                        &range, nullptr, loader_));

    // Memory commitment may still technically be zero depending on whether the kernel detects that
    // the pages are zero.
    memory_size = ctx_->device()->getMemoryCommitment(vk_device_memory.get());
    EXPECT_LE(memory_size, end_of_committed_region2);
    if (host_visible_memory_) {
      EXPECT_EQ(0u, mapped_data_[end_of_committed_region2]);
      // Last page should have been cleared to zero by decommit.
      EXPECT_EQ(0u, mapped_data_[end_of_committed_region2 - 1]);
    }
  } else {
    printf("No decommit from end, skipping part of test\n");
  }

  if (host_visible_memory_) {
    // Value of initial part shouldn't have changed.
    EXPECT_EQ(kDefaultValue, mapped_data_[0]);
  }
  range.offset = 0;
  range.size = end_of_committed_region2;

  EXPECT_EQ(vk::Result::eSuccess,
            ctx_->device()
                ->modifyMemoryRangeFUCHSIA(vk::MemoryOpFlagBitsFUCHSIA::ePin, range, loader_)
                .result);

  memory_size = ctx_->device()->getMemoryCommitment(vk_device_memory.get());
  if (control_properties_.endMemoryOperations & vk::MemoryOpFlagBitsFUCHSIA::eDecommit) {
    EXPECT_EQ(memory_size, end_of_committed_region2);
  }
}

TEST_P(MemoryControl, DecommitWhilePinned) {
  auto vk_device_memory =
      AllocateAndInitializeDeviceMemory(control_properties_.wholeMemoryOperations);

  vk::MemoryRangeFUCHSIA range;
  range.memory = vk_device_memory.get();
  range.offset = 0u;
  range.size = allocation_size_;

  EXPECT_EQ(vk::Result::eErrorMemoryPinnedFUCHSIA,
            ctx_->device()
                ->modifyMemoryRangeFUCHSIA(vk::MemoryOpFlagBitsFUCHSIA::eDecommit, range, loader_)
                .result);

  range.size = VK_WHOLE_SIZE;
  EXPECT_EQ(vk::Result::eErrorMemoryPinnedFUCHSIA,
            ctx_->device()
                ->modifyMemoryRangeFUCHSIA(vk::MemoryOpFlagBitsFUCHSIA::eDecommit, range, loader_)
                .result);

  if (host_visible_memory_) {
    EXPECT_EQ(kDefaultValue, mapped_data_[0]);
  }
  VkDeviceSize memory_size = ctx_->device()->getMemoryCommitment(vk_device_memory.get());
  EXPECT_LE(memory_size, expected_memory_size_);
}

TEST_P(MemoryControl, MultipleOps) {
  if (!(control_properties_.wholeMemoryOperations & vk::MemoryOpFlagBitsFUCHSIA::eDecommit)) {
    printf("Skipping because can't decommit\n");
    GTEST_SKIP();
  }

  auto vk_device_memory =
      AllocateAndInitializeDeviceMemory(control_properties_.wholeMemoryOperations);

  vk::MemoryRangeFUCHSIA range;
  range.memory = vk_device_memory.get();
  range.offset = 0u;
  range.size = VK_WHOLE_SIZE;

  EXPECT_EQ(vk::Result::eSuccess,
            ctx_->device()
                ->modifyMemoryRangeFUCHSIA(
                    vk::MemoryOpFlagBitsFUCHSIA::eUnpin | vk::MemoryOpFlagBitsFUCHSIA::eDecommit,
                    range, loader_)
                .result);

  VkDeviceSize memory_size = ctx_->device()->getMemoryCommitment(vk_device_memory.get());
  EXPECT_EQ(memory_size, 0u);

  EXPECT_EQ(vk::Result::eSuccess,
            ctx_->device()
                ->modifyMemoryRangeFUCHSIA(
                    vk::MemoryOpFlagBitsFUCHSIA::eUnpin | vk::MemoryOpFlagBitsFUCHSIA::eDecommit |
                        vk::MemoryOpFlagBitsFUCHSIA::ePin | vk::MemoryOpFlagBitsFUCHSIA::eCommit,
                    range, loader_)
                .result);

  memory_size = ctx_->device()->getMemoryCommitment(vk_device_memory.get());
  EXPECT_LE(memory_size, expected_memory_size_);
}

INSTANTIATE_TEST_SUITE_P(AllTypes, MemoryControl,
                         testing::Range(0u, static_cast<uint32_t>(VK_MAX_MEMORY_TYPES)));

}  // namespace
