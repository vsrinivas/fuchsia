// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async-testing/test_loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>

#include <cstdint>
#include <thread>

#include "src/lib/fsl/handles/object_info.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_importer.h"
#include "src/ui/scenic/lib/allocation/id.h"
#include "src/ui/scenic/lib/flatland/buffers/util.h"
#include "src/ui/scenic/lib/flatland/renderer/null_renderer.h"
#include "src/ui/scenic/lib/flatland/renderer/tests/common.h"
#include "src/ui/scenic/lib/flatland/renderer/vk_renderer.h"
// TODO(fxbug.dev/97242): Remove dependency on screen_capture.
#include "gmock/gmock.h"
#include "src/ui/scenic/lib/screen_capture/screen_capture.h"
#include "src/ui/scenic/lib/utils/helpers.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtx/matrix_transform_2d.hpp>

namespace flatland {

using allocation::BufferCollectionUsage;
using allocation::ImageMetadata;
using fuchsia::ui::composition::Orientation;

// TODO(fxbug.dev/52632): Move common functions to testing::WithParamInterface instead of function
// calls.
using NullRendererTest = RendererTest;
using VulkanRendererTest = RendererTest;

// We need this function for several tests because irectly reading the vmo values for sysmem-backed
// images does not unmap the sRGB image values back into a linear space. So we have to do that
// conversion here before we do any value comparisons. This conversion could be done automatically
// if we were doing a Vulkan read on the vk::Image directly and not a sysmem read of the vmo,
// but we don't have direct access to the images in the Renderer.
static void sRGBtoLinear(const uint8_t* in_sRGB, uint8_t* out_linear, uint32_t num_bytes) {
  for (uint32_t i = 0; i < num_bytes; i++) {
    // Do not de-encode the alpha value.
    if ((i + 1) % 4 == 0) {
      out_linear[i] = in_sRGB[i];
      continue;
    }

    // Function to convert from sRGB to linear RGB.
    float s_val = (float(in_sRGB[i]) / float(0xFF));
    if (0.f <= s_val && s_val <= 0.04045f) {
      out_linear[i] = (s_val / 12.92f) * 255U;
    } else {
      out_linear[i] = std::powf(((s_val + 0.055f) / 1.055f), 2.4f) * 255U;
    }
  }
}

namespace {
static constexpr float kDegreesToRadians = glm::pi<float>() / 180.f;

glm::ivec4 GetPixel(uint8_t* vmo_host, uint32_t width, uint32_t x, uint32_t y) {
  uint32_t r = vmo_host[y * width * 4 + x * 4];
  uint32_t g = vmo_host[y * width * 4 + x * 4 + 1];
  uint32_t b = vmo_host[y * width * 4 + x * 4 + 2];
  uint32_t a = vmo_host[y * width * 4 + x * 4 + 3];
  return glm::ivec4(r, g, b, a);
}

// When checking the output of a render target, we want to make sure that non only
// are the renderables renderered correctly, but that the rest of the image is
// black, without any errantly colored pixels.
#define CHECK_BLACK_PIXELS(bytes, kTargetWidth, kTargetHeight, color_count) \
  {                                                                         \
    uint32_t black_pixels = 0;                                              \
    for (uint32_t y = 0; y < kTargetHeight; y++) {                          \
      for (uint32_t x = 0; x < kTargetWidth; x++) {                         \
        auto col = GetPixel(bytes, kTargetWidth, x, y);                     \
        if (col == glm::ivec4(0, 0, 0, 0)) {                                \
          black_pixels++;                                                   \
        }                                                                   \
      }                                                                     \
    }                                                                       \
    EXPECT_EQ(black_pixels, kTargetWidth* kTargetHeight - color_count);     \
  }

// Utility function to simplify tests, since setting up a buffer collection is a process that
// requires a lot of boilerplate code. The |collection_info| and |collection_ptr| need to be
// kept alive in the test body, so they are passed in as parameters.
allocation::GlobalBufferCollectionId SetupBufferCollection(
    const uint32_t& num_buffers, const uint32_t& image_width, const uint32_t& image_height,
    allocation::BufferCollectionUsage usage, Renderer* renderer,
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    fuchsia::sysmem::BufferCollectionInfo_2* collection_info,
    fuchsia::sysmem::BufferCollectionSyncPtr& collection_ptr,
    allocation::GlobalBufferCollectionId collection_id =
        allocation::GenerateUniqueBufferCollectionId()) {
  // First create the pair of sysmem tokens, one for the client, one for the renderer.
  auto tokens = flatland::SysmemTokens::Create(sysmem_allocator);

  auto result = renderer->ImportBufferCollection(collection_id, sysmem_allocator,
                                                 std::move(tokens.dup_token), usage, std::nullopt);
  EXPECT_TRUE(result);

  // Create a client-side handle to the buffer collection and set the client constraints.
  auto [buffer_usage, memory_constraints] = GetUsageAndMemoryConstraintsForCpuWriteOften();
  collection_ptr = CreateBufferCollectionSyncPtrAndSetConstraints(
      sysmem_allocator, std::move(tokens.local_token),
      /*image_count*/ num_buffers,
      /*width*/ image_width,
      /*height*/ image_height, buffer_usage, fuchsia::sysmem::PixelFormatType::BGRA32,
      std::make_optional(memory_constraints));

  // Have the client wait for buffers allocated so it can populate its information
  // struct with the vmo data.
  {
    zx_status_t allocation_status = ZX_OK;
    auto status = collection_ptr->WaitForBuffersAllocated(&allocation_status, collection_info);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(allocation_status, ZX_OK);
  }

  return collection_id;
}
}  // anonymous namespace

// Make sure a valid token can be used to import a buffer collection.
void ImportCollectionTest(Renderer* renderer, fuchsia::sysmem::Allocator_Sync* sysmem_allocator) {
  auto tokens = SysmemTokens::Create(sysmem_allocator);

  // First id should be valid.
  auto bcid = allocation::GenerateUniqueBufferCollectionId();
  auto result =
      renderer->ImportBufferCollection(bcid, sysmem_allocator, std::move(tokens.local_token),
                                       BufferCollectionUsage::kRenderTarget, std::nullopt);
  EXPECT_TRUE(result);
}

// Multiple clients may need to reference the same buffer collection in the renderer
// (for example if they both need access to a global camera feed). In this case, both
// clients will be passing their own duped tokens to the same collection to the renderer,
// and will each get back a different ID. The collection itself (which is just a pointer)
// will be in the renderer's map twice. So if all tokens are set, both server-side
// importer collections should be allocated (since they are just pointers that refer
// to the same collection).
void SameTokenTwiceTest(Renderer* renderer, fuchsia::sysmem::Allocator_Sync* sysmem_allocator) {
  auto tokens = flatland::SysmemTokens::Create(sysmem_allocator);

  // Create a client token to represent a single client.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr client_token;
  auto status = tokens.local_token->Duplicate(std::numeric_limits<uint32_t>::max(),
                                              client_token.NewRequest());
  EXPECT_EQ(status, ZX_OK);

  // First id should be valid.
  auto bcid = allocation::GenerateUniqueBufferCollectionId();
  auto result =
      renderer->ImportBufferCollection(bcid, sysmem_allocator, std::move(tokens.local_token),
                                       BufferCollectionUsage::kRenderTarget, std::nullopt);
  EXPECT_TRUE(result);

  // Second id should be valid.
  auto bcid2 = allocation::GenerateUniqueBufferCollectionId();
  result = renderer->ImportBufferCollection(bcid2, sysmem_allocator, std::move(tokens.dup_token),
                                            BufferCollectionUsage::kRenderTarget, std::nullopt);
  EXPECT_TRUE(result);

  // Set the client constraints.
  std::vector<uint64_t> additional_format_modifiers;
  if (escher::VulkanIsSupported() && escher::test::GlobalEscherUsesVirtualGpu()) {
    additional_format_modifiers.push_back(fuchsia::sysmem::FORMAT_MODIFIER_GOOGLE_GOLDFISH_OPTIMAL);
  }
  SetClientConstraintsAndWaitForAllocated(sysmem_allocator, std::move(client_token),
                                          /* image_count */ 1, /* width */ 64, /* height */ 32,
                                          kNoneUsage, additional_format_modifiers);

  // Now check that both server ids are allocated.
  bool res_1 = renderer->ImportBufferImage({.collection_id = bcid,
                                            .identifier = allocation::GenerateUniqueImageId(),
                                            .vmo_index = 0,
                                            .width = 1,
                                            .height = 1},
                                           BufferCollectionUsage::kRenderTarget);
  bool res_2 = renderer->ImportBufferImage({.collection_id = bcid2,
                                            .identifier = allocation::GenerateUniqueImageId(),
                                            .vmo_index = 0,
                                            .width = 1,
                                            .height = 1},
                                           BufferCollectionUsage::kRenderTarget);
  EXPECT_TRUE(res_1);
  EXPECT_TRUE(res_2);
}

// Make sure a bad token returns Renderer::allocation::kInvalidId. A "bad token" here can
// either be a null token, or a token that's a valid channel but just not a
// valid buffer collection token.
void BadTokenTest(Renderer* renderer, fuchsia::sysmem::Allocator_Sync* sysmem_allocator) {
  // Null token should fail.
  auto bcid = allocation::GenerateUniqueBufferCollectionId();
  auto result = renderer->ImportBufferCollection(
      bcid, sysmem_allocator, nullptr, BufferCollectionUsage::kRenderTarget, std::nullopt);
  EXPECT_FALSE(result);

  // A valid channel that isn't a buffer collection should also fail.
  zx::channel local_endpoint;
  zx::channel remote_endpoint;
  zx::channel::create(0, &local_endpoint, &remote_endpoint);
  flatland::BufferCollectionHandle handle{std::move(remote_endpoint)};
  ASSERT_TRUE(handle.is_valid());

  bcid = allocation::GenerateUniqueBufferCollectionId();
  result = renderer->ImportBufferCollection(bcid, sysmem_allocator, std::move(handle),
                                            BufferCollectionUsage::kRenderTarget, std::nullopt);
  EXPECT_FALSE(result);
}

void BadImageInputTest(Renderer* renderer, fuchsia::sysmem::Allocator_Sync* sysmem_allocator) {
  const uint32_t kNumImages = 1;
  auto tokens = SysmemTokens::Create(sysmem_allocator);

  auto bcid = allocation::GenerateUniqueBufferCollectionId();
  auto result =
      renderer->ImportBufferCollection(bcid, sysmem_allocator, std::move(tokens.dup_token),
                                       BufferCollectionUsage::kRenderTarget, std::nullopt);
  EXPECT_TRUE(result);

  std::vector<uint64_t> additional_format_modifiers;
  if (escher::VulkanIsSupported() && escher::test::GlobalEscherUsesVirtualGpu()) {
    additional_format_modifiers.push_back(fuchsia::sysmem::FORMAT_MODIFIER_GOOGLE_GOLDFISH_OPTIMAL);
  }
  SetClientConstraintsAndWaitForAllocated(sysmem_allocator, std::move(tokens.local_token),
                                          /* image_count */ kNumImages, /* width */ 64,
                                          /* height */ 32, kNoneUsage, additional_format_modifiers);

  // Using an invalid buffer collection id.
  auto image_id = allocation::GenerateUniqueImageId();
  EXPECT_FALSE(renderer->ImportBufferImage({.collection_id = allocation::kInvalidId,
                                            .identifier = image_id,
                                            .vmo_index = kNumImages,
                                            .width = 1,
                                            .height = 1},
                                           BufferCollectionUsage::kRenderTarget));

  // Using an invalid image identifier.
  EXPECT_FALSE(renderer->ImportBufferImage({.collection_id = bcid,
                                            .identifier = allocation::kInvalidImageId,
                                            .vmo_index = kNumImages,
                                            .width = 1,
                                            .height = 1},
                                           BufferCollectionUsage::kRenderTarget));

  // VMO index is out of bounds.
  EXPECT_FALSE(renderer->ImportBufferImage({.collection_id = bcid,
                                            .identifier = image_id,
                                            .vmo_index = kNumImages,
                                            .width = 1,
                                            .height = 1},
                                           BufferCollectionUsage::kRenderTarget));
}

// Test the ImportBufferImage() function. First call ImportBufferImage() without setting the client
// constraints, which should return false, and then set the client constraints which
// should cause it to return true.
void ImportImageTest(Renderer* renderer, fuchsia::sysmem::Allocator_Sync* sysmem_allocator) {
  auto tokens = SysmemTokens::Create(sysmem_allocator);

  auto bcid = allocation::GenerateUniqueBufferCollectionId();
  auto result =
      renderer->ImportBufferCollection(bcid, sysmem_allocator, std::move(tokens.dup_token),
                                       BufferCollectionUsage::kRenderTarget, std::nullopt);
  EXPECT_TRUE(result);

  // The buffer collection should not be valid here.
  auto image_id = allocation::GenerateUniqueImageId();
  EXPECT_FALSE(renderer->ImportBufferImage(
      {.collection_id = bcid, .identifier = image_id, .vmo_index = 0, .width = 1, .height = 1},
      BufferCollectionUsage::kRenderTarget));

  std::vector<uint64_t> additional_format_modifiers;
  if (escher::VulkanIsSupported() && escher::test::GlobalEscherUsesVirtualGpu()) {
    additional_format_modifiers.push_back(fuchsia::sysmem::FORMAT_MODIFIER_GOOGLE_GOLDFISH_OPTIMAL);
  }
  SetClientConstraintsAndWaitForAllocated(sysmem_allocator, std::move(tokens.local_token),
                                          /* image_count */ 1, /* width */ 64, /* height */ 32,
                                          kNoneUsage, additional_format_modifiers);

  // The buffer collection *should* be valid here.
  auto res = renderer->ImportBufferImage(
      {.collection_id = bcid, .identifier = image_id, .vmo_index = 0, .width = 1, .height = 1},
      BufferCollectionUsage::kRenderTarget);
  EXPECT_TRUE(res);
}

// Simple release test that calls ReleaseBufferCollection() directly without
// any zx::events just to make sure that the method's functionality itself is
// working as intented.
void DeregistrationTest(Renderer* renderer, fuchsia::sysmem::Allocator_Sync* sysmem_allocator) {
  auto tokens = SysmemTokens::Create(sysmem_allocator);

  auto bcid = allocation::GenerateUniqueBufferCollectionId();
  auto result =
      renderer->ImportBufferCollection(bcid, sysmem_allocator, std::move(tokens.dup_token),
                                       BufferCollectionUsage::kRenderTarget, std::nullopt);
  EXPECT_TRUE(result);

  // The buffer collection should not be valid here.
  auto image_id = allocation::GenerateUniqueImageId();
  EXPECT_FALSE(renderer->ImportBufferImage(
      {.collection_id = bcid, .identifier = image_id, .vmo_index = 0, .width = 1, .height = 1},
      BufferCollectionUsage::kRenderTarget));

  std::vector<uint64_t> additional_format_modifiers;
  if (escher::VulkanIsSupported() && escher::test::GlobalEscherUsesVirtualGpu()) {
    additional_format_modifiers.push_back(fuchsia::sysmem::FORMAT_MODIFIER_GOOGLE_GOLDFISH_OPTIMAL);
  }
  SetClientConstraintsAndWaitForAllocated(sysmem_allocator, std::move(tokens.local_token),
                                          /* image_count */ 1, /* width */ 64, /* height */ 32,
                                          kNoneUsage, additional_format_modifiers);

  // The buffer collection *should* be valid here.
  auto import_result = renderer->ImportBufferImage(
      {.collection_id = bcid, .identifier = image_id, .vmo_index = 0, .width = 1, .height = 1},
      BufferCollectionUsage::kRenderTarget);
  EXPECT_TRUE(import_result);

  // Now release the collection.
  renderer->ReleaseBufferCollection(bcid, BufferCollectionUsage::kRenderTarget);

  // After deregistration, calling ImportBufferImage() should return false.
  import_result = renderer->ImportBufferImage(
      {.collection_id = bcid, .identifier = image_id, .vmo_index = 0, .width = 1, .height = 1},
      BufferCollectionUsage::kRenderTarget);
  EXPECT_FALSE(import_result);
}

// Test that calls ReleaseBufferCollection() before ReleaseBufferImage() and makes sure that
// imported Image can still be rendered.
void RenderImageAfterBufferCollectionReleasedTest(Renderer* renderer,
                                                  fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
                                                  bool use_vulkan) {
  auto texture_tokens = SysmemTokens::Create(sysmem_allocator);
  auto target_tokens = SysmemTokens::Create(sysmem_allocator);

  auto texture_collection_id = allocation::GenerateUniqueBufferCollectionId();
  auto target_collection_id = allocation::GenerateUniqueBufferCollectionId();
  auto result = renderer->ImportBufferCollection(texture_collection_id, sysmem_allocator,
                                                 std::move(texture_tokens.dup_token),
                                                 BufferCollectionUsage::kClientImage, std::nullopt);
  EXPECT_TRUE(result);

  result = renderer->ImportBufferCollection(target_collection_id, sysmem_allocator,
                                            std::move(target_tokens.dup_token),
                                            BufferCollectionUsage::kRenderTarget, std::nullopt);
  EXPECT_TRUE(result);

  std::vector<uint64_t> additional_format_modifiers;
  if (escher::VulkanIsSupported() && escher::test::GlobalEscherUsesVirtualGpu()) {
    additional_format_modifiers.push_back(fuchsia::sysmem::FORMAT_MODIFIER_GOOGLE_GOLDFISH_OPTIMAL);
  }
  const uint32_t kWidth = 64, kHeight = 32;
  SetClientConstraintsAndWaitForAllocated(sysmem_allocator, std::move(texture_tokens.local_token),
                                          /* image_count */ 1, /* width */ kWidth,
                                          /* height */ kHeight, kNoneUsage,
                                          additional_format_modifiers);

  SetClientConstraintsAndWaitForAllocated(sysmem_allocator, std::move(target_tokens.local_token),
                                          /* image_count */ 1, /* width */ kWidth,
                                          /* height */ kHeight, kNoneUsage,
                                          additional_format_modifiers);

  // Import render target.
  ImageMetadata render_target = {.collection_id = target_collection_id,
                                 .identifier = allocation::GenerateUniqueImageId(),
                                 .vmo_index = 0,
                                 .width = kWidth,
                                 .height = kHeight};
  auto import_result =
      renderer->ImportBufferImage(render_target, BufferCollectionUsage::kRenderTarget);
  EXPECT_TRUE(import_result);

  // Import image.
  ImageMetadata image = {.collection_id = texture_collection_id,
                         .identifier = allocation::GenerateUniqueImageId(),
                         .vmo_index = 0,
                         .width = kWidth,
                         .height = kHeight};
  import_result = renderer->ImportBufferImage(image, BufferCollectionUsage::kClientImage);
  EXPECT_TRUE(import_result);

  // Now release the collection.
  renderer->ReleaseBufferCollection(texture_collection_id, BufferCollectionUsage::kClientImage);
  renderer->ReleaseBufferCollection(target_collection_id, BufferCollectionUsage::kRenderTarget);

  // We should still be able to render this image.
  renderer->Render(render_target, {ImageRect(glm::vec2(0, 0), glm::vec2(kWidth, kHeight))},
                   {image});
  if (use_vulkan) {
    auto vk_renderer = static_cast<VkRenderer*>(renderer);
    vk_renderer->WaitIdle();
  }
}

void RenderAfterImageReleasedTest(Renderer* renderer,
                                  fuchsia::sysmem::Allocator_Sync* sysmem_allocator) {
  auto texture_tokens = SysmemTokens::Create(sysmem_allocator);
  auto target_tokens = SysmemTokens::Create(sysmem_allocator);

  auto texture_collection_id = allocation::GenerateUniqueBufferCollectionId();
  auto target_collection_id = allocation::GenerateUniqueBufferCollectionId();
  auto result = renderer->ImportBufferCollection(texture_collection_id, sysmem_allocator,
                                                 std::move(texture_tokens.dup_token),
                                                 BufferCollectionUsage::kClientImage, std::nullopt);
  EXPECT_TRUE(result);

  result = renderer->ImportBufferCollection(target_collection_id, sysmem_allocator,
                                            std::move(target_tokens.dup_token),
                                            BufferCollectionUsage::kRenderTarget, std::nullopt);
  EXPECT_TRUE(result);

  std::vector<uint64_t> additional_format_modifiers;
  if (escher::VulkanIsSupported() && escher::test::GlobalEscherUsesVirtualGpu()) {
    additional_format_modifiers.push_back(fuchsia::sysmem::FORMAT_MODIFIER_GOOGLE_GOLDFISH_OPTIMAL);
  }
  const uint32_t kWidth = 64, kHeight = 32;
  SetClientConstraintsAndWaitForAllocated(sysmem_allocator, std::move(texture_tokens.local_token),
                                          /* image_count */ 1, /* width */ kWidth,
                                          /* height */ kHeight, kNoneUsage,
                                          additional_format_modifiers);

  SetClientConstraintsAndWaitForAllocated(sysmem_allocator, std::move(target_tokens.local_token),
                                          /* image_count */ 1, /* width */ kWidth,
                                          /* height */ kHeight, kNoneUsage,
                                          additional_format_modifiers);

  // Import render target.
  ImageMetadata render_target = {.collection_id = target_collection_id,
                                 .identifier = allocation::GenerateUniqueImageId(),
                                 .vmo_index = 0,
                                 .width = kWidth,
                                 .height = kHeight};
  auto import_result =
      renderer->ImportBufferImage(render_target, BufferCollectionUsage::kRenderTarget);
  EXPECT_TRUE(import_result);

  // Import image.
  ImageMetadata image = {.collection_id = texture_collection_id,
                         .identifier = allocation::GenerateUniqueImageId(),
                         .vmo_index = 0,
                         .width = kWidth,
                         .height = kHeight};
  import_result = renderer->ImportBufferImage(image, BufferCollectionUsage::kClientImage);
  EXPECT_TRUE(import_result);

  // Now release the collection.
  renderer->ReleaseBufferImage(image.identifier);

  // Send an empty render.
  renderer->Render(render_target, {}, {});
}

// Test to make sure we can call the functions import kRenderTarget and kClientImage collections
// and ImportBufferImage() simultaneously from multiple threads and have it work.
void MultithreadingTest(Renderer* renderer) {
  const uint32_t kNumThreads = 50;

  std::set<allocation::GlobalBufferCollectionId> bcid_set;
  std::mutex lock;

  auto register_and_import_function = [&renderer, &bcid_set, &lock]() {
    // Make a test loop.
    async::TestLoop loop;

    // Make an extra sysmem allocator for tokens.
    fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator = utils::CreateSysmemAllocatorSyncPtr();

    auto tokens = SysmemTokens::Create(sysmem_allocator.get());
    auto bcid = allocation::GenerateUniqueBufferCollectionId();
    auto image_id = allocation::GenerateUniqueImageId();
    bool result = renderer->ImportBufferCollection(
        bcid, sysmem_allocator.get(), std::move(tokens.local_token),
        BufferCollectionUsage::kRenderTarget, std::nullopt);
    EXPECT_TRUE(result);

    std::vector<uint64_t> additional_format_modifiers;
    if (escher::VulkanIsSupported() && escher::test::GlobalEscherUsesVirtualGpu()) {
      additional_format_modifiers.push_back(
          fuchsia::sysmem::FORMAT_MODIFIER_GOOGLE_GOLDFISH_OPTIMAL);
    }
    SetClientConstraintsAndWaitForAllocated(sysmem_allocator.get(), std::move(tokens.local_token),
                                            /* image_count */ 1, /* width */ 64, /* height */ 32,
                                            kNoneUsage, additional_format_modifiers);

    // Add the bcid to the global vector in a thread-safe manner.
    {
      std::unique_lock<std::mutex> unique_lock(lock);
      bcid_set.insert(bcid);
    }

    // The buffer collection *should* be valid here.
    auto import_result = renderer->ImportBufferImage(
        {.collection_id = bcid, .identifier = image_id, .vmo_index = 0, .width = 1, .height = 1},
        BufferCollectionUsage::kRenderTarget);
    EXPECT_TRUE(import_result);
    loop.RunUntilIdle();
  };

  // Run a bunch of threads, alternating between threads that import texture collections
  // and threads that import render target collections.
  std::vector<std::thread> threads;
  for (uint32_t i = 0; i < kNumThreads; i++) {
    threads.push_back(std::thread(register_and_import_function));
  }

  for (auto&& thread : threads) {
    thread.join();
  }

  // Import the ids here one more time to make sure the renderer's internal
  // state hasn't been corrupted. We use the values gathered in the bcid_vec
  // to test with.
  EXPECT_EQ(bcid_set.size(), kNumThreads);
  for (const auto& bcid : bcid_set) {
    // The buffer collection *should* be valid here.
    auto result = renderer->ImportBufferImage({.collection_id = bcid,
                                               .identifier = allocation::GenerateUniqueImageId(),
                                               .vmo_index = 0,
                                               .width = 1,
                                               .height = 1},
                                              BufferCollectionUsage::kRenderTarget);
    EXPECT_TRUE(result);
  }
}

// This test checks to make sure that the Render() function properly signals
// a zx::event which can be used by an async::Wait object to asynchronously
// call a custom function.
void AsyncEventSignalTest(Renderer* renderer, fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
                          bool use_vulkan) {
  // Setup the render target collection.
  const uint32_t kWidth = 64, kHeight = 32;
  fuchsia::sysmem::BufferCollectionInfo_2 client_target_info;
  fuchsia::sysmem::BufferCollectionSyncPtr target_ptr;
  auto target_id =
      SetupBufferCollection(1, kWidth, kHeight, BufferCollectionUsage::kRenderTarget, renderer,
                            sysmem_allocator, &client_target_info, target_ptr);

  // Now that the renderer and client have set their contraints, we can import the render target.
  // Create the render_target image metadata.
  ImageMetadata render_target = {.collection_id = target_id,
                                 .identifier = allocation::GenerateUniqueImageId(),
                                 .vmo_index = 0,
                                 .width = kWidth,
                                 .height = kHeight};
  auto target_import =
      renderer->ImportBufferImage(render_target, BufferCollectionUsage::kRenderTarget);
  EXPECT_TRUE(target_import);

  // Create the release fence that will be passed along to the Render()
  // function and be used to signal when we should release the collection.
  zx::event release_fence;
  auto status = zx::event::create(0, &release_fence);
  EXPECT_EQ(status, ZX_OK);

  // Set up the async::Wait object to wait until the release_fence signals
  // ZX_EVENT_SIGNALED. We make use of a test loop to access an async dispatcher.
  async::TestLoop loop;
  bool signaled = false;
  auto dispatcher = loop.dispatcher();
  auto wait = std::make_unique<async::Wait>(release_fence.get(), ZX_EVENT_SIGNALED);
  wait->set_handler([&signaled](async_dispatcher_t*, async::Wait*, zx_status_t /*status*/,
                                const zx_packet_signal_t* /*signal*/) mutable { signaled = true; });
  wait->Begin(dispatcher);

  // The call to Render() will signal the release fence, triggering the wait object to
  // call its handler function.
  std::vector<zx::event> fences;
  fences.push_back(std::move(release_fence));
  renderer->Render(render_target, {}, {}, fences);

  if (use_vulkan) {
    auto vk_renderer = static_cast<VkRenderer*>(renderer);
    vk_renderer->WaitIdle();
  }

  // Close the test loop and test that our handler was called.
  loop.RunUntilIdle();
  EXPECT_TRUE(signaled);
}

TEST_F(NullRendererTest, ImportCollectionTest) {
  NullRenderer renderer;
  ImportCollectionTest(&renderer, sysmem_allocator_.get());
}

TEST_F(NullRendererTest, SameTokenTwiceTest) {
  NullRenderer renderer;
  SameTokenTwiceTest(&renderer, sysmem_allocator_.get());
}

TEST_F(NullRendererTest, BadTokenTest) {
  NullRenderer renderer;
  BadTokenTest(&renderer, sysmem_allocator_.get());
}

TEST_F(NullRendererTest, BadImageInputTest) {
  NullRenderer renderer;
  BadImageInputTest(&renderer, sysmem_allocator_.get());
}

TEST_F(NullRendererTest, ImportImageTest) {
  NullRenderer renderer;
  ImportImageTest(&renderer, sysmem_allocator_.get());
}

TEST_F(NullRendererTest, DeregistrationTest) {
  NullRenderer renderer;
  DeregistrationTest(&renderer, sysmem_allocator_.get());
}

TEST_F(NullRendererTest, RenderImageAfterBufferCollectionReleasedTest) {
  NullRenderer renderer;
  RenderImageAfterBufferCollectionReleasedTest(&renderer, sysmem_allocator_.get(),
                                               /*use_vulkan*/ false);
}

TEST_F(NullRendererTest, RenderAfterImageReleasedTest) {
  NullRenderer renderer;
  RenderAfterImageReleasedTest(&renderer, sysmem_allocator_.get());
}

TEST_F(NullRendererTest, DISABLED_MultithreadingTest) {
  NullRenderer renderer;
  MultithreadingTest(&renderer);
}

TEST_F(NullRendererTest, AsyncEventSignalTest) {
  NullRenderer renderer;
  AsyncEventSignalTest(&renderer, sysmem_allocator_.get(), /*use_vulkan*/ false);
}

VK_TEST_F(VulkanRendererTest, ImportCollectionTest) {
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher = std::make_unique<escher::Escher>(
      env->GetVulkanDevice(), env->GetFilesystem(), /*gpu_allocator*/ nullptr);
  VkRenderer renderer(unique_escher->GetWeakPtr());
  ImportCollectionTest(&renderer, sysmem_allocator_.get());
}

VK_TEST_F(VulkanRendererTest, SameTokenTwiceTest) {
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher = std::make_unique<escher::Escher>(
      env->GetVulkanDevice(), env->GetFilesystem(), /*gpu_allocator*/ nullptr);
  VkRenderer renderer(unique_escher->GetWeakPtr());
  SameTokenTwiceTest(&renderer, sysmem_allocator_.get());
}

VK_TEST_F(VulkanRendererTest, BadTokenTest) {
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher = std::make_unique<escher::Escher>(
      env->GetVulkanDevice(), env->GetFilesystem(), /*gpu_allocator*/ nullptr);
  VkRenderer renderer(unique_escher->GetWeakPtr());
  BadTokenTest(&renderer, sysmem_allocator_.get());
}

VK_TEST_F(VulkanRendererTest, BadImageInputTest) {
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher = std::make_unique<escher::Escher>(
      env->GetVulkanDevice(), env->GetFilesystem(), /*gpu_allocator*/ nullptr);
  VkRenderer renderer(unique_escher->GetWeakPtr());
  BadImageInputTest(&renderer, sysmem_allocator_.get());
}

VK_TEST_F(VulkanRendererTest, ImportImageTest) {
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher = std::make_unique<escher::Escher>(
      env->GetVulkanDevice(), env->GetFilesystem(), /*gpu_allocator*/ nullptr);
  VkRenderer renderer(unique_escher->GetWeakPtr());
  ImportImageTest(&renderer, sysmem_allocator_.get());
}

VK_TEST_F(VulkanRendererTest, DeregistrationTest) {
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher = std::make_unique<escher::Escher>(
      env->GetVulkanDevice(), env->GetFilesystem(), /*gpu_allocator*/ nullptr);
  VkRenderer renderer(unique_escher->GetWeakPtr());
  DeregistrationTest(&renderer, sysmem_allocator_.get());
}

// TODO(fx.bug/dev:66216) This test is flaking on FEMU.
VK_TEST_F(VulkanRendererTest, DISABLED_RenderImageAfterBufferCollectionReleasedTest) {
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher = std::make_unique<escher::Escher>(
      env->GetVulkanDevice(), env->GetFilesystem(), /*gpu_allocator*/ nullptr);
  VkRenderer renderer(unique_escher->GetWeakPtr());
  RenderImageAfterBufferCollectionReleasedTest(&renderer, sysmem_allocator_.get(),
                                               /*use_vulkan*/ true);
}

VK_TEST_F(VulkanRendererTest, RenderAfterImageReleasedTest) {
  // TODO(fxbug.dev/96541): Re-enable on FEMU once it doesn't flake.
  SKIP_TEST_IF_ESCHER_USES_DEVICE(VirtualGpu);
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher = std::make_unique<escher::Escher>(
      env->GetVulkanDevice(), env->GetFilesystem(), /*gpu_allocator*/ nullptr);
  VkRenderer renderer(unique_escher->GetWeakPtr());
  RenderAfterImageReleasedTest(&renderer, sysmem_allocator_.get());
}

VK_TEST_F(VulkanRendererTest, DISABLED_MultithreadingTest) {
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher = std::make_unique<escher::Escher>(
      env->GetVulkanDevice(), env->GetFilesystem(), /*gpu_allocator*/ nullptr);
  VkRenderer renderer(unique_escher->GetWeakPtr());
  MultithreadingTest(&renderer);
}

VK_TEST_F(VulkanRendererTest, AsyncEventSignalTest) {
  SKIP_TEST_IF_ESCHER_USES_DEVICE(VirtualGpu);
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher = std::make_unique<escher::Escher>(
      env->GetVulkanDevice(), env->GetFilesystem(), /*gpu_allocator*/ nullptr);
  VkRenderer renderer(unique_escher->GetWeakPtr());
  AsyncEventSignalTest(&renderer, sysmem_allocator_.get(), /*use_vulkan*/ true);
}

// This test actually renders a rectangle using the VKRenderer. We create a single rectangle,
// with a half-red, half-green texture, and translate it. The render target is 16x8
// and the rectangle is 4x2. So in the end the result should look like this:
//
// ----------------
// ----------------
// ----------------
// ------RRGG------
// ------RRGG------
// ----------------
// ----------------
// ----------------
//
// It then renders the renderable a second time, this time with modified UVs so that only
// the green portion of the texture covers the rect, resulting in a fully green view despite
// the texture also having red pixels:
//
// ----------------
// ----------------
// ----------------
// ------GGGG------
// ------GGGG------
// ----------------
// ----------------
// ----------------
//
VK_TEST_F(VulkanRendererTest, RenderTest) {
  SKIP_TEST_IF_ESCHER_USES_DEVICE(VirtualGpu);
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher = std::make_unique<escher::Escher>(
      env->GetVulkanDevice(), env->GetFilesystem(), /*gpu_allocator*/ nullptr);
  VkRenderer renderer(unique_escher->GetWeakPtr());

  // Setup renderable texture collection.
  fuchsia::sysmem::BufferCollectionInfo_2 client_collection_info;
  fuchsia::sysmem::BufferCollectionSyncPtr collection_ptr;
  auto collection_id =
      SetupBufferCollection(1, 60, 40, BufferCollectionUsage::kClientImage, &renderer,
                            sysmem_allocator_.get(), &client_collection_info, collection_ptr);

  // Setup the render target collection.
  fuchsia::sysmem::BufferCollectionInfo_2 client_target_info;
  fuchsia::sysmem::BufferCollectionSyncPtr target_ptr;
  auto target_id = SetupBufferCollection(1, 60, 40, BufferCollectionUsage::kRenderTarget, &renderer,
                                         sysmem_allocator_.get(), &client_target_info, target_ptr);

  const uint32_t kTargetWidth = 16;
  const uint32_t kTargetHeight = 8;

  // Create the render_target image metadata.
  ImageMetadata render_target = {.collection_id = target_id,
                                 .identifier = allocation::GenerateUniqueImageId(),
                                 .vmo_index = 0,
                                 .width = kTargetWidth,
                                 .height = kTargetHeight};

  // The texture width and height, also used for unnormalized texture coordinates.
  const float w = 2;
  const float h = 1;

  // Create the image meta data for the renderable.
  ImageMetadata renderable_texture = {.collection_id = collection_id,
                                      .identifier = allocation::GenerateUniqueImageId(),
                                      .vmo_index = 0,
                                      .width = static_cast<uint32_t>(w),
                                      .height = static_cast<uint32_t>(h)};

  auto import_res = renderer.ImportBufferImage(render_target, BufferCollectionUsage::kRenderTarget);
  EXPECT_TRUE(import_res);

  import_res = renderer.ImportBufferImage(renderable_texture, BufferCollectionUsage::kClientImage);
  EXPECT_TRUE(import_res);

  // Create a renderable where the upper-left hand corner should be at position (6,3)
  // with a width/height of (4,2).
  const uint32_t kRenderableWidth = 4;
  const uint32_t kRenderableHeight = 2;
  ImageRect renderable(glm::vec2(6, 3), glm::vec2(kRenderableWidth, kRenderableHeight),
                       {glm::vec2(0, 0), glm::vec2(w, 0), glm::vec2(w, h), glm::vec2(0, h)},
                       Orientation::CCW_0_DEGREES);

  // Have the client write pixel values to the renderable's texture.
  MapHostPointer(client_collection_info, renderable_texture.vmo_index,
                 [&](uint8_t* vmo_host, uint32_t num_bytes) mutable {
                   // The texture only has 2 pixels, so it needs 8 write values for 4 channels. We
                   // set the first pixel to red and the second pixel to green.
                   const uint8_t kNumWrites = 8;
                   const uint8_t kWriteValues[] = {/*red*/ 255U, 0,    0, 255U,
                                                   /*green*/ 0,  255U, 0, 255U};
                   memcpy(vmo_host, kWriteValues, sizeof(kWriteValues));

                   // Flush the cache after writing to host VMO.
                   EXPECT_EQ(ZX_OK,
                             zx_cache_flush(vmo_host, kNumWrites,
                                            ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));
                 });

  // Render the renderable to the render target.
  renderer.Render(render_target, {renderable}, {renderable_texture});
  renderer.WaitIdle();

  // Get a raw pointer from the client collection's vmo that represents the render target
  // and read its values. This should show that the renderable was rendered to the center
  // of the render target, with its associated texture.
  MapHostPointer(client_target_info, render_target.vmo_index,
                 [&](uint8_t* vmo_host, uint32_t num_bytes) mutable {
                   // Flush the cache before reading back target image.
                   EXPECT_EQ(ZX_OK,
                             zx_cache_flush(vmo_host, kTargetWidth * kTargetHeight * 4,
                                            ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));

                   // Make sure the pixels are in the right order.
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 6, 3), glm::ivec4(255, 0, 0, 255));
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 7, 3), glm::ivec4(255, 0, 0, 255));
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 8, 3), glm::ivec4(0, 255, 0, 255));
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 9, 3), glm::ivec4(0, 255, 0, 255));
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 6, 4), glm::ivec4(255, 0, 0, 255));
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 7, 4), glm::ivec4(255, 0, 0, 255));
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 8, 4), glm::ivec4(0, 255, 0, 255));
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 9, 4), glm::ivec4(0, 255, 0, 255));

                   // Make sure the remaining pixels are black.
                   CHECK_BLACK_PIXELS(vmo_host, kTargetWidth, kTargetHeight,
                                      kRenderableWidth * kRenderableHeight);
                 });

  // Now let's update the uvs of the renderable so only the green portion of the image maps onto
  // the rect.
  auto renderable2 =
      ImageRect(glm::vec2(6, 3), glm::vec2(kRenderableWidth, kRenderableHeight),
                {glm::vec2(w / 2, 0), glm::vec2(w, 0), glm::vec2(w, h), glm::vec2(w / 2, h)},
                Orientation::CCW_0_DEGREES);

  // Render the renderable to the render target.
  renderer.Render(render_target, {renderable2}, {renderable_texture});
  renderer.WaitIdle();

  // Get a raw pointer from the client collection's vmo that represents the render target
  // and read its values. This should show that the renderable was rendered to the center
  // of the render target, with its associated texture.
  MapHostPointer(
      client_target_info, render_target.vmo_index,
      [&](uint8_t* vmo_host, uint32_t num_bytes) mutable {
        // Flush the cache before reading back target image.
        EXPECT_EQ(ZX_OK, zx_cache_flush(vmo_host, kTargetWidth * kTargetHeight * 4,
                                        ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));

        // All of the renderable's pixels should be green.
        for (uint32_t i = 6; i < 6 + kRenderableWidth; i++) {
          for (uint32_t j = 3; j < 3 + kRenderableHeight; j++) {
            EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, i, j), glm::ivec4(0, 255, 0, 255));
          }
        }

        // Make sure the remaining pixels are black.
        CHECK_BLACK_PIXELS(vmo_host, kTargetWidth, kTargetHeight,
                           kRenderableWidth * kRenderableHeight);
      });
}

