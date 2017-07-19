// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/application_context.h"
#include "application/lib/app/connect.h"
#include "apps/mozart/lib/scene/session_helpers.h"
#include "apps/mozart/lib/scene/types.h"
#include "apps/mozart/services/scene/ops.fidl.h"
#include "apps/mozart/services/scene/scene_manager.fidl.h"
#include "apps/mozart/services/scene/session.fidl.h"
#include "escher/util/image_utils.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/log_settings_command_line.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

#include "magma/third_party/vkcube/cube.h"

using namespace mozart;

const size_t kCubeBufferWidth = 500;
const size_t kCubeBufferHeight = 500;
// If this is 0, loop forever.
const uint32_t kDurationBeforeQuitInSeconds = 20;

class VulkanCubeApp {
 public:
  VulkanCubeApp(struct demo* demo)
      : application_context_(app::ApplicationContext::CreateFromStartupInfo()),
        loop_(mtl::MessageLoop::GetCurrent()),
        demo_(demo) {
    // Connect to the SceneManager service.
    scene_manager_ = application_context_
                         ->ConnectToEnvironmentService<mozart2::SceneManager>();
    scene_manager_.set_connection_error_handler([this] {
      FTL_LOG(INFO) << "Lost connection to SceneManager service.";
      loop_->QuitNow();
    });
  }

  ResourceId NewResourceId() { return ++resource_id_counter_; }

  void Initialize() {
    last_time_ = start_time_ = mx_time_get(MX_CLOCK_MONOTONIC);
    InitializeSwapchain();
    InitializeSession();
  }

  void Update(uint64_t next_presentation_time) {
    // Quit if over time.
    uint64_t elapsed = mx_time_get(MX_CLOCK_MONOTONIC) - start_time_;
    if (kDurationBeforeQuitInSeconds != 0 &&
        elapsed >= kDurationBeforeQuitInSeconds * kBillion) {
      loop_->task_runner()->PostTask([this] {
        session_ = nullptr;
        FTL_LOG(INFO) << "Quitting.";
        loop_->QuitNow();
      });
      return;
    }

    int i = demo_->current_buffer;

    // Render the cube to the current buffer
    RenderCube((next_presentation_time - last_time_) / kMillion);
    last_time_ = next_presentation_time;

    mx::event acquire_fence;
    mx::event::create(0, &acquire_fence);
    mx::event release_fence;
    mx::event::create(0, &release_fence);
    // Signal on the acquire fence ahead of time.
    constexpr mx_status_t kSignalled = MX_USER_SIGNAL_0;
    acquire_fence.signal(0u, kSignalled);

    image_pipe_->PresentImage(
        i, next_presentation_time, std::move(acquire_fence),
        std::move(release_fence), [this](mozart2::PresentationInfoPtr info) {
          Update(info->presentation_time + info->presentation_interval);
        });
  }

