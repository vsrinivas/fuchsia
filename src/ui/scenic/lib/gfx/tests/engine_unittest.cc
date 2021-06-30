// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/engine.h"

#include <lib/async/cpp/time.h>
#include <lib/zx/time.h>

#include <gtest/gtest.h>

#include "lib/gtest/test_loop_fixture.h"
#include "src/lib/testing/loop_fixture/test_loop.h"
#include "src/ui/lib/escher/test/common/gtest_escher.h"
#include "src/ui/lib/escher/test/common/gtest_vulkan.h"
#include "src/ui/lib/escher/test/common/test_with_vk_validation_layer.h"
#include "src/ui/lib/escher/util/fuchsia_utils.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/vk/image_layout_updater.h"
#include "src/ui/scenic/lib/gfx/resources/camera.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/compositor.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer_stack.h"
#include "src/ui/scenic/lib/gfx/resources/material.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/scene.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/shape_node.h"
#include "src/ui/scenic/lib/gfx/resources/renderers/renderer.h"
#include "src/ui/scenic/lib/gfx/tests/mocks/mocks.h"
#include "src/ui/scenic/lib/gfx/tests/mocks/util.h"

namespace {
constexpr scenic_impl::SessionId kSessionId = 1;
constexpr scenic_impl::ResourceId kResourceId = 1;
constexpr uint32_t kWidth = 1920;
constexpr uint32_t kHeight = 1080;
constexpr uint32_t kNumTestFences = 4;
constexpr zx::duration kTimeout = zx::duration::infinite();

// This is used to coordinate fences between the fake swapchain and the test frameworks.
using FenceQueue = std::deque<zx::event>;

// This macro works like a function that checks a variety of conditions, but if those conditions
// fail, the line number for the failure will appear in-line rather than in a helper function.
#define RENDER_SKIPPED_FRAME()                                                                    \
  {                                                                                               \
    bool presented = false;                                                                       \
    engine()->RenderScheduledFrame(/*frame_number=*/1, /*presentation_time=*/zx::time(0),         \
                                   [&](const scheduling::FrameRenderer::Timestamps& timestamps) { \
                                     EXPECT_EQ(timestamps.render_done_time, Now());               \
                                     EXPECT_EQ(timestamps.actual_presentation_time, Now());       \
                                     presented = true;                                            \
                                   });                                                            \
    EXPECT_TRUE(presented);                                                                       \
  }

zx::time Now() { return async::Now(async_get_default_dispatcher()); }

// A fake swapchain that provides the rendering dependencies (i.e., image, acquire, and release
// fence), along with some FrameTiming integration code. This class does not communicate with any
// sort of 'display' abstraction, mocked or otherwise.
class FakeSwapchain : public scenic_impl::gfx::Swapchain {
 public:
  FakeSwapchain(escher::EscherWeakPtr escher, escher::ImagePtr target,
                const std::shared_ptr<FenceQueue>& acquire_fences)
      : escher_(escher), target_(target), acquire_fences_(acquire_fences) {}

  bool DrawAndPresentFrame(const std::shared_ptr<scenic_impl::gfx::FrameTimings>& frame,
                           size_t swapchain_index, scenic_impl::gfx::Layer& layer,
                           DrawCallback draw_callback) override {
    EXPECT_FALSE(acquire_fences_->empty());

    zx::event release_fence;
    zx::event::create(0, &release_fence);

    draw_callback(
        target_, layer,
        escher::GetSemaphoreForEvent(escher_->device(), std::move(acquire_fences_->front())),
        escher::GetSemaphoreForEvent(escher_->device(),
                                     scenic_impl::gfx::test::CopyEvent(release_fence)));
    acquire_fences_->pop_front();

    auto wait = std::make_shared<async::WaitOnce>(release_fence.release(), ZX_EVENT_SIGNALED);
    zx_status_t status = wait->Begin(async_get_default_dispatcher(),
                                     [copy_ref = wait, frame = std::move(frame)](
                                         async_dispatcher_t*, async::WaitOnce*, zx_status_t status,
                                         const zx_packet_signal_t* /*signal*/) mutable {
                                       FX_DCHECK(status == ZX_OK);
                                       frame->OnFrameRendered(0, Now());
                                       frame->OnFramePresented(0, Now());
                                     });
    FX_DCHECK(status == ZX_OK);

    return true;
  }

  bool SetDisplayColorConversion(const ColorTransform& transform) override { return false; }
  void SetUseProtectedMemory(bool use_protected_memory) override {}
  vk::Format GetImageFormat() override { return target_->format(); };