// This test actually renders a rectangle using the VKRenderer. We create a single rectangle,
// with a half-red, half-green texture, and translate it. The render target is 16x8
// and the rectangle is 4x2. So in the end the result should look like this:
//
// ----------------
// ----------------
// ----------------
// ------RRGG------
// ------RRGG------
// ----------------
// ----------------
// ----------------
//
// It then renders the renderable more times, rotating it 90* clockwise each time. This results in
// the following images:
//
// ----------------
// ----------------
// -------RR-------
// -------RR-------
// -------GG-------
// -------GG-------
// ----------------
// ----------------
//
// ----------------
// ----------------
// ----------------
// ------GGRR------
// ------GGRR------
// ----------------
// ----------------
// ----------------
//
// ----------------
// ----------------
// -------GG-------
// -------GG-------
// -------RR-------
// -------RR-------
// ----------------
// ----------------
//
VK_TEST_F(VulkanRendererTest, RotationRenderTest) {
  SKIP_TEST_IF_ESCHER_USES_DEVICE(VirtualGpu);
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher = std::make_unique<escher::Escher>(
      env->GetVulkanDevice(), env->GetFilesystem(), /*gpu_allocator*/ nullptr);
  VkRenderer renderer(unique_escher->GetWeakPtr());

  fuchsia::sysmem::BufferCollectionInfo_2 client_collection_info;
  fuchsia::sysmem::BufferCollectionSyncPtr collection_ptr;
  auto collection_id =
      SetupBufferCollection(1, 60, 40, BufferCollectionUsage::kClientImage, &renderer,
                            sysmem_allocator_.get(), &client_collection_info, collection_ptr);

  // Setup the render target collection.
  fuchsia::sysmem::BufferCollectionInfo_2 client_target_info;
  fuchsia::sysmem::BufferCollectionSyncPtr target_ptr;
  auto target_id = SetupBufferCollection(2, 60, 40, BufferCollectionUsage::kRenderTarget, &renderer,
                                         sysmem_allocator_.get(), &client_target_info, target_ptr);

  const uint32_t kTargetWidth = 32;
  const uint32_t kTargetHeight = 16;

  const uint32_t kTargetWidthFlipped = kTargetHeight;
  const uint32_t kTargetHeightFlipped = kTargetWidth;

  // Create the render_target image metadata.
  ImageMetadata render_target = {.collection_id = target_id,
                                 .identifier = allocation::GenerateUniqueImageId(),
                                 .vmo_index = 0,
                                 .width = kTargetWidth,
                                 .height = kTargetHeight};

  // Create another render target with dimensions flipped.
  ImageMetadata render_target_flipped = {.collection_id = target_id,
                                         .identifier = allocation::GenerateUniqueImageId(),
                                         .vmo_index = 1,
                                         .width = kTargetWidthFlipped,
                                         .height = kTargetHeightFlipped};

  // The texture width and height, also used for unnormalized texture coordinates.
  const float w = 2;
  const float h = 1;

  // Create the image meta data for the renderable.
  ImageMetadata renderable_texture = {.collection_id = collection_id,
                                      .identifier = allocation::GenerateUniqueImageId(),
                                      .vmo_index = 0,
                                      .width = static_cast<uint32_t>(w),
                                      .height = static_cast<uint32_t>(h)};

  auto import_res = renderer.ImportBufferImage(render_target, BufferCollectionUsage::kRenderTarget);
  EXPECT_TRUE(import_res);
  import_res =
      renderer.ImportBufferImage(render_target_flipped, BufferCollectionUsage::kRenderTarget);
  EXPECT_TRUE(import_res);

  import_res = renderer.ImportBufferImage(renderable_texture, BufferCollectionUsage::kClientImage);
  EXPECT_TRUE(import_res);

  // Create a renderable where the upper-left hand corner should be at position (5,3)
  // with a width/height of (6,2).
  const uint32_t kRenderableWidth = 6;
  const uint32_t kRenderableHeight = 2;
  ImageRect renderable(glm::vec2(5, 3), glm::vec2(kRenderableWidth, kRenderableHeight),
                       {glm::vec2(0, 0), glm::vec2(w, 0), glm::vec2(w, h), glm::vec2(0, h)},
                       Orientation::CCW_0_DEGREES);

  // Have the client write pixel values to the renderable's texture.
  MapHostPointer(client_collection_info, renderable_texture.vmo_index,
                 [&](uint8_t* vmo_host, uint32_t num_bytes) mutable {
                   // The texture only has 2 pixels, so it needs 8 write values for 4 channels. We
                   // set the first pixel to red and the second pixel to green.
                   const uint8_t kNumWrites = 4;
                   const uint8_t kWriteValues[] = {/*red*/ 255U, 0,    0, 255U,
                                                   /*green*/ 0,  255U, 0, 255U};
                   memcpy(vmo_host, kWriteValues, sizeof(kWriteValues));

                   // Flush the cache after writing to host VMO.
                   EXPECT_EQ(ZX_OK,
                             zx_cache_flush(vmo_host, kNumWrites,
                                            ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));
                 });

  // Render the renderable to the render target.
  renderer.Render(render_target, {renderable}, {renderable_texture});
  renderer.WaitIdle();

  // Get a raw pointer from the client collection's vmo that represents the render target
  // and read its values. This should show that the renderable was rendered to the center
  // of the render target, with its associated texture.
  MapHostPointer(client_target_info, render_target.vmo_index,
                 [&](uint8_t* vmo_host, uint32_t num_bytes) mutable {
                   // Flush the cache before reading back target image.
                   EXPECT_EQ(ZX_OK,
                             zx_cache_flush(vmo_host, kTargetWidth * kTargetHeight * 4,
                                            ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));

                   // Make sure the pixels are in the right order.
                   const auto red = glm::ivec4(255, 0, 0, 255);
                   const auto green = glm::ivec4(0, 255, 0, 255);

                   // Reds (left)
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 5, 3), red);
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 5, 4), red);
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 6, 3), red);
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 6, 4), red);
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 7, 3), red);
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 7, 4), red);

                   // Greens (right)
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 8, 3), green);
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 8, 4), green);
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 9, 3), green);
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 9, 4), green);
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 10, 3), green);
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 10, 4), green);

                   // Make sure the remaining pixels are black.
                   CHECK_BLACK_PIXELS(vmo_host, kTargetWidth, kTargetHeight,
                                      kRenderableWidth * kRenderableHeight);
                 });

  // Now let's update the renderable so it is rotated 90 deg.
  auto renderables_90deg = screen_capture::ScreenCapture::RotateRenderables(
      {renderable}, fuchsia::ui::composition::Rotation::CW_90_DEGREES, kTargetWidthFlipped,
      kTargetHeightFlipped);
  // Render the renderable to the render target.
  renderer.Render(render_target_flipped, std::move(renderables_90deg), {renderable_texture});
  renderer.WaitIdle();

  // Get a raw pointer from the client collection's vmo that represents the render target
  // and read its values. This should show that the renderable was rendered to the center
  // of the render target, with its associated texture.
  MapHostPointer(
      client_target_info, render_target_flipped.vmo_index,
      [&](uint8_t* vmo_host, uint32_t num_bytes) mutable {
        // Flush the cache before reading back target image.
        EXPECT_EQ(ZX_OK, zx_cache_flush(vmo_host, kTargetWidthFlipped * kTargetHeightFlipped * 4,
                                        ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));

        // Make sure the pixels are in the right order.
        const auto red = glm::ivec4(255, 0, 0, 255);
        const auto green = glm::ivec4(0, 255, 0, 255);

        // Reds (top)
        EXPECT_EQ(GetPixel(vmo_host, kTargetWidthFlipped, 11, 5), red);
        EXPECT_EQ(GetPixel(vmo_host, kTargetWidthFlipped, 12, 5), red);
        EXPECT_EQ(GetPixel(vmo_host, kTargetWidthFlipped, 11, 6), red);
        EXPECT_EQ(GetPixel(vmo_host, kTargetWidthFlipped, 12, 6), red);
        EXPECT_EQ(GetPixel(vmo_host, kTargetWidthFlipped, 11, 7), red);
        EXPECT_EQ(GetPixel(vmo_host, kTargetWidthFlipped, 12, 7), red);

        // Greens (bottom)
        EXPECT_EQ(GetPixel(vmo_host, kTargetWidthFlipped, 11, 8), green);
        EXPECT_EQ(GetPixel(vmo_host, kTargetWidthFlipped, 12, 8), green);
        EXPECT_EQ(GetPixel(vmo_host, kTargetWidthFlipped, 11, 9), green);
        EXPECT_EQ(GetPixel(vmo_host, kTargetWidthFlipped, 12, 9), green);
        EXPECT_EQ(GetPixel(vmo_host, kTargetWidthFlipped, 11, 10), green);
        EXPECT_EQ(GetPixel(vmo_host, kTargetWidthFlipped, 12, 10), green);

        // Make sure the remaining pixels are black.

        CHECK_BLACK_PIXELS(vmo_host, kTargetWidth, kTargetHeight,
                           kRenderableWidth * kRenderableHeight);
      });

  // Now let's update the renderable so it is rotated 180 deg.
  auto renderables_180deg = screen_capture::ScreenCapture::RotateRenderables(
      {renderable}, fuchsia::ui::composition::Rotation::CW_180_DEGREES, 16, 8);
  // Render the renderable to the render target.
  renderer.Render(render_target, std::move(renderables_180deg), {renderable_texture});
  renderer.WaitIdle();

  // Get a raw pointer from the client collection's vmo that represents the render target
  // and read its values. This should show that the renderable was rendered to the center
  // of the render target, with its associated texture.
  MapHostPointer(client_target_info, render_target.vmo_index,
                 [&](uint8_t* vmo_host, uint32_t num_bytes) mutable {
                   // Flush the cache before reading back target image.
                   EXPECT_EQ(ZX_OK,
                             zx_cache_flush(vmo_host, kTargetWidth * kTargetHeight * 4,
                                            ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));

                   // Make sure the pixels are in the right order.
                   const auto red = glm::ivec4(255, 0, 0, 255);
                   const auto green = glm::ivec4(0, 255, 0, 255);

                   // Greens (left)
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 5, 3), green);
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 5, 4), green);
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 6, 3), green);
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 6, 4), green);
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 7, 3), green);
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 7, 4), green);

                   // Reds (right)
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 8, 3), red);
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 8, 4), red);
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 9, 3), red);
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 9, 4), red);
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 10, 3), red);
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 10, 4), red);

                   // Make sure the remaining pixels are black.
                   CHECK_BLACK_PIXELS(vmo_host, kTargetWidth, kTargetHeight,
                                      kRenderableWidth * kRenderableHeight);
                 });

  // Now let's update the renderable so it is rotated 270 deg.
  auto renderables_270deg = screen_capture::ScreenCapture::RotateRenderables(
      {renderable}, fuchsia::ui::composition::Rotation::CW_270_DEGREES, kTargetWidthFlipped,
      kTargetHeightFlipped);
  // Render the renderable to the render target.
  renderer.Render(render_target_flipped, std::move(renderables_270deg), {renderable_texture});
  renderer.WaitIdle();

  // Get a raw pointer from the client collection's vmo that represents the render target
  // and read its values. This should show that the renderable was rendered to the center
  // of the render target, with its associated texture.
  MapHostPointer(
      client_target_info, render_target_flipped.vmo_index,
      [&](uint8_t* vmo_host, uint32_t num_bytes) mutable {
        // Flush the cache before reading back target image.
        EXPECT_EQ(ZX_OK, zx_cache_flush(vmo_host, kTargetWidthFlipped * kTargetHeightFlipped * 4,
                                        ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));

        // Make sure the pixels are in the right order.
        const auto red = glm::ivec4(255, 0, 0, 255);
        const auto green = glm::ivec4(0, 255, 0, 255);

        // Greens (top)
        EXPECT_EQ(GetPixel(vmo_host, kTargetWidthFlipped, 3, 21), green);
        EXPECT_EQ(GetPixel(vmo_host, kTargetWidthFlipped, 4, 21), green);
        EXPECT_EQ(GetPixel(vmo_host, kTargetWidthFlipped, 3, 22), green);
        EXPECT_EQ(GetPixel(vmo_host, kTargetWidthFlipped, 4, 22), green);
        EXPECT_EQ(GetPixel(vmo_host, kTargetWidthFlipped, 3, 23), green);
        EXPECT_EQ(GetPixel(vmo_host, kTargetWidthFlipped, 4, 23), green);

        // Reds (bottom)
        EXPECT_EQ(GetPixel(vmo_host, kTargetWidthFlipped, 3, 24), red);
        EXPECT_EQ(GetPixel(vmo_host, kTargetWidthFlipped, 4, 24), red);
        EXPECT_EQ(GetPixel(vmo_host, kTargetWidthFlipped, 3, 25), red);
        EXPECT_EQ(GetPixel(vmo_host, kTargetWidthFlipped, 4, 25), red);
        EXPECT_EQ(GetPixel(vmo_host, kTargetWidthFlipped, 3, 26), red);
        EXPECT_EQ(GetPixel(vmo_host, kTargetWidthFlipped, 4, 26), red);

        // Make sure the remaining pixels are black.
        CHECK_BLACK_PIXELS(vmo_host, kTargetWidth, kTargetHeight,
                           kRenderableWidth * kRenderableHeight);
      });
}

