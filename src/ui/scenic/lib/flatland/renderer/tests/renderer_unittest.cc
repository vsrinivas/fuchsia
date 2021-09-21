// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testing/test_loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>

#include <cstdint>
#include <thread>

#include "src/lib/fsl/handles/object_info.h"
#include "src/ui/scenic/lib/flatland/buffers/util.h"
#include "src/ui/scenic/lib/flatland/renderer/null_renderer.h"
#include "src/ui/scenic/lib/flatland/renderer/tests/common.h"
#include "src/ui/scenic/lib/flatland/renderer/vk_renderer.h"
#include "src/ui/scenic/lib/utils/helpers.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtx/matrix_transform_2d.hpp>

namespace flatland {

using allocation::ImageMetadata;

// TODO(fxbug.dev/52632): Move common functions to testing::WithParamInterface instead of function
// calls.
using NullRendererTest = RendererTest;
using VulkanRendererTest = RendererTest;

namespace {
static constexpr float kDegreesToRadians = glm::pi<float>() / 180.f;

glm::ivec4 GetPixel(uint8_t* vmo_host, uint32_t width, uint32_t x, uint32_t y) {
  uint32_t r = vmo_host[y * width * 4 + x * 4];
  uint32_t g = vmo_host[y * width * 4 + x * 4 + 1];
  uint32_t b = vmo_host[y * width * 4 + x * 4 + 2];
  uint32_t a = vmo_host[y * width * 4 + x * 4 + 3];
  return glm::ivec4(r, g, b, a);
};

}  // anonymous namespace

// Make sure a valid token can be used to register a buffer collection.
void RegisterCollectionTest(Renderer* renderer, fuchsia::sysmem::Allocator_Sync* sysmem_allocator) {
  auto tokens = SysmemTokens::Create(sysmem_allocator);

  // First id should be valid.
  auto bcid = allocation::GenerateUniqueBufferCollectionId();
  auto result = renderer->RegisterRenderTargetCollection(bcid, sysmem_allocator,
                                                         std::move(tokens.local_token));
  EXPECT_TRUE(result);
}

// Multiple clients may need to reference the same buffer collection in the renderer
// (for example if they both need access to a global camera feed). In this case, both
// clients will be passing their own duped tokens to the same collection to the renderer,
// and will each get back a different ID. The collection itself (which is just a pointer)
// will be in the renderer's map twice. So if all tokens are set, both server-side
// registered collections should be allocated (since they are just pointers that refer
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
  auto result = renderer->RegisterRenderTargetCollection(bcid, sysmem_allocator,
                                                         std::move(tokens.local_token));
  EXPECT_TRUE(result);

  // Second id should be valid.
  auto bcid2 = allocation::GenerateUniqueBufferCollectionId();
  result = renderer->RegisterRenderTargetCollection(bcid2, sysmem_allocator,
                                                    std::move(tokens.dup_token));
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
                                            .height = 1});
  bool res_2 = renderer->ImportBufferImage({.collection_id = bcid2,
                                            .identifier = allocation::GenerateUniqueImageId(),
                                            .vmo_index = 0,
                                            .width = 1,
                                            .height = 1});
  EXPECT_TRUE(res_1);
  EXPECT_TRUE(res_2);
}

// Make sure a bad token returns Renderer::allocation::kInvalidId. A "bad token" here can
// either be a null token, or a token that's a valid channel but just not a
// valid buffer collection token.
void BadTokenTest(Renderer* renderer, fuchsia::sysmem::Allocator_Sync* sysmem_allocator) {
  // Null token should fail.
  auto bcid = allocation::GenerateUniqueBufferCollectionId();
  auto result = renderer->RegisterRenderTargetCollection(bcid, sysmem_allocator, nullptr);
  EXPECT_FALSE(result);

  // A valid channel that isn't a buffer collection should also fail.
  zx::channel local_endpoint;
  zx::channel remote_endpoint;
  zx::channel::create(0, &local_endpoint, &remote_endpoint);
  flatland::BufferCollectionHandle handle{std::move(remote_endpoint)};
  ASSERT_TRUE(handle.is_valid());

  bcid = allocation::GenerateUniqueBufferCollectionId();
  result = renderer->RegisterRenderTargetCollection(bcid, sysmem_allocator, std::move(handle));
  EXPECT_FALSE(result);
}