 private:
  escher::EscherWeakPtr escher_;
  escher::ImagePtr target_;
  std::shared_ptr<FenceQueue> acquire_fences_;
};

}  // namespace

namespace scenic_impl {
namespace gfx {
namespace test {

class EngineTest : public escher::test::TestWithVkValidationLayer {
 public:
  // | ::testing::Test |
  void SetUp() override {
    auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
    escher_ =
        std::make_unique<escher::Escher>(env->GetVulkanDevice(), env->GetFilesystem(), nullptr);
    engine_ = std::make_shared<Engine>(escher_->GetWeakPtr(),
                                       /*buffer_collection_importer=*/nullptr, inspect::Node(),
                                       /*request_focus*/ [](auto...) { return false; });
  }

  void TearDown() override {
    // The compositor has to be torn down first, so that the image it is holding is recycled before
    // we try to tear down the resource recycler inside of Escher.
    compositor_.reset();
  }

  void VkWaitUntilIdle() { escher_->vk_device().waitIdle(); }

  // This will create a vector of fences, and insert them into the engine using
  // SignalFencesWhenPreviousRendersAreDone(). The fences are initially checked based on the
  // |initial_signalled_state| argument, but they are also returned as a vector, so that they can be
  // checked again later.
  std::vector<zx::event> CreateAndInsertFences(bool initial_signalled_state) {
    std::vector<zx::event> fences(kNumTestFences);
    std::vector<zx::event> fence_copies(kNumTestFences);

    for (uint32_t i = 0; i < kNumTestFences; ++i) {
      zx::event::create(0, &fences[i]);
      fence_copies[i] = CopyEvent(fences[i]);
    }

    engine()->SignalFencesWhenPreviousRendersAreDone(std::move(fence_copies));

    for (const auto& f : fences) {
      if (initial_signalled_state) {
        EXPECT_TRUE(IsEventSignalled(f, ZX_EVENT_SIGNALED));
      } else {
        EXPECT_FALSE(IsEventSignalled(f, ZX_EVENT_SIGNALED));
      }
    }

    return fences;
  }

  // Create a compositor with real render target, and a fake swapchain. The array of acquire fences
  // to be used by the swapchain is returned. Each successful call to RenderScheduledFrame()
  // requires at least one fence to be remaining in the deque. Each successful render job will
  // remove a fence from the front of the queue.
  std::shared_ptr<FenceQueue> AddCompositor() {
    auto target = escher::image_utils::NewColorAttachmentImage(escher_->image_cache(), kWidth,
                                                               kHeight, vk::ImageUsageFlags());
    target->set_swapchain_layout(vk::ImageLayout::eColorAttachmentOptimal);

    escher::ImageLayoutUpdater layout_updater(escher_->GetWeakPtr());
    layout_updater.ScheduleSetImageInitialLayout(target, vk::ImageLayout::eColorAttachmentOptimal);
    auto semaphore_pair = escher_->semaphore_chain()->TakeLastAndCreateNextSemaphore();
    layout_updater.AddWaitSemaphore(std::move(semaphore_pair.semaphore_to_wait),
                                    vk::PipelineStageFlagBits::eColorAttachmentOutput);
    layout_updater.AddSignalSemaphore(std::move(semaphore_pair.semaphore_to_signal));
    layout_updater.Submit();

    auto fence_queue = std::make_shared<FenceQueue>();
    auto swapchain = std::make_unique<FakeSwapchain>(escher_->GetWeakPtr(), target, fence_queue);
    engine_->renderer()->WarmPipelineCache({swapchain->GetImageFormat()});
    compositor_ = fxl::MakeRefCounted<Compositor>(/*session=*/nullptr, kSessionId, kResourceId,
                                                  Compositor::kTypeInfo, engine_->scene_graph(),
                                                  std::move(swapchain));

    return fence_queue;
  }

  // This helper function attaches a stack of nodes to the compositor, such that there is actual
  // render work for the EngineRenderer to perform.
  void AttachRenderableLayerStack() {
    auto layer = fxl::MakeRefCounted<Layer>(/*session=*/nullptr, kSessionId, kResourceId);
    layer->SetSize({kWidth, kHeight}, /*error_reporter=*/nullptr);
    auto renderer = fxl::MakeRefCounted<Renderer>(/*session=*/nullptr, kSessionId, kResourceId);
    layer->SetRenderer(renderer);
    auto scene =
        fxl::MakeRefCounted<Scene>(/*session=*/nullptr, kSessionId, kResourceId,
                                   fxl::WeakPtr<ViewTreeUpdater>(), /*event_reporter=*/nullptr);
    auto camera = fxl::MakeRefCounted<Camera>(/*session=*/nullptr, kSessionId, kResourceId, scene);
    renderer->SetCamera(camera);
    auto shape_node = fxl::MakeRefCounted<ShapeNode>(/*session=*/nullptr, kSessionId, kResourceId);
    scene->AddChild(shape_node, /*error_reporter=*/nullptr);
    auto material = fxl::MakeRefCounted<Material>(/*session=*/nullptr, kSessionId, kResourceId);
    shape_node->SetMaterial(material);

    auto layer_stack =
        fxl::MakeRefCounted<LayerStack>(/*session=*/nullptr, kSessionId, kResourceId);
    layer_stack->AddLayer(layer, /*error_reporter=*/nullptr);
    compositor_->SetLayerStack(layer_stack);
  }