// Tests if the VK renderer can handle rendering an image without a provided image
// and only a multiply color (which means that we do not allocate an image for the
// renderable in this test, only the render target).
VK_TEST_F(VulkanRendererTest, SolidColorTest) {
  SKIP_TEST_IF_ESCHER_USES_DEVICE(VirtualGpu);
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher = std::make_unique<escher::Escher>(
      env->GetVulkanDevice(), env->GetFilesystem(), /*gpu_allocator*/ nullptr);
  VkRenderer renderer(unique_escher->GetWeakPtr());

  // Setup the render target collection.
  fuchsia::sysmem::BufferCollectionInfo_2 client_target_info;
  fuchsia::sysmem::BufferCollectionSyncPtr target_ptr;
  auto target_id = SetupBufferCollection(1, 60, 40, BufferCollectionUsage::kRenderTarget, &renderer,
                                         sysmem_allocator_.get(), &client_target_info, target_ptr);

  // Create the render_target image metadata.
  const uint32_t kTargetWidth = 16;
  const uint32_t kTargetHeight = 8;
  ImageMetadata render_target = {.collection_id = target_id,
                                 .identifier = allocation::GenerateUniqueImageId(),
                                 .vmo_index = 0,
                                 .width = kTargetWidth,
                                 .height = kTargetHeight};

  // Create the image meta data for the solid color renderable.
  ImageMetadata renderable_image_data = {
      .identifier = allocation::kInvalidImageId,
      .multiply_color = {1, 0.4, 0, 1},
      .blend_mode = fuchsia::ui::composition::BlendMode::SRC_OVER};

  renderer.ImportBufferImage(render_target, BufferCollectionUsage::kRenderTarget);

  // Create the two renderables.
  const uint32_t kRenderableWidth = 4;
  const uint32_t kRenderableHeight = 2;
  ImageRect renderable(glm::vec2(6, 3), glm::vec2(kRenderableWidth, kRenderableHeight));

  // Render the renderable to the render target.
  renderer.Render(render_target, {renderable}, {renderable_image_data});
  renderer.WaitIdle();

  // Get a raw pointer from the client collection's vmo that represents the render target
  // and read its values. This should show that the renderable was rendered to the center
  // of the render target, with its associated texture.
  MapHostPointer(client_target_info, render_target.vmo_index,
                 [&](uint8_t* vmo_host, uint32_t num_bytes) mutable {
                   // Flush the cache before reading back target image.
                   EXPECT_EQ(ZX_OK,
                             zx_cache_flush(vmo_host, kTargetWidth * kTargetHeight * 4,
                                            ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));

                   uint8_t linear_vals[num_bytes];
                   sRGBtoLinear(vmo_host, linear_vals, num_bytes);

                   // Make sure the pixels are in the right order give that we rotated
                   // the rectangle. Values are BGRA.
                   for (uint32_t i = 6; i < 6 + kRenderableWidth; i++) {
                     for (uint32_t j = 3; j < 3 + kRenderableHeight; j++) {
                       auto pixel = GetPixel(linear_vals, kTargetWidth, i, j);
                       // The sRGB conversion function provides slightly different results depending
                       // on the platform.
                       EXPECT_TRUE(pixel == glm::ivec4(0, 101, 255, 255) ||
                                   pixel == glm::ivec4(0, 102, 255, 255));
                     }
                   }

                   // Make sure the remaining pixels are black.
                   CHECK_BLACK_PIXELS(vmo_host, kTargetWidth, kTargetHeight, 8U);
                 });
}