void BadImageInputTest(Renderer* renderer, fuchsia::sysmem::Allocator_Sync* sysmem_allocator) {
  const uint32_t kNumImages = 1;
  auto tokens = SysmemTokens::Create(sysmem_allocator);

  auto bcid = allocation::GenerateUniqueBufferCollectionId();
  auto result =
      renderer->RegisterRenderTargetCollection(bcid, sysmem_allocator, std::move(tokens.dup_token));
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
                                            .height = 1}));

  // Using an invalid image identifier.
  EXPECT_FALSE(renderer->ImportBufferImage({.collection_id = bcid,
                                            .identifier = allocation::kInvalidImageId,
                                            .vmo_index = kNumImages,
                                            .width = 1,
                                            .height = 1}));

  // VMO index is out of bounds.
  EXPECT_FALSE(renderer->ImportBufferImage({.collection_id = bcid,
                                            .identifier = image_id,
                                            .vmo_index = kNumImages,
                                            .width = 1,
                                            .height = 1}));
}

// Test the ImportBufferImage() function. First call ImportBufferImage() without setting the client
// constraints, which should return false, and then set the client constraints which
// should cause it to return true.
void ImportImageTest(Renderer* renderer, fuchsia::sysmem::Allocator_Sync* sysmem_allocator) {
  auto tokens = SysmemTokens::Create(sysmem_allocator);

  auto bcid = allocation::GenerateUniqueBufferCollectionId();
  auto result =
      renderer->RegisterRenderTargetCollection(bcid, sysmem_allocator, std::move(tokens.dup_token));
  EXPECT_TRUE(result);

  // The buffer collection should not be valid here.
  auto image_id = allocation::GenerateUniqueImageId();
  EXPECT_FALSE(renderer->ImportBufferImage(
      {.collection_id = bcid, .identifier = image_id, .vmo_index = 0, .width = 1, .height = 1}));

  std::vector<uint64_t> additional_format_modifiers;
  if (escher::VulkanIsSupported() && escher::test::GlobalEscherUsesVirtualGpu()) {
    additional_format_modifiers.push_back(fuchsia::sysmem::FORMAT_MODIFIER_GOOGLE_GOLDFISH_OPTIMAL);
  }
  SetClientConstraintsAndWaitForAllocated(sysmem_allocator, std::move(tokens.local_token),
                                          /* image_count */ 1, /* width */ 64, /* height */ 32,
                                          kNoneUsage, additional_format_modifiers);

  // The buffer collection *should* be valid here.
  auto res = renderer->ImportBufferImage(
      {.collection_id = bcid, .identifier = image_id, .vmo_index = 0, .width = 1, .height = 1});
  EXPECT_TRUE(res);
}