  Engine* engine() { return engine_.get(); }
  Compositor* compositor() { return compositor_.get(); }

 private:
  std::unique_ptr<escher::Escher> escher_;
  std::shared_ptr<Engine> engine_;
  CompositorPtr compositor_;
};

VK_TEST_F(EngineTest, SkippedFrames) {
  async::TestLoop loop;
  // No compositors
  RENDER_SKIPPED_FRAME();

  // No layer stack
  AddCompositor();
  RENDER_SKIPPED_FRAME();

  // No layer
  auto layer_stack = fxl::MakeRefCounted<LayerStack>(/*session=*/nullptr, kSessionId, kResourceId);
  compositor()->SetLayerStack(layer_stack);
  RENDER_SKIPPED_FRAME();

  // No drawable layer
  auto layer = fxl::MakeRefCounted<Layer>(/*session=*/nullptr, kSessionId, kResourceId);
  layer->SetSize({kWidth, kHeight}, /*error_reporter=*/nullptr);
  layer_stack->AddLayer(layer, /*error_reporter=*/nullptr);
  RENDER_SKIPPED_FRAME();

  // Drawable layer with no content inside of it
  auto renderer = fxl::MakeRefCounted<Renderer>(/*session=*/nullptr, kSessionId, kResourceId);
  layer->SetRenderer(renderer);
  auto scene =
      fxl::MakeRefCounted<Scene>(/*session=*/nullptr, kSessionId, kResourceId,
                                 fxl::WeakPtr<ViewTreeUpdater>(), /*event_reporter=*/nullptr);
  auto camera = fxl::MakeRefCounted<Camera>(/*session=*/nullptr, kSessionId, kResourceId, scene);
  renderer->SetCamera(camera);
  RENDER_SKIPPED_FRAME();
}

VK_TEST_F(EngineTest, ImmediateRender) {
  // TODO(58324): On emulation, WaitIdle is not catching the pending GPU work, but returning
  // immediately, before the work is done and nullifying the test.
  SKIP_TEST_IF_ESCHER_USES_DEVICE(VirtualGpu);

  async::TestLoop loop;

  // Create a compositor and a renderable graph of content.
  auto fence_queue = AddCompositor();
  AttachRenderableLayerStack();

  // Push an already-signaled fence onto the queue, so that rendering is not delayed.
  zx::event acquire_fence;
  zx::event::create(0, &acquire_fence);
  acquire_fence.signal(0u, ZX_EVENT_SIGNALED);
  fence_queue->emplace_back(std::move(acquire_fence));

  bool presented = false;
  engine()->RenderScheduledFrame(
      /*frame_number=*/1, /*presentation_time=*/zx::time(0),
      [&](const scheduling::FrameRenderer::Timestamps& timestamps) { presented = true; });

  // Wait for all rendering to complete.
  VkWaitUntilIdle();
  EXPECT_FALSE(presented);
  loop.RunUntilIdle();
  EXPECT_TRUE(presented);

  CreateAndInsertFences(true);
}

VK_TEST_F(EngineTest, RenderWithDelay) {
  // TODO(58325): The emulator will block of a command queue with a pending fence is submitted. So
  // this test, which depends on a delayed GPU execution, will deadlock.
  SKIP_TEST_IF_ESCHER_USES_DEVICE(VirtualGpu);

  async::TestLoop loop;

  // Create a compositor and a renderable graph of content.
  auto fence_queue = AddCompositor();
  AttachRenderableLayerStack();

  zx::event acquire_fences[2];
  for (auto& f : acquire_fences) {
    zx::event::create(0, &f);
    fence_queue->emplace_back(CopyEvent(f));
  }

  bool presented[2] = {false, false};
  engine()->RenderScheduledFrame(
      /*frame_number=*/1, /*presentation_time=*/zx::time(0),
      [&](const scheduling::FrameRenderer::Timestamps& timestamps) { presented[0] = true; });

  // There shouldn't be any rendering, as the fence has not been signaled yet.
  loop.RunUntilIdle();

  EXPECT_FALSE(presented[0]);
  // Queue some signal fences.
  auto fences0 = CreateAndInsertFences(false);

  // Queue another frame.
  engine()->RenderScheduledFrame(
      /*frame_number=*/1, /*presentation_time=*/zx::time(0),
      [&](const scheduling::FrameRenderer::Timestamps& timestamps) { presented[1] = true; });

  // Queue some more signal fences.
  auto fences1 = CreateAndInsertFences(false);

  // Signal the first fence and wait again.
  auto status = acquire_fences[0].signal(0u, ZX_EVENT_SIGNALED);
  FX_DCHECK(status == ZX_OK);

  // Unfortunately, there is no deterministic way to block on the GPU in this case, other than by
  // waiting on the fences.
  for (auto& f : fences0) {
    EXPECT_EQ(f.wait_one(ZX_EVENT_SIGNALED, zx::deadline_after(kTimeout), nullptr), ZX_OK);
  }
  loop.RunUntilIdle();

  EXPECT_TRUE(presented[0]);
  EXPECT_FALSE(presented[1]);
  for (auto& f : fences1) {
    EXPECT_FALSE(IsEventSignalled(f, ZX_EVENT_SIGNALED));
  }

  // Not signaling a waiting fence causes a timeout, signal for cleanup.
  status = acquire_fences[1].signal(0u, ZX_EVENT_SIGNALED);
  FX_DCHECK(status == ZX_OK);
  VkWaitUntilIdle();
  loop.RunUntilIdle();
}

VK_TEST_F(EngineTest, RenderWithDelayOutOfOrder) {
  // TODO(58325): The emulator will block of a command queue with a pending fence is submitted. So
  // this test, which depends on a delayed GPU execution, will deadlock.
  SKIP_TEST_IF_ESCHER_USES_DEVICE(VirtualGpu);

  async::TestLoop loop;

  // Create a compositor and a renderable graph of content.
  auto fence_queue = AddCompositor();
  AttachRenderableLayerStack();

  zx::event acquire_fences[2];
  for (auto& f : acquire_fences) {
    zx::event::create(0, &f);
    fence_queue->emplace_back(CopyEvent(f));
  }

  bool presented[2] = {false, false};
  engine()->RenderScheduledFrame(
      /*frame_number=*/1, /*presentation_time=*/zx::time(0),
      [&](const scheduling::FrameRenderer::Timestamps& timestamps) { presented[0] = true; });

  // There shouldn't be any rendering, as the fence has not been signaled yet.
  loop.RunUntilIdle();

  EXPECT_FALSE(presented[0]);
  // Queue some signal fences.
  auto fences0 = CreateAndInsertFences(false);

  // Queue another frame.
  engine()->RenderScheduledFrame(
      /*frame_number=*/1, /*presentation_time=*/zx::time(0),
      [&](const scheduling::FrameRenderer::Timestamps& timestamps) { presented[1] = true; });

  // Queue some more signal fences.
  auto fences1 = CreateAndInsertFences(false);

  // Signal the second fence and wait again.
  auto status = acquire_fences[1].signal(0u, ZX_EVENT_SIGNALED);
  FX_DCHECK(status == ZX_OK);
  loop.RunUntilIdle();

  EXPECT_FALSE(presented[0]);
  for (auto& f : fences0) {
    EXPECT_FALSE(IsEventSignalled(f, ZX_EVENT_SIGNALED));
  }
  EXPECT_FALSE(presented[1]);
  for (auto& f : fences1) {
    EXPECT_FALSE(IsEventSignalled(f, ZX_EVENT_SIGNALED));
  }

  // Signal the first fence and wait again.
  status = acquire_fences[0].signal(0u, ZX_EVENT_SIGNALED);
  FX_DCHECK(status == ZX_OK);

  // Unfortunately, there is no deterministic way to block on the GPU in this case, other than by
  // waiting on the fences.
  for (auto& f : fences0) {
    EXPECT_EQ(f.wait_one(ZX_EVENT_SIGNALED, zx::deadline_after(kTimeout), nullptr), ZX_OK);
  }
  for (auto& f : fences1) {
    EXPECT_EQ(f.wait_one(ZX_EVENT_SIGNALED, zx::deadline_after(kTimeout), nullptr), ZX_OK);
  }
  loop.RunUntilIdle();

  // All rendering should be complete.
  EXPECT_TRUE(presented[0]);
  EXPECT_TRUE(presented[1]);

  // Cleanup.
  VkWaitUntilIdle();
  loop.RunUntilIdle();
}

#undef RENDER_SKIPPED_FRAME

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