// Test that colors change properly when we apply a color correction matrix.
VK_TEST_F(VulkanRendererTest, ColorCorrectionTest) {
  SKIP_TEST_IF_ESCHER_USES_DEVICE(VirtualGpu);
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher = std::make_unique<escher::Escher>(
      env->GetVulkanDevice(), env->GetFilesystem(), /*gpu_allocator*/ nullptr);
  VkRenderer renderer(unique_escher->GetWeakPtr());

  // Set the color correction data on the renderer.
  static const std::array<float, 3> preoffsets = {0, 0, 0};
  static const std::array<float, 9> matrix = {.288299,  0.052709, -0.257912, 0.711701, 0.947291,
                                              0.257912, 0.000000, -0.000000, 1.000000};
  static const std::array<float, 3> postoffsets = {0, 0, 0};
  renderer.SetColorConversionValues(matrix, preoffsets, postoffsets);

  // Setup the render target collection.
  fuchsia::sysmem::BufferCollectionInfo_2 client_target_info;
  fuchsia::sysmem::BufferCollectionSyncPtr target_ptr;
  auto target_id = SetupBufferCollection(1, 60, 40, BufferCollectionUsage::kRenderTarget, &renderer,
                                         sysmem_allocator_.get(), &client_target_info, target_ptr);

  // Create the render_target image metadata.
  const uint32_t kTargetWidth = 16;
  const uint32_t kTargetHeight = 8;
  ImageMetadata render_target = {.collection_id = target_id,
                                 .identifier = allocation::GenerateUniqueImageId(),
                                 .vmo_index = 0,
                                 .width = kTargetWidth,
                                 .height = kTargetHeight};

  // Create the image meta data for the solid color renderable.
  ImageMetadata renderable_image_data = {
      .identifier = allocation::kInvalidImageId,
      .multiply_color = {1, 0, 0, 1},
      .blend_mode = fuchsia::ui::composition::BlendMode::SRC_OVER};

  renderer.ImportBufferImage(render_target, BufferCollectionUsage::kRenderTarget);

  // Create the two renderables.
  const uint32_t kRenderableWidth = 4;
  const uint32_t kRenderableHeight = 2;
  ImageRect renderable(glm::vec2(6, 3), glm::vec2(kRenderableWidth, kRenderableHeight));

  // Render the renderable to the render target.
  renderer.Render(render_target, {renderable}, {renderable_image_data}, /*fences*/ {},
                  /*color_conversion*/ true);
  renderer.WaitIdle();

  // Calculate expected color.
  const glm::mat4 glm_matrix(.288299, 0.052709, -0.257912, 0.00000, 0.711701, 0.947291, 0.257912,
                             0.00000, 0.000000, -0.000000, 1.000000, 0.00000, 0.000000, 0.000000,
                             0.00000, 1.00000);
  auto expected_color_float = glm_matrix * glm::vec4(1, 0, 0, 1);

  // Order needs to be BGRA.
  glm::ivec4 expected_color = {
      static_cast<uint8_t>(std::max(expected_color_float.z * 255, 0.f)),
      static_cast<uint8_t>(std::max(expected_color_float.y * 255, 0.f)),
      static_cast<uint8_t>(std::max(expected_color_float.x * 255, 0.f)),
      static_cast<uint8_t>(std::max(expected_color_float.w * 255, 0.f)),
  };

  // Get a raw pointer from the client collection's vmo that represents the render target
  // and read its values. This should show that the renderable was rendered to the center
  // of the render target, with its associated texture.
  MapHostPointer(client_target_info, render_target.vmo_index,
                 [&](uint8_t* vmo_host, uint32_t num_bytes) mutable {
                   // Flush the cache before reading back target image.
                   EXPECT_EQ(ZX_OK,
                             zx_cache_flush(vmo_host, kTargetWidth * kTargetHeight * 4,
                                            ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));

                   uint8_t linear_vals[num_bytes];
                   sRGBtoLinear(vmo_host, linear_vals, num_bytes);

                   // Make sure the pixels are in the right order give that we rotated
                   // the rectangle. Values are BGRA.
                   for (uint32_t i = 6; i < 6 + kRenderableWidth; i++) {
                     for (uint32_t j = 3; j < 3 + kRenderableHeight; j++) {
                       auto pixel = GetPixel(linear_vals, kTargetWidth, i, j);
                       EXPECT_TRUE(pixel == expected_color);
                     }
                   }

                   // Make sure the remaining pixels are black.
                   CHECK_BLACK_PIXELS(vmo_host, kTargetWidth, kTargetHeight, 8U);
                 });
}