  void InitializeSwapchain() {
    mozart::Size size;
    size.width = kCubeBufferWidth;
    size.height = kCubeBufferHeight;
    FTL_DCHECK(size.width > 0 && size.height > 0);
    if ((uint32_t)size.width == demo_->width &&
        (uint32_t)size.height == demo_->height)
      return;

    demo_init_vk_swapchain(demo_);

    VkResult err;
    demo_->swapchainImageCount = kNumBuffers;
    demo_->width = size.width;
    demo_->height = size.height;
    demo_->buffers = (SwapchainBuffers*)malloc(sizeof(SwapchainBuffers) *
                                               demo_->swapchainImageCount);
    assert(demo_->buffers);

    mat4x4_perspective(demo_->projection_matrix, (float)degreesToRadians(45.0f),
                       (float)demo_->width / (float)demo_->height, 0.1f,
                       100.0f);

    for (uint32_t i = 0; i < kNumBuffers; i++) {
      VkImageCreateInfo image_create_info = {
          .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
          .pNext = nullptr,
          .flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT,
          .imageType = VK_IMAGE_TYPE_2D,
          .format = VK_FORMAT_R8G8B8A8_UNORM,
          .extent = VkExtent3D{demo_->width, demo_->height, 1},
          .mipLevels = 1,
          .arrayLayers = 1,
          .samples = VK_SAMPLE_COUNT_1_BIT,
          .tiling = VK_IMAGE_TILING_OPTIMAL,
          .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
          .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
          .queueFamilyIndexCount = 0,      // not used since not sharing
          .pQueueFamilyIndices = nullptr,  // not used since not sharing
          .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      };

      VkImage vk_image;

      err =
          vkCreateImage(demo_->device, &image_create_info, nullptr, &vk_image);
      assert(!err);
      vk_images_[i] = vk_image;

      VkMemoryRequirements memory_reqs;
      vkGetImageMemoryRequirements(demo_->device, vk_image, &memory_reqs);

      uint32_t memory_type = 0;
      for (; memory_type < 32; memory_type++) {
        if ((memory_reqs.memoryTypeBits & (1 << memory_type)))
          break;
      }
      assert(memory_type < 32);

      VkMemoryAllocateInfo alloc_info = {
          .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
          .pNext = nullptr,
          .allocationSize = memory_reqs.size,
          .memoryTypeIndex = memory_type,
      };

      VkDeviceMemory mem;
      /* allocate memory */
      err = vkAllocateMemory(demo_->device, &alloc_info, NULL, &mem);
      assert(!err);
      vk_memories_[i] = mem;

      err = vkBindImageMemory(demo_->device, vk_image, mem, 0);
      assert(!err);

      VkImageViewCreateInfo color_image_view = {
          .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
          .pNext = NULL,
          .format = demo_->format,
          .components =
              {
                  .r = VK_COMPONENT_SWIZZLE_R,
                  .g = VK_COMPONENT_SWIZZLE_G,
                  .b = VK_COMPONENT_SWIZZLE_B,
                  .a = VK_COMPONENT_SWIZZLE_A,
              },
          .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                               .baseMipLevel = 0,
                               .levelCount = 1,
                               .baseArrayLayer = 0,
                               .layerCount = 1},
          .viewType = VK_IMAGE_VIEW_TYPE_2D,
          .flags = 0,
      };

      demo_->buffers[i].image = vk_image;
      color_image_view.image = demo_->buffers[i].image;

      err = vkCreateImageView(demo_->device, &color_image_view, NULL,
                              &demo_->buffers[i].view);
      assert(!err);

      // share underlying memory with mozart
      uint32_t handle;
      err = vkExportDeviceMemoryMAGMA(demo_->device, mem, &handle);
      assert(!err);

      vmos_[i] = mx::vmo(handle);
    }

