// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_TESTS_COMMON_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_TESTS_COMMON_H_

#include <fuchsia/images/cpp/fidl.h>
#include <lib/fdio/directory.h>

#include "src/ui/lib/escher/test/common/gtest_escher.h"
#include "src/ui/lib/escher/test/common/gtest_vulkan.h"
#include "src/ui/scenic/lib/flatland/renderer/buffer_collection.h"

namespace flatland {

struct SysmemTokens {
  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;
  fuchsia::sysmem::BufferCollectionTokenSyncPtr dup_token;
};

inline SysmemTokens CreateSysmemTokens(fuchsia::sysmem::Allocator_Sync* sysmem_allocator) {
  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;
  zx_status_t status = sysmem_allocator->AllocateSharedCollection(local_token.NewRequest());
  EXPECT_EQ(status, ZX_OK);
  fuchsia::sysmem::BufferCollectionTokenSyncPtr dup_token;
  status = local_token->Duplicate(std::numeric_limits<uint32_t>::max(), dup_token.NewRequest());
  EXPECT_EQ(status, ZX_OK);
  status = local_token->Sync();
  EXPECT_EQ(status, ZX_OK);

  return {std::move(local_token), std::move(dup_token)};
}

constexpr fuchsia::sysmem::BufferUsage kNoneUsage = {.none = fuchsia::sysmem::noneUsage};

constexpr std::pair<fuchsia::sysmem::BufferUsage, fuchsia::sysmem::BufferMemoryConstraints>
GetUsageAndMemoryConstraintsForCpuWriteOften() {
  // TODO(fxbug.dev/55193): The default memory constraints set by Sysmem only allows using
  // CPU domain for buffers with CPU usage, while Mali driver asks for only
  // RAM and Inaccessible domains for buffer allocation, which caused failure in
  // sysmem allocation. So here we add RAM domain support to clients in order
  // to get buffer allocated correctly.
  constexpr fuchsia::sysmem::BufferMemoryConstraints kCpuConstraints = {
      .ram_domain_supported = true,
      .cpu_domain_supported = true,
  };
  constexpr fuchsia::sysmem::BufferUsage kCpuWriteUsage = {.cpu =
                                                               fuchsia::sysmem::cpuUsageWriteOften};
  return std::make_pair(kCpuWriteUsage, kCpuConstraints);
}

inline void SetClientConstraintsAndWaitForAllocated(
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    fuchsia::sysmem::BufferCollectionTokenSyncPtr token, uint32_t image_count = 1,
    uint32_t width = 64, uint32_t height = 32, fuchsia::sysmem::BufferUsage usage = kNoneUsage,
    std::optional<fuchsia::sysmem::BufferMemoryConstraints> memory_constraints = std::nullopt) {
  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  zx_status_t status =
      sysmem_allocator->BindSharedCollection(std::move(token), buffer_collection.NewRequest());
  EXPECT_EQ(status, ZX_OK);
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  if (memory_constraints) {
    constraints.has_buffer_memory_constraints = true;
    constraints.buffer_memory_constraints = std::move(*memory_constraints);
  } else {
    constraints.has_buffer_memory_constraints = false;
  }
  constraints.usage = usage;
  constraints.min_buffer_count = image_count;

  constraints.image_format_constraints_count = 1;
  auto& image_constraints = constraints.image_format_constraints[0];
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0] =
      fuchsia::sysmem::ColorSpace{.type = fuchsia::sysmem::ColorSpaceType::SRGB};
  image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::BGRA32;
  image_constraints.pixel_format.has_format_modifier = true;
  image_constraints.pixel_format.format_modifier.value = fuchsia::sysmem::FORMAT_MODIFIER_LINEAR;

  image_constraints.required_min_coded_width = width;
  image_constraints.required_min_coded_height = height;
  image_constraints.required_max_coded_width = width;
  image_constraints.required_max_coded_height = height;
  image_constraints.max_coded_width = width * 4;
  image_constraints.max_coded_height = height;
  image_constraints.max_bytes_per_row = 0xffffffff;

  status = buffer_collection->SetConstraints(true, constraints);
  EXPECT_EQ(status, ZX_OK);

  // Have the client wait for allocation.
  zx_status_t allocation_status = ZX_OK;
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info = {};
  status = buffer_collection->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(allocation_status, ZX_OK);

  status = buffer_collection->Close();
  EXPECT_EQ(status, ZX_OK);
}

inline fuchsia::sysmem::BufferCollectionSyncPtr CreateClientPointerWithConstraints(
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    fuchsia::sysmem::BufferCollectionTokenSyncPtr token, uint32_t image_count = 1,
    uint32_t width = 64, uint32_t height = 32, fuchsia::sysmem::BufferUsage usage = kNoneUsage,
    std::optional<fuchsia::sysmem::BufferMemoryConstraints> memory_constraints = std::nullopt) {
  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  zx_status_t status =
      sysmem_allocator->BindSharedCollection(std::move(token), buffer_collection.NewRequest());
  EXPECT_EQ(status, ZX_OK);
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  if (memory_constraints) {
    constraints.has_buffer_memory_constraints = true;
    constraints.buffer_memory_constraints = std::move(*memory_constraints);
  } else {
    constraints.has_buffer_memory_constraints = false;
  }
  constraints.usage = usage;
  constraints.min_buffer_count = image_count;

  constraints.image_format_constraints_count = 1;
  auto& image_constraints = constraints.image_format_constraints[0];
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0] =
      fuchsia::sysmem::ColorSpace{.type = fuchsia::sysmem::ColorSpaceType::SRGB};
  image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::BGRA32;
  image_constraints.pixel_format.has_format_modifier = true;
  image_constraints.pixel_format.format_modifier.value = fuchsia::sysmem::FORMAT_MODIFIER_LINEAR;

  image_constraints.required_min_coded_width = width;
  image_constraints.required_min_coded_height = height;
  image_constraints.required_max_coded_width = width;
  image_constraints.required_max_coded_height = height;
  image_constraints.max_coded_width = width * 4;
  image_constraints.max_coded_height = height;
  image_constraints.max_bytes_per_row = 0xffffffff;

  status = buffer_collection->SetConstraints(true, constraints);
  EXPECT_EQ(status, ZX_OK);

  return buffer_collection;
}

// Common testing base class to be used across different unittests that
// require Vulkan and a SysmemAllocator.
class RendererTest : public escher::test::TestWithVkValidationLayer {
 protected:
  void SetUp() override {
    TestWithVkValidationLayer::SetUp();
    // Create the SysmemAllocator.
    zx_status_t status = fdio_service_connect(
        "/svc/fuchsia.sysmem.Allocator", sysmem_allocator_.NewRequest().TakeChannel().release());
  }

  void TearDown() override {
    sysmem_allocator_ = nullptr;
    TestWithVkValidationLayer::TearDown();
  }

  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_TESTS_COMMON_H_