// Tests if the VK renderer can handle rendering 2 solid color images. Since solid
// color images make use of a shared default 1x1 white texture within the vk renderer,
// this tests to make sure that there aren't any problems that arise from this sharing.
VK_TEST_F(VulkanRendererTest, MultipleSolidColorTest) {
  SKIP_TEST_IF_ESCHER_USES_DEVICE(VirtualGpu);
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher = std::make_unique<escher::Escher>(
      env->GetVulkanDevice(), env->GetFilesystem(), /*gpu_allocator*/ nullptr);
  VkRenderer renderer(unique_escher->GetWeakPtr());

  // Setup the render target collection.
  fuchsia::sysmem::BufferCollectionInfo_2 client_target_info;
  fuchsia::sysmem::BufferCollectionSyncPtr target_ptr;
  auto target_id = SetupBufferCollection(1, 60, 40, BufferCollectionUsage::kRenderTarget, &renderer,
                                         sysmem_allocator_.get(), &client_target_info, target_ptr);

  // Create the render_target image metadata.
  const uint32_t kTargetWidth = 16;
  const uint32_t kTargetHeight = 8;
  ImageMetadata render_target = {.collection_id = target_id,
                                 .identifier = allocation::GenerateUniqueImageId(),
                                 .vmo_index = 0,
                                 .width = kTargetWidth,
                                 .height = kTargetHeight};

  // Create the image meta data for the solid color renderable - red.
  ImageMetadata renderable_image_data = {
      .identifier = allocation::kInvalidImageId,
      .multiply_color = {1, 0, 0, 1},
      .blend_mode = fuchsia::ui::composition::BlendMode::SRC_OVER};

  // Create the image meta data for the other solid color renderable - blue.
  ImageMetadata renderable_image_data_2 = {
      .identifier = allocation::kInvalidImageId,
      .multiply_color = {0, 0, 1, 1},
      .blend_mode = fuchsia::ui::composition::BlendMode::SRC_OVER};

  renderer.ImportBufferImage(render_target, BufferCollectionUsage::kRenderTarget);

  // Create the two renderables.
  const uint32_t kRenderableWidth = 4;
  const uint32_t kRenderableHeight = 2;
  ImageRect renderable(glm::vec2(6, 3), glm::vec2(kRenderableWidth, kRenderableHeight));
  ImageRect renderable_2(glm::vec2(6, 5), glm::vec2(kRenderableWidth, kRenderableHeight));

  // Render the renderable to the render target.
  renderer.Render(render_target, {renderable, renderable_2},
                  {renderable_image_data, renderable_image_data_2});
  renderer.WaitIdle();

  // Get a raw pointer from the client collection's vmo that represents the render target
  // and read its values. This should show that the renderable was rendered to the center
  // of the render target, with its associated texture.
  MapHostPointer(
      client_target_info, render_target.vmo_index,
      [&](uint8_t* vmo_host, uint32_t num_bytes) mutable {
        // Flush the cache before reading back target image.
        EXPECT_EQ(ZX_OK, zx_cache_flush(vmo_host, kTargetWidth * kTargetHeight * 4,
                                        ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));

        uint8_t linear_vals[num_bytes];
        sRGBtoLinear(vmo_host, linear_vals, num_bytes);

        // Make sure the pixels are in the right order give that we rotated
        // the rectangle.
        for (uint32_t i = 6; i < 6 + kRenderableWidth; i++) {
          for (uint32_t j = 3; j < 3 + kRenderableHeight; j++) {
            EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, i, j), glm::ivec4(0, 0, 255, 255));
          }
        }

        for (uint32_t i = 6; i < 6 + kRenderableWidth; i++) {
          for (uint32_t j = 5; j < 5 + kRenderableHeight; j++) {
            EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, i, j), glm::ivec4(255, 0, 0, 255));
          }
        }

        // Make sure the remaining pixels are black.
        CHECK_BLACK_PIXELS(vmo_host, kTargetWidth, kTargetHeight, 16U);
      });
}

