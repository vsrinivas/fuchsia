// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testing/test_loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>

#include <thread>

#include "src/ui/scenic/lib/flatland/renderer/null_renderer.h"
#include "src/ui/scenic/lib/flatland/renderer/tests/common.h"
#include "src/ui/scenic/lib/flatland/renderer/vk_renderer.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtx/matrix_transform_2d.hpp>

namespace flatland {

using NullRendererTest = RendererTest;
using VulkanRendererTest = RendererTest;

namespace {
static constexpr float kDegreesToRadians = glm::pi<float>() / 180.f;

void GetVmoHostPtr(uint8_t** vmo_host,
                   const fuchsia::sysmem::BufferCollectionInfo_2& collection_info, uint32_t idx) {
  const zx::vmo& image_vmo = collection_info.buffers[idx].vmo;
  auto image_vmo_bytes = collection_info.settings.buffer_settings.size_bytes;
  ASSERT_TRUE(image_vmo_bytes > 0);
  auto status = zx::vmar::root_self()->map(0, image_vmo, 0, image_vmo_bytes,
                                           ZX_VM_PERM_WRITE | ZX_VM_PERM_READ,
                                           reinterpret_cast<uintptr_t*>(vmo_host));
  EXPECT_EQ(status, ZX_OK);
}

glm::ivec4 GetPixel(uint8_t* vmo_host, uint32_t width, uint32_t x, uint32_t y) {
  uint32_t r = vmo_host[y * width * 4 + x * 4];
  uint32_t g = vmo_host[y * width * 4 + x * 4 + 1];
  uint32_t b = vmo_host[y * width * 4 + x * 4 + 2];
  uint32_t a = vmo_host[y * width * 4 + x * 4 + 3];
  return glm::ivec4(r, g, b, a);
};

}  // anonymous namespace

// Make sure a valid token can be used to register a buffer collection. Make
// sure also that multiple calls to register buffer collection return
// different values for the GlobalBufferCollectionId.
void RegisterCollectionTest(Renderer* renderer, fuchsia::sysmem::Allocator_Sync* sysmem_allocator) {
  auto tokens = CreateSysmemTokens(sysmem_allocator);
  auto tokens2 = CreateSysmemTokens(sysmem_allocator);

  // First id should be valid.
  auto bcid = renderer->RegisterTextureCollection(sysmem_allocator, std::move(tokens.local_token));
  EXPECT_NE(bcid, Renderer::kInvalidId);

  // Second id should be valid.
  auto bcid2 =
      renderer->RegisterTextureCollection(sysmem_allocator, std::move(tokens2.local_token));
  EXPECT_NE(bcid2, Renderer::kInvalidId);

  // Ids should not equal eachother.
  EXPECT_NE(bcid, bcid2);
}

// Multiple clients may need to reference the same buffer collection in the renderer
// (for example if they both need access to a global camera feed). In this case, both
// clients will be passing their own duped tokens to the same collection to the renderer,
// and will each get back a different ID. The collection itself (which is just a pointer)
// will be in the renderer's map twice. So if all tokens are set, both server-side
// registered collections should be allocated (since they are just pointers that refer
// to the same collection).
void SameTokenTwiceTest(Renderer* renderer, fuchsia::sysmem::Allocator_Sync* sysmem_allocator) {
  auto tokens = flatland::CreateSysmemTokens(sysmem_allocator);

  // Create a client token to represent a single client.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr client_token;
  auto status = tokens.local_token->Duplicate(std::numeric_limits<uint32_t>::max(),
                                              client_token.NewRequest());
  EXPECT_EQ(status, ZX_OK);

  // First id should be valid.
  auto bcid = renderer->RegisterTextureCollection(sysmem_allocator, std::move(tokens.local_token));
  EXPECT_NE(bcid, Renderer::kInvalidId);

  // Second id should be valid.
  auto bcid2 = renderer->RegisterTextureCollection(sysmem_allocator, std::move(tokens.dup_token));
  EXPECT_NE(bcid2, Renderer::kInvalidId);

  // Ids should not equal eachother.
  EXPECT_NE(bcid, bcid2);

  // Set the client constraints.
  SetClientConstraintsAndWaitForAllocated(sysmem_allocator, std::move(client_token));

  // Now check that both server ids are allocated.
  auto result_1 = renderer->Validate(bcid);
  auto result_2 = renderer->Validate(bcid2);

  EXPECT_TRUE(result_1.has_value());
  EXPECT_TRUE(result_2.has_value());

  // There should be 1 vmo each.
  EXPECT_EQ(result_1->vmo_count, 1U);
  EXPECT_EQ(result_2->vmo_count, 1U);
}

// Make sure a bad token returns Renderer::kInvalidId. A "bad token" here can
// either be a null token, or a token that's a valid channel but just not a
// valid buffer collection token.
void BadTokenTest(Renderer* renderer, fuchsia::sysmem::Allocator_Sync* sysmem_allocator) {
  // Null token should fail.
  auto bcid = renderer->RegisterTextureCollection(sysmem_allocator, nullptr);
  EXPECT_EQ(bcid, Renderer::kInvalidId);

  // A valid channel that isn't a buffer collection should also fail.
  zx::channel local_endpoint;
  zx::channel remote_endpoint;
  zx::channel::create(0, &local_endpoint, &remote_endpoint);
  flatland::BufferCollectionHandle handle{std::move(remote_endpoint)};
  ASSERT_TRUE(handle.is_valid());
  bcid = renderer->RegisterTextureCollection(sysmem_allocator, std::move(handle));
  EXPECT_EQ(bcid, Renderer::kInvalidId);
}

// Test the Validate() function. First call Validate() without setting the client
// constraints, which should return std::nullopt, and then set the client constraints which
// should cause Validate() to return a valid BufferCollectionMetadata struct.
void ValidationTest(Renderer* renderer, fuchsia::sysmem::Allocator_Sync* sysmem_allocator) {
  auto tokens = CreateSysmemTokens(sysmem_allocator);

  auto bcid = renderer->RegisterTextureCollection(sysmem_allocator, std::move(tokens.dup_token));
  EXPECT_NE(bcid, Renderer::kInvalidId);

  // The buffer collection should not be valid here.
  EXPECT_FALSE(renderer->Validate(bcid).has_value());

  SetClientConstraintsAndWaitForAllocated(sysmem_allocator, std::move(tokens.local_token));

  // The buffer collection *should* be valid here.
  auto result = renderer->Validate(bcid);
  EXPECT_TRUE(result.has_value());

  // There should be 1 vmo.
  EXPECT_EQ(result->vmo_count, 1U);
}

// Simple deregistration test that calls DeregisterCollection() directly without
// any zx::events just to make sure that the method's functionality itself is
// working as intented.
void DeregistrationTest(Renderer* renderer, fuchsia::sysmem::Allocator_Sync* sysmem_allocator) {
  auto tokens = CreateSysmemTokens(sysmem_allocator);

  auto bcid = renderer->RegisterTextureCollection(sysmem_allocator, std::move(tokens.dup_token));
  EXPECT_NE(bcid, Renderer::kInvalidId);

  // The buffer collection should not be valid here.
  EXPECT_FALSE(renderer->Validate(bcid).has_value());

  SetClientConstraintsAndWaitForAllocated(sysmem_allocator, std::move(tokens.local_token));

  // The buffer collection *should* be valid here.
  auto result = renderer->Validate(bcid);
  EXPECT_TRUE(result.has_value());

  // There should be 1 vmo.
  EXPECT_EQ(result->vmo_count, 1U);

  // Now deregister the collection.
  renderer->DeregisterCollection(bcid);

  // After deregistration, calling validate should return false.
  EXPECT_FALSE(renderer->Validate(bcid));
}

// Test to make sure we can call the functions RegisterTextureCollection(),
// RegisterRenderTargetCollection() and Validate() simultaneously from
// multiple threads and have it work.
void MultithreadingTest(Renderer* renderer) {
  const uint32_t kNumThreads = 100;

  auto register_and_validate_function = [&renderer](bool register_texture) {
    // Make a test loop.
    async::TestLoop loop;

    // Make an extra sysmem allocator for tokens.
    fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator;
    zx_status_t status = fdio_service_connect(
        "/svc/fuchsia.sysmem.Allocator", sysmem_allocator.NewRequest().TakeChannel().release());

    auto tokens = CreateSysmemTokens(sysmem_allocator.get());
    GlobalBufferCollectionId bcid = Renderer::kInvalidId;
    if (register_texture) {
      bcid = renderer->RegisterTextureCollection(sysmem_allocator.get(),
                                                 std::move(tokens.local_token));
    } else {
      bcid = renderer->RegisterRenderTargetCollection(sysmem_allocator.get(),
                                                      std::move(tokens.local_token));
    }

    EXPECT_NE(bcid, Renderer::kInvalidId);

    SetClientConstraintsAndWaitForAllocated(sysmem_allocator.get(), std::move(tokens.dup_token));

    // The buffer collection *should* be valid here.
    auto result = renderer->Validate(bcid);
    EXPECT_TRUE(result.has_value());

    // There should be 1 vmo.
    EXPECT_EQ(result->vmo_count, 1U);

    loop.RunUntilIdle();
  };

  // Run a bunch of threads, alternating between threads that register texture collections
  // and threads that register render target collections.
  std::vector<std::thread> threads;
  for (uint32_t i = 0; i < kNumThreads; i++) {
    threads.push_back(std::thread(register_and_validate_function, static_cast<bool>(i % 2)));
  }

  for (auto&& thread : threads) {
    thread.join();
  }

  // Validate the ids here one more time to make sure the renderer's internal
  // state hasn't been corrupted. The ids are generated incrementally so we
  // know that we should have id values in the range [1, kNumThreads].
  for (GlobalBufferCollectionId i = 1; i <= kNumThreads; i++) {
    // The buffer collection *should* be valid here.
    auto result = renderer->Validate(i);
    EXPECT_TRUE(result.has_value());

    // There should be 1 vmo.
    EXPECT_EQ(result->vmo_count, 1U);
  }

  // Check that if we give a non-valid bcid, that the result is null.
  auto result = renderer->Validate(kNumThreads + 1);
  EXPECT_FALSE(result.has_value());
}

// This test checks to make sure that the Render() function properly signals
// a zx::event which can be used by an async::Wait object to asynchronously
// call a custom function.
void AsyncEventSignalTest(Renderer* renderer,
                          fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
                          bool use_vulkan) {
  // First create a pairs of sysmem tokens for the render target.
  auto target_tokens = CreateSysmemTokens(sysmem_allocator);

  // Register the render target with the renderer.
  GlobalBufferCollectionId target_id;
  fuchsia::sysmem::BufferCollectionInfo_2 target_info = {};
  target_id = renderer->RegisterRenderTargetCollection(sysmem_allocator,
                                                       std::move(target_tokens.dup_token));

  // Create a client-side handle to the buffer collection and set the client constraints.
  auto client_target_collection =
      CreateClientPointerWithConstraints(sysmem_allocator, std::move(target_tokens.local_token),
                                         /*image_count*/ 1,
                                         /*width*/ 60,
                                         /*height*/ 40);
  auto allocation_status = ZX_OK;
  auto status = client_target_collection->WaitForBuffersAllocated(&allocation_status, &target_info);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(allocation_status, ZX_OK);

  // Now that the renderer and client have set their contraints, we validate.
  auto target_metadata = renderer->Validate(target_id);
  EXPECT_TRUE(target_metadata.has_value());

  // Create the render_target image meta_data.
  ImageMetadata render_target = {
      .collection_id = target_id,
      .vmo_idx = 0,
      .width = 16,
      .height = 8,
  };

  // Create the release fence that will be passed along to the Render()
  // function and be used to signal when we should deregister the collection.
  zx::event release_fence;
  status = zx::event::create(0, &release_fence);
  EXPECT_EQ(status, ZX_OK);

  // Set up the async::Wait object to wait until the release_fence signals
  // ZX_EVENT_SIGNALED. We make use of a test loop to access an async dispatcher.
  async::TestLoop loop;
  bool signaled = false;
  auto dispatcher = loop.dispatcher();
  auto wait = std::make_unique<async::Wait>(release_fence.get(), ZX_EVENT_SIGNALED);
  wait->set_handler([&signaled](async_dispatcher_t*, async::Wait*,
                                zx_status_t /*status*/,
                                const zx_packet_signal_t* /*signal*/) mutable {
    signaled = true;
  });
  wait->Begin(dispatcher);

  // The call to Render() will signal the release fence, triggering the wait object to
  // call DeregisterCollection().
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

TEST_F(NullRendererTest, RegisterCollectionTest) {
  NullRenderer renderer;
  RegisterCollectionTest(&renderer, sysmem_allocator_.get());
}

TEST_F(NullRendererTest, SameTokenTwiceTest) {
  NullRenderer renderer;
  SameTokenTwiceTest(&renderer, sysmem_allocator_.get());
}

TEST_F(NullRendererTest, BadTokenTest) {
  NullRenderer renderer;
  BadTokenTest(&renderer, sysmem_allocator_.get());
}

TEST_F(NullRendererTest, ValidationTest) {
  NullRenderer renderer;
  ValidationTest(&renderer, sysmem_allocator_.get());
}

TEST_F(NullRendererTest, DeregistrationTest) {
  NullRenderer renderer;
  DeregistrationTest(&renderer, sysmem_allocator_.get());
}

TEST_F(NullRendererTest, MultithreadingTest) {
  NullRenderer renderer;
  MultithreadingTest(&renderer);
}

TEST_F(NullRendererTest, AsyncEventSignalTest) {
  NullRenderer renderer;
  AsyncEventSignalTest(&renderer, sysmem_allocator_.get(), /*use_vulkan*/false);
}

VK_TEST_F(VulkanRendererTest, RegisterCollectionTest) {
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher =
      std::make_unique<escher::Escher>(env->GetVulkanDevice(), env->GetFilesystem());
  VkRenderer renderer(std::move(unique_escher));
  RegisterCollectionTest(&renderer, sysmem_allocator_.get());
}

VK_TEST_F(VulkanRendererTest, SameTokenTwiceTest) {
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher =
      std::make_unique<escher::Escher>(env->GetVulkanDevice(), env->GetFilesystem());
  VkRenderer renderer(std::move(unique_escher));
  SameTokenTwiceTest(&renderer, sysmem_allocator_.get());
}

VK_TEST_F(VulkanRendererTest, BadTokenTest) {
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher =
      std::make_unique<escher::Escher>(env->GetVulkanDevice(), env->GetFilesystem());
  VkRenderer renderer(std::move(unique_escher));
  BadTokenTest(&renderer, sysmem_allocator_.get());
}

VK_TEST_F(VulkanRendererTest, ValidationTest) {
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher =
      std::make_unique<escher::Escher>(env->GetVulkanDevice(), env->GetFilesystem());
  VkRenderer renderer(std::move(unique_escher));
  ValidationTest(&renderer, sysmem_allocator_.get());
}

VK_TEST_F(VulkanRendererTest, DeregistrationTest) {
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher =
      std::make_unique<escher::Escher>(env->GetVulkanDevice(), env->GetFilesystem());
  VkRenderer renderer(std::move(unique_escher));
  DeregistrationTest(&renderer, sysmem_allocator_.get());
}

VK_TEST_F(VulkanRendererTest, MulithtreadingTest) {
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher =
      std::make_unique<escher::Escher>(env->GetVulkanDevice(), env->GetFilesystem());
  VkRenderer renderer(std::move(unique_escher));
  MultithreadingTest(&renderer);
}

VK_TEST_F(VulkanRendererTest, AsyncEventSignalTest) {
  SKIP_TEST_IF_ESCHER_USES_DEVICE(VirtualGpu);
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher =
      std::make_unique<escher::Escher>(env->GetVulkanDevice(), env->GetFilesystem());
  VkRenderer renderer(std::move(unique_escher));
  AsyncEventSignalTest(&renderer, sysmem_allocator_.get(), /*use_vulkan*/true);
}

// This test actually renders a rectangle using the VKRenderer. We create a single rectangle,
// with a half-red, half-green texture, translate and scale it. The render target is 16x8
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
VK_TEST_F(VulkanRendererTest, RenderTest) {
  SKIP_TEST_IF_ESCHER_USES_DEVICE(VirtualGpu);
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher =
      std::make_unique<escher::Escher>(env->GetVulkanDevice(), env->GetFilesystem());
  VkRenderer renderer(std::move(unique_escher));

  // First create the pair of sysmem tokens, one for the client, one for the renderer.
  auto tokens = flatland::CreateSysmemTokens(sysmem_allocator_.get());

  auto target_tokens = flatland::CreateSysmemTokens(sysmem_allocator_.get());

  // Register and validate the collection with the renderer.
  auto collection_id =
      renderer.RegisterTextureCollection(sysmem_allocator_.get(), std::move(tokens.dup_token));

  // Create a client-side handle to the buffer collection and set the client constraints.
  auto [buffer_usage, memory_constraints] = GetUsageAndMemoryConstraintsForCpuWriteOften();
  auto client_collection = CreateClientPointerWithConstraints(
      sysmem_allocator_.get(), std::move(tokens.local_token),
      /*image_count*/ 1,
      /*width*/ 60,
      /*height*/ 40, buffer_usage, std::make_optional(memory_constraints));

  auto target_id = renderer.RegisterRenderTargetCollection(sysmem_allocator_.get(),
                                                           std::move(target_tokens.dup_token));

  // Create a client-side handle to the buffer collection and set the client constraints.
  auto client_target = CreateClientPointerWithConstraints(
      sysmem_allocator_.get(), std::move(target_tokens.local_token),
      /*image_count*/ 1,
      /*width*/ 60,
      /*height*/ 40, buffer_usage, std::make_optional(memory_constraints));

  // Have the client wait for buffers allocated so it can populate its information
  // struct with the vmo data.
  fuchsia::sysmem::BufferCollectionInfo_2 client_collection_info = {};
  {
    zx_status_t allocation_status = ZX_OK;
    auto status =
        client_collection->WaitForBuffersAllocated(&allocation_status, &client_collection_info);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(allocation_status, ZX_OK);
  }

  // Have the client wait for buffers allocated so it can populate its information
  // struct with the vmo data.
  fuchsia::sysmem::BufferCollectionInfo_2 client_target_info = {};
  {
    zx_status_t allocation_status = ZX_OK;
    auto status = client_target->WaitForBuffersAllocated(&allocation_status, &client_target_info);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(allocation_status, ZX_OK);
  }

  // Now that the renderer and client have set their contraints, we validate.
  auto buffer_metadata = renderer.Validate(collection_id);
  EXPECT_TRUE(buffer_metadata.has_value());

  auto target_metadata = renderer.Validate(target_id);
  EXPECT_TRUE(buffer_metadata.has_value());

  const uint32_t kTargetWidth = 16;
  const uint32_t kTargetHeight = 8;

  // Create the render_target image meta_data.
  ImageMetadata render_target = {
      .collection_id = target_id,
      .vmo_idx = 0,
      .width = kTargetWidth,
      .height = kTargetHeight,
  };

  // Create the image meta data for the renderable.
  ImageMetadata renderable_texture = {
      .collection_id = collection_id, .vmo_idx = 0, .width = 2, .height = 1};

  // Create a renderable where the upper-left hand corner should be at position (6,3)
  // with a width/height of (4,2).
  const uint32_t kRenderableWidth = 4;
  const uint32_t kRenderableHeight = 2;
  Rectangle2D renderable(glm::vec2(6, 3), glm::vec2(kRenderableWidth, kRenderableHeight));

  // Have the client write pixel values to the renderable's texture.
  uint8_t* vmo_host;
  {
    GetVmoHostPtr(&vmo_host, client_collection_info, renderable_texture.vmo_idx);
    // The texture only has 2 pixels, so it needs 8 write values for 4 channels. We
    // set the first pixel to red and the second pixel to green.
    const uint8_t kNumWrites = 8;
    const uint8_t kWriteValues[] = {/*red*/ 255U, 0, 0, 255U, /*green*/ 0, 255U, 0, 255U};
    memcpy(vmo_host, kWriteValues, sizeof(kWriteValues));

    // Flush the cache after writing to host VMO.
    EXPECT_EQ(ZX_OK, zx_cache_flush(vmo_host, kNumWrites,
                                    ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));
  }

  // Render the renderable to the render target.
  renderer.Render(render_target, {renderable}, {renderable_texture});
  renderer.WaitIdle();

  // Get a raw pointer from the client collection's vmo that represents the render target
  // and read its values. This should show that the renderable was rendered to the center
  // of the render target, with its associated texture.
  GetVmoHostPtr(&vmo_host, client_target_info, render_target.vmo_idx);

  // Flush the cache before reading back target image.
  EXPECT_EQ(ZX_OK, zx_cache_flush(vmo_host, kTargetWidth * kTargetHeight * 4,
                                  ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));

  // Make sure the pixels are in the right order give that we rotated
  // the rectangle.
  EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 6, 3), glm::ivec4(255, 0, 0, 255));
  EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 7, 3), glm::ivec4(255, 0, 0, 255));
  EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 8, 3), glm::ivec4(0, 255, 0, 255));
  EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 9, 3), glm::ivec4(0, 255, 0, 255));
  EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 6, 4), glm::ivec4(255, 0, 0, 255));
  EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 7, 4), glm::ivec4(255, 0, 0, 255));
  EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 8, 4), glm::ivec4(0, 255, 0, 255));
  EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 9, 4), glm::ivec4(0, 255, 0, 255));

  // Make sure the remaining pixels are black.
  uint32_t black_pixels = 0;
  for (uint32_t y = 0; y < kTargetHeight; y++) {
    for (uint32_t x = 0; x < kTargetWidth; x++) {
      auto col = GetPixel(vmo_host, kTargetWidth, x, y);
      if (col == glm::ivec4(0, 0, 0, 0))
        black_pixels++;
    }
  }
  EXPECT_EQ(black_pixels, kTargetWidth * kTargetHeight - kRenderableWidth * kRenderableHeight);
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
// TODO(fxbug.dev/52632): Transparency is currently hardcoded in the renderer to be on. This test will
// break if that is changed to be hardcoded to false before we expose it in the API.
VK_TEST_F(VulkanRendererTest, TransparencyTest) {
  SKIP_TEST_IF_ESCHER_USES_DEVICE(VirtualGpu);
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher =
      std::make_unique<escher::Escher>(env->GetVulkanDevice(), env->GetFilesystem());
  VkRenderer renderer(std::move(unique_escher));

  // First create the pair of sysmem tokens, one for the client, one for the renderer.
  auto tokens = flatland::CreateSysmemTokens(sysmem_allocator_.get());

  auto target_tokens = flatland::CreateSysmemTokens(sysmem_allocator_.get());

  // Register and validate the collection with the renderer.
  auto collection_id =
      renderer.RegisterTextureCollection(sysmem_allocator_.get(), std::move(tokens.dup_token));

  // Create a client-side handle to the buffer collection and set the client constraints.
  auto [buffer_usage, memory_constraints] = GetUsageAndMemoryConstraintsForCpuWriteOften();
  auto client_collection = CreateClientPointerWithConstraints(
      sysmem_allocator_.get(), std::move(tokens.local_token),
      /*image_count*/ 2,
      /*width*/ 60,
      /*height*/ 40, buffer_usage, std::make_optional(memory_constraints));

  auto target_id = renderer.RegisterRenderTargetCollection(sysmem_allocator_.get(),
                                                           std::move(target_tokens.dup_token));

  // Create a client-side handle to the buffer collection and set the client constraints.
  auto client_target = CreateClientPointerWithConstraints(
      sysmem_allocator_.get(), std::move(target_tokens.local_token),
      /*image_count*/ 1,
      /*width*/ 60,
      /*height*/ 40, buffer_usage, std::make_optional(memory_constraints));

  // Have the client wait for buffers allocated so it can populate its information
  // struct with the vmo data.
  fuchsia::sysmem::BufferCollectionInfo_2 client_collection_info = {};
  {
    zx_status_t allocation_status = ZX_OK;
    auto status =
        client_collection->WaitForBuffersAllocated(&allocation_status, &client_collection_info);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(allocation_status, ZX_OK);
  }

  fuchsia::sysmem::BufferCollectionInfo_2 client_target_info = {};
  {
    zx_status_t allocation_status = ZX_OK;
    auto status = client_target->WaitForBuffersAllocated(&allocation_status, &client_target_info);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(allocation_status, ZX_OK);
  }

  // Now that the renderer and client have set their contraints, we validate.
  auto buffer_metadata = renderer.Validate(collection_id);
  EXPECT_TRUE(buffer_metadata.has_value());

  auto target_metadata = renderer.Validate(target_id);
  EXPECT_TRUE(buffer_metadata.has_value());

  const uint32_t kTargetWidth = 16;
  const uint32_t kTargetHeight = 8;

  // Create the render_target image meta_data.
  ImageMetadata render_target = {
      .collection_id = target_id,
      .vmo_idx = 0,
      .width = kTargetWidth,
      .height = kTargetHeight,
  };

  // Create the image meta data for the renderable.
  ImageMetadata renderable_texture = {
      .collection_id = collection_id, .vmo_idx = 0, .width = 1, .height = 1};

  // Create the texture that will go on the transparent renderable.
  ImageMetadata transparent_texture = {
      .collection_id = collection_id, .vmo_idx = 1, .width = 1, .height = 1};

  // Create the two renderables.
  const uint32_t kRenderableWidth = 4;
  const uint32_t kRenderableHeight = 2;
  Rectangle2D renderable(glm::vec2(6, 3), glm::vec2(kRenderableWidth, kRenderableHeight));
  Rectangle2D transparent_renderable(glm::vec2(7, 3),
                                     glm::vec2(kRenderableWidth, kRenderableHeight));

  // Have the client write pixel values to the renderable's texture.
  uint8_t* vmo_host;
  {
    GetVmoHostPtr(&vmo_host, client_collection_info, renderable_texture.vmo_idx);
    // Create a red opaque pixel.
    const uint8_t kNumWrites = 4;
    const uint8_t kWriteValues[] = {/*red*/ 255U, 0, 0, 255U};
    memcpy(vmo_host, kWriteValues, sizeof(kWriteValues));

    // Flush the cache after writing to host VMO.
    EXPECT_EQ(ZX_OK, zx_cache_flush(vmo_host, kNumWrites,
                                    ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));
  }

  {
    GetVmoHostPtr(&vmo_host, client_collection_info, transparent_texture.vmo_idx);
    // Create a green pixel with an alpha of 0.5.
    const uint8_t kNumWrites = 4;
    const uint8_t kWriteValues[] = {/*red*/ 0, 255, 0, 128U};
    memcpy(vmo_host, kWriteValues, sizeof(kWriteValues));

    // Flush the cache after writing to host VMO.
    EXPECT_EQ(ZX_OK, zx_cache_flush(vmo_host, kNumWrites,
                                    ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));
  }

  // Render the renderable to the render target.
  renderer.Render(render_target, {renderable, transparent_renderable},
                  {renderable_texture, transparent_texture});
  renderer.WaitIdle();

  // Get a raw pointer from the client collection's vmo that represents the render target
  // and read its values. This should show that the renderable was rendered to the center
  // of the render target, with its associated texture.
  GetVmoHostPtr(&vmo_host, client_target_info, render_target.vmo_idx);

  // Flush the cache before reading back target image.
  EXPECT_EQ(ZX_OK, zx_cache_flush(vmo_host, kTargetWidth * kTargetHeight * 4,
                                  ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));

  // Make sure the pixels are in the right order give that we rotated
  // the rectangle.
  EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 6, 3), glm::ivec4(255, 0, 0, 255));
  EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 6, 4), glm::ivec4(255, 0, 0, 255));
  EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 7, 3), glm::ivec4(127, 255, 0, 255));
  EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 7, 4), glm::ivec4(127, 255, 0, 255));
  EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 8, 3), glm::ivec4(127, 255, 0, 255));
  EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 8, 4), glm::ivec4(127, 255, 0, 255));
  EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 9, 3), glm::ivec4(127, 255, 0, 255));
  EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 9, 4), glm::ivec4(127, 255, 0, 255));
  EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 10, 3), glm::ivec4(0, 255, 0, 128));
  EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 10, 4), glm::ivec4(0, 255, 0, 128));

  // Make sure the remaining pixels are black.
  uint32_t black_pixels = 0;
  for (uint32_t y = 0; y < kTargetHeight; y++) {
    for (uint32_t x = 0; x < kTargetWidth; x++) {
      auto col = GetPixel(vmo_host, kTargetWidth, x, y);
      if (col == glm::ivec4(0, 0, 0, 0))
        black_pixels++;
    }
  }
  EXPECT_EQ(black_pixels, kTargetWidth * kTargetHeight - 10U);
}

}  // namespace flatland