    demo_prepare(demo_);
    for (int i = 0; i < FRAME_LAG; i++) {
      vkResetFences(demo_->device, 1, &demo_->fences[i]);
    }
  }

  void InitializeSession() {
    FTL_LOG(INFO) << "Creating new Session";
    // TODO: set up SessionListener.
    scene_manager_->CreateSession(session_.NewRequest(), nullptr);

    auto ops = PopulateSession();

    session_->Enqueue(std::move(ops));

    session_.set_connection_error_handler([this] {
      FTL_LOG(INFO) << "Session terminated.";
      loop_->QuitNow();
    });

    session_->Present(0, fidl::Array<mx::event>::New(0),
                      fidl::Array<mx::event>::New(0),
                      [this](mozart2::PresentationInfoPtr info) {});
  }

  fidl::Array<mozart2::OpPtr> PopulateSession() {
    auto ops = fidl::Array<mozart2::OpPtr>::New(0);

    // Create a shape node.
    ResourceId node_id = node_id_ = NewResourceId();
    ops.push_back(NewCreateShapeNodeOp(node_id));

    ResourceId image_pipe_id = NewResourceId();
    ops.push_back(
        NewCreateImagePipeOp(image_pipe_id, image_pipe_.NewRequest()));

    // Create a Material with the image pipe.
    ResourceId material_id = NewResourceId();
    ops.push_back(NewCreateMaterialOp(material_id));
    ops.push_back(NewSetColorOp(material_id, 255, 255, 255, 255));
    ops.push_back(NewSetTextureOp(material_id, image_pipe_id));
    material_resource_id_ = material_id;

    ops.push_back(NewSetMaterialOp(node_id_, material_resource_id_));

    // Generate an image for each buffer.
    for (uint32_t i = 0; i < kNumBuffers; i++) {
      mx::vmo buffer_vmo = std::move(vmos_[i]);  // TODO: don't do this

      const size_t pixel_size = 4;
      auto image_info = mozart2::ImageInfo::New();
      image_info->pixel_format = mozart2::ImageInfo::PixelFormat::BGRA_8;
      image_info->tiling = mozart2::ImageInfo::Tiling::LINEAR;
      image_info->width = kCubeBufferWidth;
      image_info->height = kCubeBufferHeight;
      image_info->stride = pixel_size * kCubeBufferWidth;

      image_pipe_->AddImage(i, std::move(image_info), std::move(buffer_vmo),
                            mozart2::MemoryType::VK_DEVICE_MEMORY, 0);
    }

    // Make the shape a circle.
    ResourceId shape_id = NewResourceId();
    ops.push_back(NewCreateCircleOp(shape_id, 500.f));

    ops.push_back(NewSetShapeOp(node_id, shape_id));

    // Translate the circle.
    const float kScreenWidth = 2160.f;
    const float kScreenHeight = 1440.f;
    float translation[3] = {kScreenWidth / 2, kScreenHeight / 2, 10.f};
    ops.push_back(NewSetTranslationOp(node_id, translation));

    // Create a Scene, and attach to it the Nodes created above.
    ResourceId scene_id = NewResourceId();
    ops.push_back(NewCreateSceneOp(scene_id));

    // Attach the circle to the Scene.
    ops.push_back(NewAddChildOp(scene_id, node_id));

    // Create a Camera to view the Scene.
    ResourceId camera_id = NewResourceId();
    ops.push_back(NewCreateCameraOp(camera_id, scene_id));

    // Create a DisplayRenderer that renders the Scene from the viewpoint of the
    // Camera that we just created.
    ResourceId renderer_id = NewResourceId();
    ops.push_back(NewCreateDisplayRendererOp(renderer_id));
    ops.push_back(NewSetCameraOp(renderer_id, camera_id));

    return ops;
  }

  void RenderCube(uint64_t elapsed_ms) {
    VkResult err;

    demo_update_data_buffer(demo_);

    total_ms_ += elapsed_ms;

    if (elapsed_frames_ && (elapsed_frames_ % num_frames_) == 0) {
      float fps = num_frames_ / (total_ms_ / kMsPerSec);
      printf("Framerate average for last %u frames: %f frames per second\n",
             num_frames_, fps);
      total_ms_ = 0;
      // attempt to log once per second
      num_frames_ = fps;
      elapsed_frames_ = 0;
    }
    elapsed_frames_++;

    // Draw the contents of the scene to a surface.
    demo_draw(demo_);

    // wait for frame to complete before submitting to mozart
    err = vkWaitForFences(demo_->device, 1, &demo_->fences[demo_->frame_index],
                          VK_TRUE, UINT64_MAX);

    assert(!err);
    err = vkResetFences(demo_->device, 1, &demo_->fences[demo_->frame_index]);
    assert(!err);
    demo_->frame_index += 1;
    demo_->frame_index %= FRAME_LAG;

    demo_->current_buffer = (demo_->current_buffer + 1) % kNumBuffers;
    demo_->curFrame++;
  }

  uint64_t start_time() const { return start_time_; }

 private:
  static constexpr uint64_t kBillion = 1000000000;
  static constexpr uint64_t kMillion = 1000000;

  std::unique_ptr<app::ApplicationContext> application_context_;
  app::ApplicationControllerPtr controller_;
  app::ServiceProviderPtr services_;
  mozart2::SceneManagerPtr scene_manager_;
  mtl::MessageLoop* loop_;
  ResourceId resource_id_counter_ = 0;

  mozart2::SessionPtr session_;
  // The ID of the circle we are texturing
  ResourceId node_id_;

  // vk_cube demo state
  struct demo* demo_;

  static constexpr uint32_t kNumBuffers = 3;

  ResourceId material_resource_id_;
  mozart2::ImagePipePtr image_pipe_;
  VkDeviceMemory vk_memories_[kNumBuffers];
  VkImage vk_images_[kNumBuffers];
  mx::vmo vmos_[kNumBuffers];

  uint32_t num_frames_ = 60;
  uint32_t elapsed_frames_ = 0;
  static constexpr float kMsPerSec =
      std::chrono::milliseconds(std::chrono::seconds(1)).count();

  float total_ms_ = 0;

  uint64_t start_time_ = 0;
  uint64_t last_time_ = 0;
};

int main(int argc, char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  if (!ftl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  mtl::MessageLoop loop;

  struct demo vk_cube_demo;
  demo_init(&vk_cube_demo, argc, argv);
  VulkanCubeApp app(&vk_cube_demo);
  app.Initialize();

  // Kick off the cube example. Update takes care of posting new frames (or
  // quitting).
  loop.task_runner()->PostTask([&app] { app.Update(app.start_time()); });
  loop.Run();
  return 0;
}