// Tests if the VK renderer can handle rendering a solid color rectangle as well as
// an image-backed rectangle. Make sure that the two rectangles, if given the same
// dimensions, occupy the exact same number of pixels.
VK_TEST_F(VulkanRendererTest, MixSolidColorAndImageTest) {
  SKIP_TEST_IF_ESCHER_USES_DEVICE(VirtualGpu);
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher = std::make_unique<escher::Escher>(
      env->GetVulkanDevice(), env->GetFilesystem(), /*gpu_allocator*/ nullptr);
  VkRenderer renderer(unique_escher->GetWeakPtr());

  // Both renderables should be the same size.
  const uint32_t kRenderableWidth = 93;
  const uint32_t kRenderableHeight = 78;
  fuchsia::sysmem::BufferCollectionInfo_2 client_collection_info;
  fuchsia::sysmem::BufferCollectionSyncPtr collection_ptr;
  auto collection_id =
      SetupBufferCollection(1, 100, 100, BufferCollectionUsage::kClientImage, &renderer,
                            sysmem_allocator_.get(), &client_collection_info, collection_ptr);

  // Setup the render target collection.
  const uint32_t kTargetWidth = 200;
  const uint32_t kTargetHeight = 100;
  fuchsia::sysmem::BufferCollectionInfo_2 client_target_info;
  fuchsia::sysmem::BufferCollectionSyncPtr target_ptr;
  auto target_id =
      SetupBufferCollection(1, 200, 100, BufferCollectionUsage::kRenderTarget, &renderer,
                            sysmem_allocator_.get(), &client_target_info, target_ptr);

  // Create the render_target image metadata.
  ImageMetadata render_target = {.collection_id = target_id,
                                 .identifier = allocation::GenerateUniqueImageId(),
                                 .vmo_index = 0,
                                 .width = kTargetWidth,
                                 .height = kTargetHeight};

  // Create the image meta data for the solid color renderable - green.
  ImageMetadata renderable_image_data = {
      .identifier = allocation::kInvalidImageId,
      .multiply_color = {0, 1, 0, 1},
      .blend_mode = fuchsia::ui::composition::BlendMode::SRC_OVER};

  // Create the image meta data for the image backed renderable - red.
  ImageMetadata renderable_image_data_2 = {.collection_id = collection_id,
                                           .identifier = allocation::GenerateUniqueImageId(),
                                           .vmo_index = 0,
                                           .width = kRenderableWidth,
                                           .height = kRenderableHeight};

  // Have the client write pixel values to the renderable's texture. They should all be red.
  MapHostPointer(client_collection_info, renderable_image_data_2.vmo_index,
                 [&](uint8_t* vmo_host, uint32_t num_bytes) mutable {
                   uint8_t writeValues[num_bytes];
                   for (uint32_t i = 0; i < num_bytes; i += 4) {
                     writeValues[i] = 255U;
                     writeValues[i + 1] = 0;
                     writeValues[i + 2] = 0;
                     writeValues[i + 3] = 255U;
                   }

                   memcpy(vmo_host, writeValues, sizeof(writeValues));

                   // Flush the cache after writing to host VMO.
                   EXPECT_EQ(ZX_OK,
                             zx_cache_flush(vmo_host, num_bytes,
                                            ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));
                 });

  renderer.ImportBufferImage(renderable_image_data_2, BufferCollectionUsage::kClientImage);
  renderer.ImportBufferImage(render_target, BufferCollectionUsage::kRenderTarget);

  // Create the two renderables.
  ImageRect renderable(glm::vec2(0, 0), glm::vec2(kRenderableWidth, kRenderableHeight));
  ImageRect renderable_2(glm::vec2(kRenderableWidth + 1, 0),
                         glm::vec2(kRenderableWidth, kRenderableHeight));

  // Render the renderable to the render target.
  renderer.Render(render_target, {renderable, renderable_2},
                  {renderable_image_data, renderable_image_data_2});
  renderer.WaitIdle();

  // Get a raw pointer from the client collection's vmo that represents the render target
  // and read its values. This should show that the two renderables were rendered side by
  // side at the same size.
  MapHostPointer(client_target_info, render_target.vmo_index,
                 [&](uint8_t* vmo_host, uint32_t num_bytes) mutable {
                   // Flush the cache before reading back target image.
                   EXPECT_EQ(ZX_OK,
                             zx_cache_flush(vmo_host, kTargetWidth * kTargetHeight * 4,
                                            ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));

                   uint8_t linear_vals[num_bytes];
                   sRGBtoLinear(vmo_host, linear_vals, num_bytes);

                   uint32_t num_red = 0, num_green = 0;
                   for (uint32_t i = 0; i < kTargetWidth; i++) {
                     for (uint32_t j = 0; j < kTargetHeight; j++) {
                       auto pixel = GetPixel(linear_vals, kTargetWidth, i, j);
                       if (pixel == glm::ivec4(0, 255, 0, 255)) {
                         num_green++;
                       } else if (pixel == glm::ivec4(255, 0, 0, 255)) {
                         num_red++;
                       }
                     }
                   }

                   EXPECT_EQ(num_green, num_red);
                   EXPECT_EQ(num_green, kRenderableWidth * kRenderableHeight);
                   EXPECT_EQ(num_red, kRenderableWidth * kRenderableHeight);
                   CHECK_BLACK_PIXELS(vmo_host, kTargetWidth, kTargetHeight,
                                      2 * (kRenderableWidth * kRenderableHeight));
                 });
}

// Tests transparency. Render two overlapping rectangles, a red opaque one covered slightly by
// a green transparent one with an alpha of 0.5. The result should look like this:
//
// ----------------
// ----------------
// ----------------
// ------RYYYG----
// ------RYYYG----
// ----------------
// ----------------
// ----------------
VK_TEST_F(VulkanRendererTest, TransparencyTest) {
  SKIP_TEST_IF_ESCHER_USES_DEVICE(VirtualGpu);
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher = std::make_unique<escher::Escher>(
      env->GetVulkanDevice(), env->GetFilesystem(), /*gpu_allocator*/ nullptr);
  VkRenderer renderer(unique_escher->GetWeakPtr());

  fuchsia::sysmem::BufferCollectionInfo_2 client_collection_info;
  fuchsia::sysmem::BufferCollectionSyncPtr collection_ptr;
  auto collection_id =
      SetupBufferCollection(2, 60, 40, BufferCollectionUsage::kClientImage, &renderer,
                            sysmem_allocator_.get(), &client_collection_info, collection_ptr);

  // Setup the render target collection.
  fuchsia::sysmem::BufferCollectionInfo_2 client_target_info;
  fuchsia::sysmem::BufferCollectionSyncPtr target_ptr;
  auto target_id = SetupBufferCollection(1, 60, 40, BufferCollectionUsage::kRenderTarget, &renderer,
                                         sysmem_allocator_.get(), &client_target_info, target_ptr);

  const uint32_t kTargetWidth = 16;
  const uint32_t kTargetHeight = 8;

  // Create the render_target image metadata.
  ImageMetadata render_target = {.collection_id = target_id,
                                 .identifier = allocation::GenerateUniqueImageId(),
                                 .vmo_index = 0,
                                 .width = kTargetWidth,
                                 .height = kTargetHeight};

  // Create the image meta data for the renderable.
  ImageMetadata renderable_texture = {.collection_id = collection_id,
                                      .identifier = allocation::GenerateUniqueImageId(),
                                      .vmo_index = 0,
                                      .width = 1,
                                      .height = 1};

  // Create the texture that will go on the transparent renderable.
  ImageMetadata transparent_texture = {.collection_id = collection_id,
                                       .identifier = allocation::GenerateUniqueImageId(),
                                       .vmo_index = 1,
                                       .width = 1,
                                       .height = 1,
                                       .blend_mode = fuchsia::ui::composition::BlendMode::SRC_OVER};

  // Import all the images.
  renderer.ImportBufferImage(render_target, BufferCollectionUsage::kRenderTarget);
  renderer.ImportBufferImage(renderable_texture, BufferCollectionUsage::kClientImage);
  renderer.ImportBufferImage(transparent_texture, BufferCollectionUsage::kClientImage);

  // Create the two renderables.
  const uint32_t kRenderableWidth = 4;
  const uint32_t kRenderableHeight = 2;
  ImageRect renderable(glm::vec2(6, 3), glm::vec2(kRenderableWidth, kRenderableHeight));
  ImageRect transparent_renderable(glm::vec2(7, 3), glm::vec2(kRenderableWidth, kRenderableHeight));

  // Have the client write pixel values to the renderable's texture.
  MapHostPointer(client_collection_info, renderable_texture.vmo_index,
                 [&](uint8_t* vmo_host, uint32_t num_bytes) mutable {
                   // Create a red opaque pixel.
                   const uint8_t kNumWrites = 4;
                   const uint8_t kWriteValues[] = {/*red*/ 255U, 0, 0, 255U};
                   memcpy(vmo_host, kWriteValues, sizeof(kWriteValues));

                   // Flush the cache after writing to host VMO.
                   EXPECT_EQ(ZX_OK,
                             zx_cache_flush(vmo_host, kNumWrites,
                                            ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));
                 });

  MapHostPointer(client_collection_info, transparent_texture.vmo_index,
                 [&](uint8_t* vmo_host, uint32_t num_bytes) mutable {
                   // Create a green pixel with an alpha of 0.5.
                   const uint8_t kNumWrites = 4;
                   const uint8_t kWriteValues[] = {/*red*/ 0, 255, 0, 128U};
                   memcpy(vmo_host, kWriteValues, sizeof(kWriteValues));

                   // Flush the cache after writing to host VMO.
                   EXPECT_EQ(ZX_OK,
                             zx_cache_flush(vmo_host, kNumWrites,
                                            ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));
                 });

  // Render the renderable to the render target.
  renderer.Render(render_target, {renderable, transparent_renderable},
                  {renderable_texture, transparent_texture});
  renderer.WaitIdle();

  // Get a raw pointer from the client collection's vmo that represents the render target
  // and read its values. This should show that the renderable was rendered to the center
  // of the render target, with its associated texture.
  MapHostPointer(
      client_target_info, render_target.vmo_index,
      [&](uint8_t* vmo_host, uint32_t num_bytes) mutable {
        // Flush the cache before reading back target image.
        EXPECT_EQ(ZX_OK, zx_cache_flush(vmo_host, kTargetWidth * kTargetHeight * 4,
                                        ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));

        uint8_t linear_vals[num_bytes];
        sRGBtoLinear(vmo_host, linear_vals, num_bytes);

        // Make sure the pixels are in the right order give that we rotated
        // the rectangle.
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 6, 3), glm::ivec4(255, 0, 0, 255));
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 6, 4), glm::ivec4(255, 0, 0, 255));
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 7, 3), glm::ivec4(126, 255, 0, 255));
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 7, 4), glm::ivec4(126, 255, 0, 255));
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 8, 3), glm::ivec4(126, 255, 0, 255));
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 8, 4), glm::ivec4(126, 255, 0, 255));
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 9, 3), glm::ivec4(126, 255, 0, 255));
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 9, 4), glm::ivec4(126, 255, 0, 255));
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 10, 3), glm::ivec4(0, 255, 0, 128));
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 10, 4), glm::ivec4(0, 255, 0, 128));

        // Make sure the remaining pixels are black.
        CHECK_BLACK_PIXELS(vmo_host, kTargetWidth, kTargetHeight, 10U);
      });
}