// Simple deregistration test that calls ReleaseBufferCollection() directly without
// any zx::events just to make sure that the method's functionality itself is
// working as intented.
void DeregistrationTest(Renderer* renderer, fuchsia::sysmem::Allocator_Sync* sysmem_allocator) {
  auto tokens = SysmemTokens::Create(sysmem_allocator);

  auto bcid = allocation::GenerateUniqueBufferCollectionId();
  auto result =
      renderer->RegisterRenderTargetCollection(bcid, sysmem_allocator, std::move(tokens.dup_token));
  EXPECT_TRUE(result);

  // The buffer collection should not be valid here.
  auto image_id = allocation::GenerateUniqueImageId();
  EXPECT_FALSE(renderer->ImportBufferImage(
      {.collection_id = bcid, .identifier = image_id, .vmo_index = 0, .width = 1, .height = 1}));

  std::vector<uint64_t> additional_format_modifiers;
  if (escher::VulkanIsSupported() && escher::test::GlobalEscherUsesVirtualGpu()) {
    additional_format_modifiers.push_back(fuchsia::sysmem::FORMAT_MODIFIER_GOOGLE_GOLDFISH_OPTIMAL);
  }
  SetClientConstraintsAndWaitForAllocated(sysmem_allocator, std::move(tokens.local_token),
                                          /* image_count */ 1, /* width */ 64, /* height */ 32,
                                          kNoneUsage, additional_format_modifiers);

  // The buffer collection *should* be valid here.
  auto import_result = renderer->ImportBufferImage(
      {.collection_id = bcid, .identifier = image_id, .vmo_index = 0, .width = 1, .height = 1});
  EXPECT_TRUE(import_result);

  // Now deregister the collection.
  renderer->DeregisterRenderTargetCollection(bcid);

  // After deregistration, calling ImportBufferImage() should return false.
  import_result = renderer->ImportBufferImage(
      {.collection_id = bcid, .identifier = image_id, .vmo_index = 0, .width = 1, .height = 1});
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
                                                 std::move(texture_tokens.dup_token));
  EXPECT_TRUE(result);

  result = renderer->RegisterRenderTargetCollection(target_collection_id, sysmem_allocator,
                                                    std::move(target_tokens.dup_token));
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
  auto import_result = renderer->ImportBufferImage(render_target);
  EXPECT_TRUE(import_result);

  // Import image.
  ImageMetadata image = {.collection_id = texture_collection_id,
                         .identifier = allocation::GenerateUniqueImageId(),
                         .vmo_index = 0,
                         .width = kWidth,
                         .height = kHeight};
  import_result = renderer->ImportBufferImage(image);
  EXPECT_TRUE(import_result);

  // Now deregister the collection.
  renderer->DeregisterRenderTargetCollection(texture_collection_id);
  renderer->DeregisterRenderTargetCollection(target_collection_id);

  // We should still be able to render this image.
  renderer->Render(render_target, {Rectangle2D(glm::vec2(0, 0), glm::vec2(kWidth, kHeight))},
                   {image});
  if (use_vulkan) {
    auto vk_renderer = static_cast<VkRenderer*>(renderer);
    vk_renderer->WaitIdle();
  }
}

// Test to make sure we can call the functions RegisterTextureCollection(),
// RegisterRenderTargetCollection() and ImportBufferImage() simultaneously from
// multiple threads and have it work.
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
    bool result = renderer->RegisterRenderTargetCollection(bcid, sysmem_allocator.get(),
                                                           std::move(tokens.local_token));
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
        {.collection_id = bcid, .identifier = image_id, .vmo_index = 0, .width = 1, .height = 1});
    EXPECT_TRUE(import_result);
    loop.RunUntilIdle();
  };

  // Run a bunch of threads, alternating between threads that register texture collections
  // and threads that register render target collections.
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
                                               .height = 1});
    EXPECT_TRUE(result);
  }
}

