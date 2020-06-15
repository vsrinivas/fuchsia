// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testing/test_loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>

#include <thread>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/lib/escher/vk/vulkan_device_queues.h"
#include "src/ui/scenic/lib/common/display_util.h"
#include "src/ui/scenic/lib/display/display_manager.h"
#include "src/ui/scenic/lib/flatland/renderer/null_renderer.h"
#include "src/ui/scenic/lib/flatland/renderer/tests/common.h"
#include "src/ui/scenic/lib/flatland/renderer/vk_renderer.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtx/matrix_transform_2d.hpp>

namespace scenic_impl {

namespace display {

class DisplayTest : public gtest::RealLoopFixture {
 protected:
  void SetUp() override {
    if (VK_TESTS_SUPPRESSED()) {
      return;
    }
    gtest::RealLoopFixture::SetUp();

    // Create the SysmemAllocator.
    zx_status_t status = fdio_service_connect(
        "/svc/fuchsia.sysmem.Allocator", sysmem_allocator_.NewRequest().TakeChannel().release());

    async_set_default_dispatcher(dispatcher());
    display_manager_ = std::make_unique<display::DisplayManager>();

    display_manager_->WaitForDefaultDisplayController([] {});
    RunLoopUntil([this] { return display_manager_->default_display() != nullptr; });
  }

  void TearDown() override {
    if (VK_TESTS_SUPPRESSED()) {
      return;
    }
    display_manager_.reset();
    sysmem_allocator_ = nullptr;
    gtest::RealLoopFixture::TearDown();
  }

  std::unique_ptr<display::DisplayManager> display_manager_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
};

// Create a buffer collection and set constraints on the display, the vulkan renderer
// and the client, and make sure that the collection is still properly allocated.
VK_TEST_F(DisplayTest, SetAllConstraintsTest) {
  const uint64_t kWidth = 60;
  const uint64_t kHeight = 40;

  // Create the VK renderer.
  auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
  auto unique_escher =
      std::make_unique<escher::Escher>(env->GetVulkanDevice(), env->GetFilesystem());
  flatland::VkRenderer renderer(std::move(unique_escher));

  // Grab the display controller.
  auto display_controller = display_manager_->default_display_controller();
  EXPECT_TRUE(display_controller);

  // First create the pair of sysmem tokens, one for the client, one for the renderer.
  auto tokens = flatland::CreateSysmemTokens(sysmem_allocator_.get());

  // Create display token.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr display_token;
  auto status = tokens.local_token->Duplicate(std::numeric_limits<uint32_t>::max(),
                                              display_token.NewRequest());
  EXPECT_EQ(status, ZX_OK);

  // Register the collection with the renderer, which sets the vk constraints.
  auto renderer_collection_id =
      renderer.RegisterRenderTargetCollection(sysmem_allocator_.get(), std::move(tokens.dup_token));
  EXPECT_NE(renderer_collection_id, flatland::Renderer::kInvalidId);

  // Validating should fail, because we've only set the renderer constraints.
  auto buffer_metadata = renderer.Validate(renderer_collection_id);
  EXPECT_FALSE(buffer_metadata.has_value());

  // Set the display constraints on the display controller.
  fuchsia::hardware::display::ImageConfig image_config = {
      .width = kWidth,
      .height = kHeight,
      .pixel_format = ZX_PIXEL_FORMAT_RGB_x888,
  };
  auto display_collection_id = scenic::ImportBufferCollection(
      *display_controller.get(), std::move(display_token), image_config);
  EXPECT_NE(display_collection_id, 0U);

  // Validating should still fail, since even though we have the renderer and display, we don't have
  // the client constraints set.
  buffer_metadata = renderer.Validate(renderer_collection_id);
  EXPECT_FALSE(buffer_metadata.has_value());

  // Create a client-side handle to the buffer collection and set the client constraints.
  auto client_collection = flatland::CreateClientPointerWithConstraints(
      sysmem_allocator_.get(), std::move(tokens.local_token),
      /*image_count*/ 1,
      /*width*/ kWidth,
      /*height*/ kHeight);

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

  // Now that the renderer, client and display have set their contraints, we validate one last
  // time and this time it should return real data.
  buffer_metadata = renderer.Validate(renderer_collection_id);
  EXPECT_TRUE(buffer_metadata.has_value());
}

}  // namespace display
}  // namespace scenic_impl