// Partial ordering, true if it's true for all components.
bool operator<=(const glm::tvec4<int, glm::packed_highp>& a,
                const glm::tvec4<int, glm::packed_highp>& b) {
  return a.x <= b.x && a.y <= b.y && a.z <= b.z && a.w <= b.w;
}

MATCHER_P2(InRange, low, high, "") { return low <= arg && arg <= high; }
// Tests the multiply color for images, which can also affect transparency.
// Render two overlapping rectangles, a red opaque one covered slightly by
// a green transparent one with an alpha of 0.5. These values are set not
// on the pixel values of the images which should be all white and opaque
// (1,1,1,1) but instead via the multiply_color value on the ImageMetadata.
// ----------------
// ----------------
// ----------------
// ------RYYYG----
// ------RYYYG----
// ----------------
// ----------------
// ----------------
VK_TEST_F(VulkanRendererTest, MultiplyColorTest) {
  SKIP_TEST_IF_ESCHER_USES_DEVICE(VirtualGpu);
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher = std::make_unique<escher::Escher>(
      env->GetVulkanDevice(), env->GetFilesystem(), /*gpu_allocator*/ nullptr);
  VkRenderer renderer(unique_escher->GetWeakPtr());

  fuchsia::sysmem::BufferCollectionInfo_2 client_collection_info;
  fuchsia::sysmem::BufferCollectionSyncPtr collection_ptr;
  auto collection_id =
      SetupBufferCollection(1, 1, 1, BufferCollectionUsage::kClientImage, &renderer,
                            sysmem_allocator_.get(), &client_collection_info, collection_ptr);

  // Setup the render target collection.
  fuchsia::sysmem::BufferCollectionInfo_2 client_target_info;
  fuchsia::sysmem::BufferCollectionSyncPtr target_ptr;
  auto target_id = SetupBufferCollection(1, 60, 40, BufferCollectionUsage::kRenderTarget, &renderer,
                                         sysmem_allocator_.get(), &client_target_info, target_ptr);

  const uint32_t kTargetWidth = 16;
  const uint32_t kTargetHeight = 8;

  // Create the render_target image metadata.
  ImageMetadata render_target = {.collection_id = target_id,
                                 .identifier = allocation::GenerateUniqueImageId(),
                                 .vmo_index = 0,
                                 .width = kTargetWidth,
                                 .height = kTargetHeight};

  // Create the image meta data for the renderable.
  ImageMetadata renderable_texture = {.collection_id = collection_id,
                                      .identifier = allocation::GenerateUniqueImageId(),
                                      .vmo_index = 0,
                                      .width = 1,
                                      .height = 1,
                                      .multiply_color = {1, 0, 0, 1},
                                      .blend_mode = fuchsia::ui::composition::BlendMode::SRC_OVER};

  // Create the texture that will go on the transparent renderable.
  ImageMetadata transparent_texture = {.collection_id = collection_id,
                                       .identifier = allocation::GenerateUniqueImageId(),
                                       .vmo_index = 0,
                                       .width = 1,
                                       .height = 1,
                                       .multiply_color = {0, 1, 0, 0.5},
                                       .blend_mode = fuchsia::ui::composition::BlendMode::SRC_OVER};

  // Import all the images.
  renderer.ImportBufferImage(render_target, BufferCollectionUsage::kRenderTarget);
  renderer.ImportBufferImage(renderable_texture, BufferCollectionUsage::kClientImage);
  renderer.ImportBufferImage(transparent_texture, BufferCollectionUsage::kClientImage);

  // Create the two renderables.
  const uint32_t kRenderableWidth = 4;
  const uint32_t kRenderableHeight = 2;
  ImageRect renderable(glm::vec2(6, 3), glm::vec2(kRenderableWidth, kRenderableHeight));
  ImageRect transparent_renderable(glm::vec2(7, 3), glm::vec2(kRenderableWidth, kRenderableHeight));

  // Have the client write white pixel values to image backing the above two renderables.
  MapHostPointer(client_collection_info, renderable_texture.vmo_index,
                 [&](uint8_t* vmo_host, uint32_t num_bytes) mutable {
                   // Create a red opaque pixel.
                   const uint8_t kNumWrites = 4;
                   const uint8_t kWriteValues[] = {/*red*/ 255U, /*green*/ 255U, /*blue*/ 255U,
                                                   /*alpha*/ 255U};
                   memcpy(vmo_host, kWriteValues, sizeof(kWriteValues));

                   // Flush the cache after writing to host VMO.
                   EXPECT_EQ(ZX_OK,
                             zx_cache_flush(vmo_host, kNumWrites,
                                            ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));
                 });

  // Render the renderable to the render target.
  renderer.Render(render_target, {renderable, transparent_renderable},
                  {renderable_texture, transparent_texture});
  renderer.WaitIdle();

  // Get a raw pointer from the client collection's vmo that represents the render target
  // and read its values. This should show that the renderable was rendered to the center
  // of the render target, with its associated texture.
  MapHostPointer(
      client_target_info, render_target.vmo_index,
      [&](uint8_t* vmo_host, uint32_t num_bytes) mutable {
        // Flush the cache before reading back target image.
        EXPECT_EQ(ZX_OK, zx_cache_flush(vmo_host, kTargetWidth * kTargetHeight * 4,
                                        ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));

        uint8_t linear_vals[num_bytes];
        sRGBtoLinear(vmo_host, linear_vals, num_bytes);

        // Different platforms have slightly different sRGB<->linear conversions, so use fuzzy
        // matching. Intel Gen value:
        constexpr uint32_t kCompLow = 126;
        // ARM Mali value:
        constexpr uint32_t kCompHigh = 128;
        const auto kLowValue = glm::ivec4(0, kCompLow, kCompLow, 255);
        const auto kHighValue = glm::ivec4(0, kCompHigh, kCompHigh, 255);

        // Make sure the pixels are in the right order give that we rotated
        // the rectangle.
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 6, 3), glm::ivec4(0, 0, 255, 255));
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 6, 4), glm::ivec4(0, 0, 255, 255));
        EXPECT_THAT(GetPixel(linear_vals, kTargetWidth, 7, 3), InRange(kLowValue, kHighValue));
        EXPECT_THAT(GetPixel(linear_vals, kTargetWidth, 7, 4), InRange(kLowValue, kHighValue));
        EXPECT_THAT(GetPixel(linear_vals, kTargetWidth, 8, 3), InRange(kLowValue, kHighValue));
        EXPECT_THAT(GetPixel(linear_vals, kTargetWidth, 8, 4), InRange(kLowValue, kHighValue));
        EXPECT_THAT(GetPixel(linear_vals, kTargetWidth, 9, 3), InRange(kLowValue, kHighValue));
        EXPECT_THAT(GetPixel(linear_vals, kTargetWidth, 9, 4), InRange(kLowValue, kHighValue));
        EXPECT_THAT(GetPixel(linear_vals, kTargetWidth, 10, 3),
                    InRange(glm::ivec4(0, kCompLow, 0, 128), glm::ivec4(0, kCompHigh, 0, 128)));
        EXPECT_THAT(GetPixel(linear_vals, kTargetWidth, 10, 4),
                    InRange(glm::ivec4(0, kCompLow, 0, 128), glm::ivec4(0, kCompHigh, 0, 128)));

        // Make sure the remaining pixels are black.
        CHECK_BLACK_PIXELS(vmo_host, kTargetWidth, kTargetHeight, 10U);
      });
}

class VulkanRendererParameterizedYuvTest
    : public VulkanRendererTest,
      public ::testing::WithParamInterface<fuchsia::sysmem::PixelFormatType> {};

// This test actually renders a YUV format texture using the VKRenderer. We create a single
// rectangle, with a fuchsia texture. The render target and the rectangle are 32x32.
VK_TEST_P(VulkanRendererParameterizedYuvTest, YuvTest) {
  SKIP_TEST_IF_ESCHER_USES_DEVICE(VirtualGpu);
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher = std::make_unique<escher::Escher>(
      env->GetVulkanDevice(), env->GetFilesystem(), /*gpu_allocator*/ nullptr);
  VkRenderer renderer(unique_escher->GetWeakPtr());

  // Create a pair of tokens for the Image allocation.
  auto image_tokens = flatland::SysmemTokens::Create(sysmem_allocator_.get());

  // Register the Image token with the renderer.
  auto image_collection_id = allocation::GenerateUniqueBufferCollectionId();
  auto result = renderer.ImportBufferCollection(image_collection_id, sysmem_allocator_.get(),
                                                std::move(image_tokens.dup_token),
                                                BufferCollectionUsage::kClientImage, std::nullopt);
  EXPECT_TRUE(result);

  const uint32_t kTargetWidth = 32;
  const uint32_t kTargetHeight = 32;

  // Set the local constraints for the Image.
  const fuchsia::sysmem::PixelFormatType pixel_format = GetParam();
  auto [buffer_usage, memory_constraints] = GetUsageAndMemoryConstraintsForCpuWriteOften();
  auto image_collection = CreateBufferCollectionSyncPtrAndSetConstraints(
      sysmem_allocator_.get(), std::move(image_tokens.local_token),
      /*image_count*/ 1,
      /*width*/ kTargetWidth,
      /*height*/ kTargetHeight, buffer_usage, pixel_format, std::make_optional(memory_constraints));

  // Wait for buffers allocated so it can populate its information struct with the vmo data.
  fuchsia::sysmem::BufferCollectionInfo_2 image_collection_info = {};
  {
    zx_status_t allocation_status = ZX_OK;
    auto status =
        image_collection->WaitForBuffersAllocated(&allocation_status, &image_collection_info);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(allocation_status, ZX_OK);
    EXPECT_EQ(image_collection_info.settings.image_format_constraints.pixel_format.type,
              pixel_format);
  }

  // Create the image meta data for the Image and import.
  ImageMetadata image_metadata = {.collection_id = image_collection_id,
                                  .identifier = allocation::GenerateUniqueImageId(),
                                  .vmo_index = 0,
                                  .width = kTargetWidth,
                                  .height = kTargetHeight};
  auto import_res = renderer.ImportBufferImage(image_metadata, BufferCollectionUsage::kClientImage);
  EXPECT_TRUE(import_res);

  // Create a pair of tokens for the render target allocation.
  auto render_target_tokens = flatland::SysmemTokens::Create(sysmem_allocator_.get());

  // Register the render target tokens with the renderer.
  auto render_target_collection_id = allocation::GenerateUniqueBufferCollectionId();
  result = renderer.ImportBufferCollection(render_target_collection_id, sysmem_allocator_.get(),
                                           std::move(render_target_tokens.dup_token),
                                           BufferCollectionUsage::kRenderTarget, std::nullopt);
  EXPECT_TRUE(result);

  // Create a client-side handle to the render target's buffer collection and set the client
  // constraints.
  auto render_target_collection = CreateBufferCollectionSyncPtrAndSetConstraints(
      sysmem_allocator_.get(), std::move(render_target_tokens.local_token),
      /*image_count*/ 1,
      /*width*/ kTargetWidth,
      /*height*/ kTargetHeight, buffer_usage, fuchsia::sysmem::PixelFormatType::BGRA32,
      std::make_optional(memory_constraints));

  // Wait for buffers allocated so it can populate its information struct with the vmo data.
  fuchsia::sysmem::BufferCollectionInfo_2 render_target_collection_info = {};
  {
    zx_status_t allocation_status = ZX_OK;
    auto status = render_target_collection->WaitForBuffersAllocated(&allocation_status,
                                                                    &render_target_collection_info);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(allocation_status, ZX_OK);
  }

  // Create the render_target image metadata and import.
  ImageMetadata render_target_metadata = {.collection_id = render_target_collection_id,
                                          .identifier = allocation::GenerateUniqueImageId(),
                                          .vmo_index = 0,
                                          .width = kTargetWidth,
                                          .height = kTargetHeight};
  import_res =
      renderer.ImportBufferImage(render_target_metadata, BufferCollectionUsage::kRenderTarget);
  EXPECT_TRUE(import_res);

  // Create a renderable where the upper-left hand corner should be at position (0,0) with a
  // width/height of (32,32).
  ImageRect image_renderable(glm::vec2(0, 0), glm::vec2(kTargetWidth, kTargetHeight));

  const uint32_t num_pixels = kTargetWidth * kTargetHeight;
  const uint8_t kFuchsiaYuvValues[] = {110U, 192U, 192U};
  const uint8_t kFuchsiaBgraValues[] = {246U, 68U, 228U, 255U};
  // Have the client write pixel values to the renderable Image's texture.
  MapHostPointer(
      image_collection_info, image_metadata.vmo_index,
      [&](uint8_t* vmo_host, uint32_t num_bytes) mutable {
        for (uint32_t i = 0; i < num_pixels; ++i) {
          vmo_host[i] = kFuchsiaYuvValues[0];
        }
        switch (GetParam()) {
          case fuchsia::sysmem::PixelFormatType::NV12:
            for (uint32_t i = num_pixels; i < num_pixels + num_pixels / 2; i += 2) {
              vmo_host[i] = kFuchsiaYuvValues[1];
              vmo_host[i + 1] = kFuchsiaYuvValues[2];
            }
            break;
            break;
          case fuchsia::sysmem::PixelFormatType::I420:
            for (uint32_t i = num_pixels; i < num_pixels + num_pixels / 4; ++i) {
              vmo_host[i] = kFuchsiaYuvValues[1];
            }
            for (uint32_t i = num_pixels + num_pixels / 4; i < num_pixels + num_pixels / 2; ++i) {
              vmo_host[i] = kFuchsiaYuvValues[2];
            }
            break;
          default:
            FX_NOTREACHED();
        }

        // Flush the cache after writing to host VMO.
        EXPECT_EQ(ZX_OK, zx_cache_flush(vmo_host, num_pixels + num_pixels / 2,
                                        ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));
      });

  // Render the renderable to the render target.
  renderer.Render(render_target_metadata, {image_renderable}, {image_metadata});
  renderer.WaitIdle();

  // Get a raw pointer from the client collection's vmo that represents the render target and read
  // its values. This should show that the renderable was rendered with expected BGRA colors.
  MapHostPointer(render_target_collection_info, render_target_metadata.vmo_index,
                 [&](uint8_t* vmo_host, uint32_t num_bytes) mutable {
                   // Flush the cache before reading back target image.
                   EXPECT_EQ(ZX_OK,
                             zx_cache_flush(vmo_host, num_pixels * 4,
                                            ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));

                   // Make sure the pixels are fuchsia.
                   for (uint32_t y = 0; y < kTargetHeight; y++) {
                     for (uint32_t x = 0; x < kTargetWidth; x++) {
                       EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, x, y),
                                 glm::ivec4(kFuchsiaBgraValues[0], kFuchsiaBgraValues[1],
                                            kFuchsiaBgraValues[2], kFuchsiaBgraValues[3]));
                     }
                   }
                 });
}