// This test checks to make sure that the Render() function properly signals
// a zx::event which can be used by an async::Wait object to asynchronously
// call a custom function.
void AsyncEventSignalTest(Renderer* renderer, fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
                          bool use_vulkan) {
  // First create a pairs of sysmem tokens for the render target.
  auto target_tokens = SysmemTokens::Create(sysmem_allocator);

  // Register the render target with the renderer.
  fuchsia::sysmem::BufferCollectionInfo_2 target_info = {};

  auto target_id = allocation::GenerateUniqueBufferCollectionId();

  auto result = renderer->RegisterRenderTargetCollection(target_id, sysmem_allocator,
                                                         std::move(target_tokens.dup_token));
  EXPECT_TRUE(result);

  // Create a client-side handle to the buffer collection and set the client constraints.
  const uint32_t kWidth = 64, kHeight = 32;
  auto client_target_collection = CreateBufferCollectionSyncPtrAndSetConstraints(
      sysmem_allocator, std::move(target_tokens.local_token),
      /*image_count*/ 1, kWidth, kHeight);
  auto allocation_status = ZX_OK;
  auto status = client_target_collection->WaitForBuffersAllocated(&allocation_status, &target_info);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(allocation_status, ZX_OK);

  // Now that the renderer and client have set their contraints, we can import the render target.
  // Create the render_target image metadata.
  ImageMetadata render_target = {.collection_id = target_id,
                                 .identifier = allocation::GenerateUniqueImageId(),
                                 .vmo_index = 0,
                                 .width = kWidth,
                                 .height = kHeight};
  auto target_import = renderer->ImportBufferImage(render_target);
  EXPECT_TRUE(target_import);

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

TEST_F(NullRendererTest, DISABLED_MultithreadingTest) {
  NullRenderer renderer;
  MultithreadingTest(&renderer);
}

TEST_F(NullRendererTest, AsyncEventSignalTest) {
  NullRenderer renderer;
  AsyncEventSignalTest(&renderer, sysmem_allocator_.get(), /*use_vulkan*/ false);
}

VK_TEST_F(VulkanRendererTest, RegisterCollectionTest) {
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher = std::make_unique<escher::Escher>(
      env->GetVulkanDevice(), env->GetFilesystem(), /*gpu_allocator*/ nullptr);
  VkRenderer renderer(unique_escher->GetWeakPtr());
  RegisterCollectionTest(&renderer, sysmem_allocator_.get());
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

  // First create the pair of sysmem tokens, one for the client, one for the renderer.
  auto tokens = flatland::SysmemTokens::Create(sysmem_allocator_.get());

  auto target_tokens = flatland::SysmemTokens::Create(sysmem_allocator_.get());

  // Register the collection with the renderer.
  auto collection_id = allocation::GenerateUniqueBufferCollectionId();

  auto result = renderer.ImportBufferCollection(collection_id, sysmem_allocator_.get(),
                                                std::move(tokens.dup_token));
  EXPECT_TRUE(result);

  // Create a client-side handle to the buffer collection and set the client constraints.
  auto [buffer_usage, memory_constraints] = GetUsageAndMemoryConstraintsForCpuWriteOften();
  auto client_collection = CreateBufferCollectionSyncPtrAndSetConstraints(
      sysmem_allocator_.get(), std::move(tokens.local_token),
      /*image_count*/ 1,
      /*width*/ 60,
      /*height*/ 40, buffer_usage, fuchsia::sysmem::PixelFormatType::BGRA32,
      std::make_optional(memory_constraints));

  auto target_id = allocation::GenerateUniqueBufferCollectionId();

  result = renderer.RegisterRenderTargetCollection(target_id, sysmem_allocator_.get(),
                                                   std::move(target_tokens.dup_token));
  EXPECT_TRUE(result);

  // Create a client-side handle to the buffer collection and set the client constraints.
  auto client_target = CreateBufferCollectionSyncPtrAndSetConstraints(
      sysmem_allocator_.get(), std::move(target_tokens.local_token),
      /*image_count*/ 1,
      /*width*/ 60,
      /*height*/ 40, buffer_usage, fuchsia::sysmem::PixelFormatType::BGRA32,
      std::make_optional(memory_constraints));

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
                                      .width = 2,
                                      .height = 1};

  auto import_res = renderer.ImportBufferImage(render_target);
  EXPECT_TRUE(import_res);

  import_res = renderer.ImportBufferImage(renderable_texture);
  EXPECT_TRUE(import_res);

  // Create a renderable where the upper-left hand corner should be at position (6,3)
  // with a width/height of (4,2).
  const uint32_t kRenderableWidth = 4;
  const uint32_t kRenderableHeight = 2;
  Rectangle2D renderable(glm::vec2(6, 3), glm::vec2(kRenderableWidth, kRenderableHeight));

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
                   uint32_t black_pixels = 0;
                   for (uint32_t y = 0; y < kTargetHeight; y++) {
                     for (uint32_t x = 0; x < kTargetWidth; x++) {
                       auto col = GetPixel(vmo_host, kTargetWidth, x, y);
                       if (col == glm::ivec4(0, 0, 0, 0))
                         black_pixels++;
                     }
                   }
                   EXPECT_EQ(black_pixels,
                             kTargetWidth * kTargetHeight - kRenderableWidth * kRenderableHeight);
                 });

  // Now let's update the uvs of the renderable so only the green portion of the image maps onto
  // the rect.
  auto renderable2 =
      Rectangle2D(glm::vec2(6, 3), glm::vec2(kRenderableWidth, kRenderableHeight),
                  {glm::vec2(0.5, 0), glm::vec2(1.0, 0), glm::vec2(1.0, 1.0), glm::vec2(0.5, 1.0)});

  // Render the renderable to the render target.
  renderer.Render(render_target, {renderable2}, {renderable_texture});
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

                   // All of the renderable's pixels should be green.
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 6, 3), glm::ivec4(0, 255, 0, 255));
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 7, 3), glm::ivec4(0, 255, 0, 255));
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 8, 3), glm::ivec4(0, 255, 0, 255));
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 9, 3), glm::ivec4(0, 255, 0, 255));
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 6, 4), glm::ivec4(0, 255, 0, 255));
                   EXPECT_EQ(GetPixel(vmo_host, kTargetWidth, 7, 4), glm::ivec4(0, 255, 0, 255));
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
                   EXPECT_EQ(black_pixels,
                             kTargetWidth * kTargetHeight - kRenderableWidth * kRenderableHeight);
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

  // First create the pair of sysmem tokens, one for the client, one for the renderer.
  auto tokens = flatland::SysmemTokens::Create(sysmem_allocator_.get());

  auto target_tokens = flatland::SysmemTokens::Create(sysmem_allocator_.get());

  // Register the collection with the renderer.
  auto collection_id = allocation::GenerateUniqueBufferCollectionId();

  auto result = renderer.ImportBufferCollection(collection_id, sysmem_allocator_.get(),
                                                std::move(tokens.dup_token));
  EXPECT_TRUE(result);

  // Create a client-side handle to the buffer collection and set the client constraints.
  auto [buffer_usage, memory_constraints] = GetUsageAndMemoryConstraintsForCpuWriteOften();
  auto client_collection = CreateBufferCollectionSyncPtrAndSetConstraints(
      sysmem_allocator_.get(), std::move(tokens.local_token),
      /*image_count*/ 2,
      /*width*/ 60,
      /*height*/ 40, buffer_usage, fuchsia::sysmem::PixelFormatType::BGRA32,
      std::make_optional(memory_constraints));

  auto target_id = allocation::GenerateUniqueBufferCollectionId();
  result = renderer.RegisterRenderTargetCollection(target_id, sysmem_allocator_.get(),
                                                   std::move(target_tokens.dup_token));
  EXPECT_TRUE(result);

  // Create a client-side handle to the buffer collection and set the client constraints.
  auto client_target = CreateBufferCollectionSyncPtrAndSetConstraints(
      sysmem_allocator_.get(), std::move(target_tokens.local_token),
      /*image_count*/ 1,
      /*width*/ 60,
      /*height*/ 40, buffer_usage, fuchsia::sysmem::PixelFormatType::BGRA32,
      std::make_optional(memory_constraints));

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
                                       .is_opaque = false};

  // Import all the images.
  renderer.ImportBufferImage(render_target);
  renderer.ImportBufferImage(renderable_texture);
  renderer.ImportBufferImage(transparent_texture);

  // Create the two renderables.
  const uint32_t kRenderableWidth = 4;
  const uint32_t kRenderableHeight = 2;
  Rectangle2D renderable(glm::vec2(6, 3), glm::vec2(kRenderableWidth, kRenderableHeight));
  Rectangle2D transparent_renderable(glm::vec2(7, 3),
                                     glm::vec2(kRenderableWidth, kRenderableHeight));

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

        // Directly reading the vmo values does not unmap the sRGB image values back
        // into a linear space. So we have to do that conversion here before we do
        // any value comparisons. This conversion could be done automatically if we were
        // doing a Vulkan read on the vk::Image directly and not a sysmem read of the vmo,
        // but we don't have direct access to the images in the Renderer.
        uint8_t linear_vals[num_bytes];
        for (uint32_t i = 0; i < num_bytes; i++) {
          // Do not de-encode the alpha value.
          if ((i + 1) % 4 == 0) {
            linear_vals[i] = vmo_host[i];
            continue;
          }
          float lin_val = std::powf((float(vmo_host[i]) / float(0xFF)), 2.2f);
          linear_vals[i] = lin_val * 255U;
        }

        // Make sure the pixels are in the right order give that we rotated
        // the rectangle.
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 6, 3), glm::ivec4(255, 0, 0, 255));
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 6, 4), glm::ivec4(255, 0, 0, 255));
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 7, 3), glm::ivec4(128, 255, 0, 255));
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 7, 4), glm::ivec4(128, 255, 0, 255));
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 8, 3), glm::ivec4(128, 255, 0, 255));
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 8, 4), glm::ivec4(128, 255, 0, 255));
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 9, 3), glm::ivec4(128, 255, 0, 255));
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 9, 4), glm::ivec4(128, 255, 0, 255));
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 10, 3), glm::ivec4(0, 255, 0, 128));
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 10, 4), glm::ivec4(0, 255, 0, 128));

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
      });
}

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

  // First create the pair of sysmem tokens, one for the client, one for the renderer.
  auto tokens = flatland::SysmemTokens::Create(sysmem_allocator_.get());

  auto target_tokens = flatland::SysmemTokens::Create(sysmem_allocator_.get());

  // Register the collection with the renderer.
  auto collection_id = allocation::GenerateUniqueBufferCollectionId();

  auto result = renderer.ImportBufferCollection(collection_id, sysmem_allocator_.get(),
                                                std::move(tokens.dup_token));
  EXPECT_TRUE(result);

  // Create a client-side handle to the buffer collection and set the client constraints.
  auto [buffer_usage, memory_constraints] = GetUsageAndMemoryConstraintsForCpuWriteOften();
  auto client_collection = CreateBufferCollectionSyncPtrAndSetConstraints(
      sysmem_allocator_.get(), std::move(tokens.local_token),
      /*image_count*/ 1,
      /*width*/ 1,
      /*height*/ 1, buffer_usage, fuchsia::sysmem::PixelFormatType::BGRA32,
      std::make_optional(memory_constraints));

  auto target_id = allocation::GenerateUniqueBufferCollectionId();
  result = renderer.RegisterRenderTargetCollection(target_id, sysmem_allocator_.get(),
                                                   std::move(target_tokens.dup_token));
  EXPECT_TRUE(result);

  // Create a client-side handle to the buffer collection and set the client constraints.
  auto client_target = CreateBufferCollectionSyncPtrAndSetConstraints(
      sysmem_allocator_.get(), std::move(target_tokens.local_token),
      /*image_count*/ 1,
      /*width*/ 60,
      /*height*/ 40, buffer_usage, fuchsia::sysmem::PixelFormatType::BGRA32,
      std::make_optional(memory_constraints));

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
                                      .is_opaque = false};

  // Create the texture that will go on the transparent renderable.
  ImageMetadata transparent_texture = {.collection_id = collection_id,
                                       .identifier = allocation::GenerateUniqueImageId(),
                                       .vmo_index = 0,
                                       .width = 1,
                                       .height = 1,
                                       .multiply_color = {0, 1, 0, 0.5},
                                       .is_opaque = false};

  // Import all the images.
  renderer.ImportBufferImage(render_target);
  renderer.ImportBufferImage(renderable_texture);
  renderer.ImportBufferImage(transparent_texture);

  // Create the two renderables.
  const uint32_t kRenderableWidth = 4;
  const uint32_t kRenderableHeight = 2;
  Rectangle2D renderable(glm::vec2(6, 3), glm::vec2(kRenderableWidth, kRenderableHeight));
  Rectangle2D transparent_renderable(glm::vec2(7, 3),
                                     glm::vec2(kRenderableWidth, kRenderableHeight));

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

        // Directly reading the vmo values does not unmap the sRGB image values back
        // into a linear space. So we have to do that conversion here before we do
        // any value comparisons. This conversion could be done automatically if we were
        // doing a Vulkan read on the vk::Image directly and not a sysmem read of the vmo,
        // but we don't have direct access to the images in the Renderer.
        uint8_t linear_vals[num_bytes];
        for (uint32_t i = 0; i < num_bytes; i++) {
          // Do not de-encode the alpha value.
          if ((i + 1) % 4 == 0) {
            linear_vals[i] = vmo_host[i];
            continue;
          }

          // Function to convert from sRGB to linear RGB.
          float s_val = (float(vmo_host[i]) / float(0xFF));
          if (0.f <= s_val && s_val <= 0.04045f) {
            linear_vals[i] = (s_val / 12.92f) * 255U;
          } else {
            linear_vals[i] = std::powf(((s_val + 0.055f) / 1.055f), 2.4f) * 255U;
          }
        }

// The sRGB values are different on different platforms and so the uncompressed values
// will likewise be different. Specifically, it is the AMLOGIC platforms (astro, sherlock,
// vim3) that have a different compressed value. So we define different uncompressed linear
// rgb values to depending on whether or not we are running on AMLOGIC.
#ifdef PLATFORM_AMLOGIC
        const uint32_t kCompVal = 128;
#else
        const uint32_t kCompVal = 126;
#endif

        // Make sure the pixels are in the right order give that we rotated
        // the rectangle.
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 6, 3), glm::ivec4(0, 0, 255, 255));
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 6, 4), glm::ivec4(0, 0, 255, 255));
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 7, 3),
                  glm::ivec4(0, kCompVal, kCompVal, 255));
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 7, 4),
                  glm::ivec4(0, kCompVal, kCompVal, 255));
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 8, 3),
                  glm::ivec4(0, kCompVal, kCompVal, 255));
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 8, 4),
                  glm::ivec4(0, kCompVal, kCompVal, 255));
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 9, 3),
                  glm::ivec4(0, kCompVal, kCompVal, 255));
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 9, 4),
                  glm::ivec4(0, kCompVal, kCompVal, 255));
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 10, 3), glm::ivec4(0, kCompVal, 0, 128));
        EXPECT_EQ(GetPixel(linear_vals, kTargetWidth, 10, 4), glm::ivec4(0, kCompVal, 0, 128));

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
                                                std::move(image_tokens.dup_token));
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
  auto import_res = renderer.ImportBufferImage(image_metadata);
  EXPECT_TRUE(import_res);

  // Create a pair of tokens for the render target allocation.
  auto render_target_tokens = flatland::SysmemTokens::Create(sysmem_allocator_.get());

  // Register the render target tokens with the renderer.
  auto render_target_collection_id = allocation::GenerateUniqueBufferCollectionId();
  result =
      renderer.RegisterRenderTargetCollection(render_target_collection_id, sysmem_allocator_.get(),
                                              std::move(render_target_tokens.dup_token));
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
  import_res = renderer.ImportBufferImage(render_target_metadata);
  EXPECT_TRUE(import_res);

  // Create a renderable where the upper-left hand corner should be at position (0,0) with a
  // width/height of (32,32).
  Rectangle2D image_renderable(glm::vec2(0, 0), glm::vec2(kTargetWidth, kTargetHeight));

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
                                                std::move(image_tokens.dup_token));
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
  auto import_res = renderer.ImportBufferImage(image_metadata);
  EXPECT_TRUE(import_res);

  // Create a pair of tokens for the render target allocation.
  auto render_target_tokens = flatland::SysmemTokens::Create(sysmem_allocator_.get());

  // Register the render target tokens with the renderer.
  auto render_target_collection_id = allocation::GenerateUniqueBufferCollectionId();
  result =
      renderer.RegisterRenderTargetCollection(render_target_collection_id, sysmem_allocator_.get(),
                                              std::move(render_target_tokens.dup_token));
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
  import_res = renderer.ImportBufferImage(render_target_metadata);
  EXPECT_TRUE(import_res);

  // Create a renderable where the upper-left hand corner should be at position (0,0) with a
  // width/height of (32,32).
  Rectangle2D image_renderable(glm::vec2(0, 0), glm::vec2(kTargetWidth, kTargetHeight));
  // Render the renderable to the render target.
  renderer.Render(render_target_metadata, {image_renderable}, {image_metadata});
  renderer.WaitIdle();

  // Note that we cannot read pixel values from either buffer because protected memory does not
  // allow that.
}

}  // namespace flatland