INSTANTIATE_TEST_SUITE_P(YuvPixelFormats, VulkanRendererParameterizedYuvTest,
                         ::testing::Values(fuchsia::sysmem::PixelFormatType::NV12,
                                           fuchsia::sysmem::PixelFormatType::I420));

// This test actually renders a protected memory backed image using the VKRenderer.
VK_TEST_F(VulkanRendererTest, ProtectedMemoryTest) {
  SKIP_TEST_IF_ESCHER_USES_DEVICE(VirtualGpu);
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher = escher::test::CreateEscherWithProtectedMemoryEnabled();
  if (!unique_escher) {
    FX_LOGS(WARNING) << "Protected memory not supported. Test skipped.";
    GTEST_SKIP();
  }
  VkRenderer renderer(unique_escher->GetWeakPtr());

  // Create a pair of tokens for the Image allocation.
  auto image_tokens = flatland::SysmemTokens::Create(sysmem_allocator_.get());

  // Register the Image token with the renderer.
  auto image_collection_id = allocation::GenerateUniqueBufferCollectionId();
  auto result = renderer.ImportBufferCollection(image_collection_id, sysmem_allocator_.get(),
                                                std::move(image_tokens.dup_token),
                                                BufferCollectionUsage::kClientImage, std::nullopt);
  EXPECT_TRUE(result);

  const uint32_t kTargetWidth = 32;
  const uint32_t kTargetHeight = 32;

  // Set the local constraints for the Image.
  const fuchsia::sysmem::PixelFormatType pixel_format = fuchsia::sysmem::PixelFormatType::BGRA32;
  const fuchsia::sysmem::BufferMemoryConstraints memory_constraints = {
      .secure_required = true,
      .cpu_domain_supported = false,
      .inaccessible_domain_supported = true,
  };
  const fuchsia::sysmem::BufferUsage buffer_usage = {.vulkan =
                                                         fuchsia::sysmem::vulkanUsageTransferSrc};
  auto image_collection = CreateBufferCollectionSyncPtrAndSetConstraints(
      sysmem_allocator_.get(), std::move(image_tokens.local_token),
      /*image_count*/ 1,
      /*width*/ kTargetWidth,
      /*height*/ kTargetHeight, buffer_usage, pixel_format, std::make_optional(memory_constraints));

  // Wait for buffers allocated so it can populate its information struct with the vmo data.
  fuchsia::sysmem::BufferCollectionInfo_2 image_collection_info = {};
  {
    zx_status_t allocation_status = ZX_OK;
    auto status =
        image_collection->WaitForBuffersAllocated(&allocation_status, &image_collection_info);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(allocation_status, ZX_OK);
    EXPECT_EQ(image_collection_info.settings.image_format_constraints.pixel_format.type,
              pixel_format);
    EXPECT_TRUE(image_collection_info.settings.buffer_settings.is_secure);
  }

  // Create the image meta data for the Image and import.
  ImageMetadata image_metadata = {.collection_id = image_collection_id,
                                  .identifier = allocation::GenerateUniqueImageId(),
                                  .vmo_index = 0,
                                  .width = kTargetWidth,
                                  .height = kTargetHeight};
  auto import_res = renderer.ImportBufferImage(image_metadata, BufferCollectionUsage::kClientImage);
  EXPECT_TRUE(import_res);

  // Create a pair of tokens for the render target allocation.
  auto render_target_tokens = flatland::SysmemTokens::Create(sysmem_allocator_.get());

  // Register the render target tokens with the renderer.
  auto render_target_collection_id = allocation::GenerateUniqueBufferCollectionId();
  result = renderer.ImportBufferCollection(render_target_collection_id, sysmem_allocator_.get(),
                                           std::move(render_target_tokens.dup_token),
                                           BufferCollectionUsage::kRenderTarget, std::nullopt);
  EXPECT_TRUE(result);

  // Create a client-side handle to the render target's buffer collection and set the client
  // constraints.
  auto render_target_collection = CreateBufferCollectionSyncPtrAndSetConstraints(
      sysmem_allocator_.get(), std::move(render_target_tokens.local_token),
      /*image_count*/ 1,
      /*width*/ kTargetWidth,
      /*height*/ kTargetHeight, buffer_usage, fuchsia::sysmem::PixelFormatType::BGRA32,
      std::make_optional(memory_constraints));

  // Wait for buffers allocated so it can populate its information struct with the vmo data.
  fuchsia::sysmem::BufferCollectionInfo_2 render_target_collection_info = {};
  {
    zx_status_t allocation_status = ZX_OK;
    auto status = render_target_collection->WaitForBuffersAllocated(&allocation_status,
                                                                    &render_target_collection_info);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(allocation_status, ZX_OK);
    EXPECT_TRUE(image_collection_info.settings.buffer_settings.is_secure);
  }

  // Create the render_target image metadata and import.
  ImageMetadata render_target_metadata = {.collection_id = render_target_collection_id,
                                          .identifier = allocation::GenerateUniqueImageId(),
                                          .vmo_index = 0,
                                          .width = kTargetWidth,
                                          .height = kTargetHeight};
  import_res =
      renderer.ImportBufferImage(render_target_metadata, BufferCollectionUsage::kRenderTarget);
  EXPECT_TRUE(import_res);

  // Create a renderable where the upper-left hand corner should be at position (0,0) with a
  // width/height of (32,32).
  ImageRect image_renderable(glm::vec2(0, 0), glm::vec2(kTargetWidth, kTargetHeight));
  // Render the renderable to the render target.
  renderer.Render(render_target_metadata, {image_renderable}, {image_metadata});
  renderer.WaitIdle();

  // Note that we cannot read pixel values from either buffer because protected memory does not
  // allow that.
}

// Tests VkRenderer's readback path. This test is enabled on virtual gpu.
VK_TEST_F(VulkanRendererTest, ReadbackTest) {
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher = std::make_unique<escher::Escher>(
      env->GetVulkanDevice(), env->GetFilesystem(), /*gpu_allocator*/ nullptr);
  VkRenderer renderer(unique_escher->GetWeakPtr());

  // Setup the render target collection.
  allocation::GlobalBufferCollectionId target_id = allocation::GenerateUniqueBufferCollectionId();
  auto tokens = flatland::SysmemTokens::Create(sysmem_allocator_.get());
  auto result = renderer.ImportBufferCollection(target_id, sysmem_allocator_.get(),
                                                std::move(tokens.dup_token),
                                                BufferCollectionUsage::kRenderTarget, std::nullopt);
  ASSERT_TRUE(result);
  fuchsia::sysmem::BufferCollectionSyncPtr target_ptr;
  zx_status_t status = sysmem_allocator_->BindSharedCollection(std::move(tokens.local_token),
                                                               target_ptr.NewRequest());
  ASSERT_TRUE(status == ZX_OK);
  status = target_ptr->SetConstraints(false, {});
  {
    fuchsia::sysmem::BufferCollectionInfo_2 client_target_info;
    zx_status_t allocation_status = ZX_OK;
    auto status = target_ptr->WaitForBuffersAllocated(&allocation_status, &client_target_info);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(allocation_status, ZX_OK);
  }
  target_ptr->Close();

  // Setup the readback collection.
  fuchsia::sysmem::BufferCollectionInfo_2 readback_info;
  fuchsia::sysmem::BufferCollectionSyncPtr readback_ptr;
  auto readback_id =
      SetupBufferCollection(1, 60, 40, BufferCollectionUsage::kReadback, &renderer,
                            sysmem_allocator_.get(), &readback_info, readback_ptr, target_id);
  EXPECT_EQ(target_id, readback_id);

  // Create the render_target image metadata and import.
  const uint32_t kTargetWidth = 16;
  const uint32_t kTargetHeight = 8;
  ImageMetadata render_target = {.collection_id = target_id,
                                 .identifier = allocation::GenerateUniqueImageId(),
                                 .vmo_index = 0,
                                 .width = kTargetWidth,
                                 .height = kTargetHeight};
  result = renderer.ImportBufferImage(render_target, BufferCollectionUsage::kRenderTarget);
  ASSERT_TRUE(result);
  result = renderer.ImportBufferImage(render_target, BufferCollectionUsage::kReadback);
  ASSERT_TRUE(result);

  // Create the image metadata for the solid color renderable.
  ImageMetadata renderable_image_data = {
      .identifier = allocation::kInvalidImageId,
      .multiply_color = {1, 0.4, 0, 1},
      .blend_mode = fuchsia::ui::composition::BlendMode::SRC_OVER};
  ImageRect renderable(glm::vec2(0, 0), glm::vec2(kTargetWidth, kTargetHeight));

  // Render the renderable to the render target.
  renderer.Render(render_target, {renderable}, {renderable_image_data});
  renderer.WaitIdle();

  // Get a raw pointer from the readback collection's vmo that represents the copied render target
  // and read its values.
  MapHostPointer(readback_info, 0, [&](uint8_t* vmo_host, uint32_t num_bytes) mutable {
    // Flush the cache before reading back target image.
    EXPECT_EQ(ZX_OK, zx_cache_flush(vmo_host, kTargetWidth * kTargetHeight * 4,
                                    ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));

    uint8_t linear_vals[num_bytes];
    sRGBtoLinear(vmo_host, linear_vals, num_bytes);

    // Make sure the pixels are in the right order give that we rotated
    // the rectangle. Values are BGRA.
    for (uint32_t i = 0; i < kTargetWidth; i++) {
      for (uint32_t j = 0; j < kTargetHeight; j++) {
        auto pixel = GetPixel(linear_vals, kTargetWidth, i, j);
        // The sRGB conversion function provides slightly different results depending
        // on the platform.
        EXPECT_TRUE(pixel == glm::ivec4(0, 101, 255, 255) || pixel == glm::ivec4(0, 102, 255, 255));
      }
    }
  });
}

}  // namespace flatland
