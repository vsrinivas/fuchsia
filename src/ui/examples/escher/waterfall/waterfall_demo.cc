// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/escher/waterfall/waterfall_demo.h"

#include "src/lib/files/file.h"
#include "src/ui/examples/escher/waterfall/scenes/paper_demo_scene1.h"
#include "src/ui/lib/escher/defaults/default_shader_program_factory.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/mesh/tessellation.h"
#include "src/ui/lib/escher/paper/paper_renderer_static_config.h"
#include "src/ui/lib/escher/paper/paper_scene.h"
#include "src/ui/lib/escher/paper/paper_shader_structs.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/renderer/frame.h"
#include "src/ui/lib/escher/scene/camera.h"
#include "src/ui/lib/escher/scene/viewing_volume.h"
#include "src/ui/lib/escher/shape/mesh.h"
#include "src/ui/lib/escher/util/enum_utils.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/util/trace_macros.h"
#include "src/ui/lib/escher/vk/shader_module_template.h"
#include "src/ui/lib/escher/vk/shader_program.h"
#include "src/ui/lib/escher/vk/texture.h"

using namespace escher;

static constexpr float kNear = 1.f;
static constexpr float kFar = -200.f;
static constexpr size_t kMaxNumPointLights = 2;
static constexpr vk::Format kYuvTextureFormat = vk::Format::eG8B8G8R8422Unorm;
static constexpr vk::ImageTiling kYuvTextureTiling = vk::ImageTiling::eOptimal;

namespace {

bool IsImageFormatSupported(vk::PhysicalDevice device, vk::Format format, vk::ImageTiling tiling) {
  vk::FormatProperties properties = device.getFormatProperties(format);
  if (tiling == vk::ImageTiling::eLinear) {
    return properties.linearTilingFeatures != vk::FormatFeatureFlags();
  } else {
    return properties.optimalTilingFeatures != vk::FormatFeatureFlags();
  }
}

}  // namespace

WaterfallDemo::WaterfallDemo(escher::EscherWeakPtr escher_in, vk::Format swapchain_format, int argc,
                             char** argv)
    : Demo(std::move(escher_in), "Waterfall Demo") {
  ProcessCommandLineArgs(argc, argv);

  // Initialize filesystem with files before creating renderer; it will use them
  // to generate the necessary ShaderPrograms.
  escher()->shader_program_factory()->filesystem()->InitializeWithRealFiles(
      kPaperRendererShaderPaths);

  auto& device_caps = escher()->device()->caps();

  renderer_ = escher::PaperRenderer::New(GetEscherWeakPtr());

  // Determine the allowable MSAA sample counts to cycle through with "M" key.
  {
    auto filtered =
        // TODO(fxbug.dev/44326): 8x MSAA causes a segfault on NVIDIA/Linux.
        // device_caps.GetAllMatchingSampleCounts({1U, 2U, 4U, 8});
        device_caps.GetAllMatchingSampleCounts({1U, 2U, 4U});
    FX_CHECK(!filtered.empty());
    allowed_sample_counts_.reserve(filtered.size());
    for (auto sample_count : filtered) {
      // PaperRendererConfig expects uint8_t, not size_t.
      allowed_sample_counts_.push_back(static_cast<uint8_t>(sample_count));
    }
    if (allowed_sample_counts_.size() >= 2) {
      // Start with the cheapest available MSAA.
      current_sample_count_index_ = 1;
    }
  }

  if (device_caps.allow_ycbcr && IsImageFormatSupported(escher()->vk_physical_device(),
                                                        kYuvTextureFormat, kYuvTextureTiling)) {
    InitializeYcbcrTexture();
  }

  renderer_config_.debug_frame_number = true;
  renderer_config_.shadow_type = PaperRendererShadowType::kShadowVolume;
  renderer_config_.msaa_sample_count = allowed_sample_counts_[current_sample_count_index_];
  renderer_config_.num_depth_buffers = 2;
  renderer_config_.depth_stencil_format =
      ESCHER_CHECKED_VK_RESULT(device_caps.GetMatchingDepthStencilFormat(
          {vk::Format::eD24UnormS8Uint, vk::Format::eD32SfloatS8Uint}));

  WarmPipelineCache(swapchain_format);
  renderer_->SetConfig(renderer_config_);

  // Start with 1 light.  Number of lights can be cycled via CycleNumLights().  Light positions
  // and colors are animated by UpdateLighting().
  paper_scene_ = fxl::MakeRefCounted<PaperScene>();
  paper_scene_->point_lights.resize(1);
}

WaterfallDemo::~WaterfallDemo() {}

void WaterfallDemo::WarmPipelineCache(vk::Format swapchain_format) const {
  TRACE_DURATION("gfx", "WaterfallDemo::WarmPipelineCache");

  // Make a copy of the config, we'll be modifying it.
  PaperRendererConfig config = renderer_config_;

  // Build pipelines for rendering to the display as well as offscreen.
  std::vector<std::pair<vk::Format, vk::ImageLayout>> formats_and_layouts = {
      {swapchain_format, vk::ImageLayout::eColorAttachmentOptimal},
      {swapchain_format, vk::ImageLayout::ePresentSrcKHR},
  };

  std::vector<escher::SamplerPtr> immutable_samplers;
  if (ycbcr_tex_) {
    immutable_samplers.push_back(ycbcr_tex_->sampler());
  }

  for (size_t count : allowed_sample_counts_) {
    config.msaa_sample_count = count;

    for (auto shadow_type : EnumArray<PaperRendererShadowType>()) {
      if (!renderer_->SupportsShadowType(shadow_type)) {
        continue;
      }
      config.shadow_type = shadow_type;

      for (auto& p : formats_and_layouts) {
        auto& output_format = p.first;
        auto& output_swapchain_layout = p.second;
        PaperRenderer::WarmPipelineAndRenderPassCaches(escher(), config, output_format,
                                                       output_swapchain_layout, immutable_samplers);
      }
    }
  }
}

void WaterfallDemo::SetWindowSize(vk::Extent2D window_size) {
  FX_CHECK(paper_scene_);
  if (window_size_ == window_size)
    return;

  window_size_ = window_size;
  paper_scene_->bounding_box =
      escher::BoundingBox(vec3(0.f, 0.f, kFar), vec3(window_size.width, window_size.height, kNear));

  InitializeDemoScenes();
}

void WaterfallDemo::InitializeYcbcrTexture() {
  FX_CHECK(escher()->device()->caps().allow_ycbcr);

  const char* kYuvFramePath = "/assets/bbb_frame.yuv";
  constexpr uint32_t kYuvFrameWidth = 320;
  constexpr uint32_t kYuvFrameHeight = 180;
  constexpr vk::Format kYuvFrameFormat = kYuvTextureFormat;

  auto base = escher()->shader_program_factory()->filesystem()->base_path();
  FX_CHECK(base);
  std::string path = *base + kYuvFramePath;
  std::vector<uint8_t> data;
  FX_CHECK(files::ReadFileToVector(path, &data)) << "failed to read: " << path;

  auto image = escher()->gpu_allocator()->AllocateImage(
      escher()->resource_recycler(),
      {.format = kYuvFrameFormat,
       .width = kYuvFrameWidth,
       .height = kYuvFrameHeight,
       .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
       .tiling = kYuvTextureTiling});

  BatchGpuUploader gpu_uploader(escher()->GetWeakPtr(), 0);
  image_utils::WritePixelsToImage(&gpu_uploader, data.data(), image,
                                  vk::ImageLayout::eShaderReadOnlyOptimal);

  // NOTE: we *could* use a semaphore to guarantee that the upload finishes before we use the
  // texture.  However, we still haven't warmed the pipeline cache; this will take millisecond, by
  // which time the texture should be finished uploading.
  gpu_uploader.Submit();

  ycbcr_tex_ = escher::Texture::New(escher()->resource_recycler(), image, vk::Filter::eNearest);
}

void WaterfallDemo::InitializeDemoScenes() {
  demo_scenes_.clear();
  demo_scenes_.emplace_back(new PaperDemoScene1(this, ycbcr_tex_));
  demo_scenes_.emplace_back(new PaperDemoScene1(this));
  demo_scenes_.back()->ToggleGraph();
  for (auto& scene : demo_scenes_) {
    scene->Init(paper_scene_.get());
  }
}

void WaterfallDemo::ProcessCommandLineArgs(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (!strcmp("--debug", argv[i])) {
      show_debug_info_ = true;
    } else if (!strcmp("--no-debug", argv[i])) {
      show_debug_info_ = false;
    }
  }
}

void WaterfallDemo::CycleNumLights() {
  uint32_t num_point_lights = (paper_scene_->num_point_lights() + 1) % (kMaxNumPointLights + 1);
  paper_scene_->point_lights.resize(num_point_lights);
  FX_LOGS(INFO) << "WaterfallDemo number of point lights: " << num_point_lights;
}

void WaterfallDemo::CycleAnimationState() {
  animation_state_ = (animation_state_ + 1) % 3;
  switch (animation_state_) {
    case 0:
      object_stopwatch_.Start();
      lighting_stopwatch_.Start();
      break;
    case 1:
      object_stopwatch_.Stop();
      lighting_stopwatch_.Start();
      break;
    case 2:
      object_stopwatch_.Stop();
      lighting_stopwatch_.Stop();
      break;
    default:
      FX_CHECK(false) << "animation_state_ must be 0-2, is: " << animation_state_;
  }
}

bool WaterfallDemo::HandleKeyPress(std::string key) {
  if (key.size() > 1) {
    if (key == "SPACE") {
      CycleAnimationState();
      return true;
    }
    return Demo::HandleKeyPress(key);
  } else {
    char key_char = key[0];
    switch (key_char) {
      // Cycle through camera projection modes.
      case 'C': {
        camera_projection_mode_ = (camera_projection_mode_ + 1) % 5;
        const char* kCameraModeStrings[5] = {
            "orthographic", "perspective", "tilted perspective", "tilted perspective from corner",
            "stereo",
        };
        FX_LOGS(INFO) << "Camera projection mode: " << kCameraModeStrings[camera_projection_mode_];
        return true;
      }
      // Toggle display of debug information.
      case 'D': {
        show_debug_info_ = !show_debug_info_;
        renderer_config_.debug = show_debug_info_;
        FX_LOGS(INFO) << "WaterfallDemo " << (show_debug_info_ ? "enabled" : "disabled")
                      << " debugging.";
        renderer_->SetConfig(renderer_config_);
        return true;
      }
      case 'L': {
        CycleNumLights();
        return true;
      }
      // Cycle through MSAA sample counts.
      case 'M': {
        current_sample_count_index_ =
            (current_sample_count_index_ + 1) % allowed_sample_counts_.size();
        renderer_config_.msaa_sample_count = allowed_sample_counts_[current_sample_count_index_];
        FX_LOGS(INFO) << "MSAA sample count: " << unsigned{renderer_config_.msaa_sample_count};
        renderer_->SetConfig(renderer_config_);
        return true;
      }
      // Cycle through shadow algorithms..
      case 'S': {
        auto& shadow_type = renderer_config_.shadow_type;
        shadow_type = EnumCycle(shadow_type);
        while (!renderer_->SupportsShadowType(shadow_type)) {
          FX_LOGS(INFO) << "WaterfallDemo skipping unsupported shadow type: " << shadow_type;
          shadow_type = EnumCycle(shadow_type);
        }
        renderer_->SetConfig(renderer_config_);
        FX_LOGS(INFO) << "WaterfallDemo changed shadow type: " << renderer_config_;
        return true;
      }
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
      case '0':
        current_scene_ = (demo_scenes_.size() + (key_char - '0') - 1) % demo_scenes_.size();
        FX_LOGS(INFO) << "Current scene index: " << current_scene_;
        return true;
      default:
        return Demo::HandleKeyPress(key);
    }
  }
}

// Helper function for DrawFrame().
static std::vector<escher::Camera> GenerateCameras(int camera_projection_mode,
                                                   const escher::ViewingVolume& volume,
                                                   const escher::FramePtr& frame) {
  switch (camera_projection_mode) {
    // Orthographic full-screen.
    case 0: {
      return {escher::Camera::NewOrtho(volume)};
    }
    // Perspective where floor plane is full-screen, and parallel to screen.
    case 1: {
      vec3 eye(volume.width() / 2, volume.height() / 2, -10000);
      vec3 target(volume.width() / 2, volume.height() / 2, 0);
      vec3 up(0, -1, 0);
      return {
          escher::Camera::NewPerspective(volume, glm::lookAt(eye, target, up), glm::radians(8.f))};
    }
    // Perspective from tilted viewpoint (from x-center of stage).
    case 2: {
      vec3 eye(volume.width() / 2, 6000, -2000);
      vec3 target(volume.width() / 2, volume.height() / 2, 0);
      vec3 up(0, -1, 0);
      return {
          escher::Camera::NewPerspective(volume, glm::lookAt(eye, target, up), glm::radians(15.f))};
    } break;
    // Perspective from tilted viewpoint (from corner).
    case 3: {
      vec3 eye(volume.width() / 3, 6000, -3000);
      vec3 target(volume.width() / 2, volume.height() / 3, 0);
      vec3 up(0, -1, 0);
      return {
          escher::Camera::NewPerspective(volume, glm::lookAt(eye, target, up), glm::radians(15.f))};
    } break;
    // Stereo/Perspective from tilted viewpoint (from corner).  This also
    // demonstrates the ability to provide the view-projection matrix in a
    // buffer instead of having the PaperRenderer upload the vp-matrix itself.
    // This is typically used with a "pose buffer" in HMD applications.
    // NOTE: the camera's transform must be fairly close to what will be read
    // from the pose buffer, because the camera's position is used for z-sorting
    // etc.
    case 4: {
      vec3 eye(volume.width() / 2, 6000, -3500);
      vec3 eye_offset(40.f, 0.f, 0.f);
      vec3 target(volume.width() / 2, volume.height() / 2, 0);
      vec3 up(0, -1, 0);
      float fov = glm::radians(15.f);
      auto left_camera =
          escher::Camera::NewPerspective(volume, glm::lookAt(eye - eye_offset, target, up), fov);
      auto right_camera =
          escher::Camera::NewPerspective(volume, glm::lookAt(eye + eye_offset, target, up), fov);

      // Obtain a buffer and populate it as though it were obtained by invoking
      // PoseBufferLatchingShader.
      auto binding =
          escher::NewPaperShaderUniformBinding<escher::PaperShaderLatchedPoseBuffer>(frame);
      binding.first->vp_matrix[0] = left_camera.projection() * left_camera.transform();
      binding.first->vp_matrix[1] = right_camera.projection() * right_camera.transform();
      escher::BufferPtr latched_pose_buffer(binding.second.buffer);

      // Both cameras use the same buffer, but index into it using a different
      // eye index.  NOTE: if you comment these lines out, there will be no
      // visible difference, because PaperRenderer will compute/upload the same
      // project * transform matrix.  What would happen if you swap the kLeft
      // and kRight?
      left_camera.SetLatchedPoseBuffer(latched_pose_buffer, escher::CameraEye::kLeft);
      right_camera.SetLatchedPoseBuffer(latched_pose_buffer, escher::CameraEye::kRight);

      left_camera.SetViewport({0.f, 0.25f, 0.5f, 0.5f});
      right_camera.SetViewport({0.5f, 0.25f, 0.5f, 0.5f});
      return {left_camera, right_camera};
    } break;
    default:
      // Should not happen.
      FX_DCHECK(false);
      return {escher::Camera::NewOrtho(volume)};
  }
}

static void UpdateLighting(PaperScene* paper_scene, const escher::Stopwatch& stopwatch,
                           PaperRendererShadowType shadow_type) {
  const size_t num_point_lights = paper_scene->num_point_lights();
  if (num_point_lights == 0 || shadow_type == PaperRendererShadowType::kNone) {
    paper_scene->ambient_light.color = vec3(1, 1, 1);
    return;
  }

  // Set the ambient light to an arbitrary value that looks OK.  The intensities
  // of the point lights will be chosen so that the total light intensity on an
  // unshadowed fragment is vec3(1,1,1).
  const vec3 kAmbientLightColor(0.4f, 0.5f, 0.5f);
  paper_scene->ambient_light.color = kAmbientLightColor;

  for (auto& pl : paper_scene->point_lights) {
    pl.color = (vec3(1.f, 1.f, 1.f) - kAmbientLightColor) / float(num_point_lights);

    // Choose a light intensity that looks good with the falloff.  If an object
    // is too close to the light it will appear washed out.
    // TODO(fxbug.dev/7260): add HDR support to address this.
    pl.color *= 2.5f;
    pl.falloff = 0.001f;
  }

  // Simple animation of point light.
  const float width = paper_scene->width();
  const float height = paper_scene->height();
  if (num_point_lights == 1) {
    paper_scene->point_lights[0].position = vec3(
        width * .3f, height * .3f, -(800.f + 200.f * sin(stopwatch.GetElapsedSeconds() * 1.2f)));
  } else {
    FX_DCHECK(num_point_lights == 2);

    paper_scene->point_lights[0].position = vec3(
        width * .3f, height * .3f, -(800.f + 300.f * sin(stopwatch.GetElapsedSeconds() * 1.2f)));
    paper_scene->point_lights[1].position =
        vec3(width * (0.6f + 0.3f * sin(stopwatch.GetElapsedSeconds() * 0.7f)),
             height * (0.4f + 0.2f * sin(stopwatch.GetElapsedSeconds() * 0.6f)), -900.f);

    // Make the light colors subtly different.
    vec3 color_diff = vec3(.02f, -.01f, .04f) * paper_scene->point_lights[0].color;
    paper_scene->point_lights[0].color += color_diff;
    paper_scene->point_lights[1].color -= color_diff;
  }
}

void WaterfallDemo::DrawFrame(const FramePtr& frame, const ImagePtr& output_image,
                              const escher::SemaphorePtr& framebuffer_acquired) {
  TRACE_DURATION("gfx", "WaterfallDemo::DrawFrame");

  // WarmPipelineCache() generated all of the necessary pipelines.
  frame->DisableLazyPipelineCreation();

  SetWindowSize({output_image->width(), output_image->height()});

  frame->cmds()->AddWaitSemaphore(framebuffer_acquired,
                                  vk::PipelineStageFlagBits::eColorAttachmentOutput);

  std::vector<Camera> cameras =
      GenerateCameras(camera_projection_mode_, ViewingVolume(paper_scene_->bounding_box), frame);

  // Animate light positions and intensities.
  UpdateLighting(paper_scene_.get(), lighting_stopwatch_, renderer_config_.shadow_type);

  auto gpu_uploader =
      std::make_shared<BatchGpuUploader>(escher()->GetWeakPtr(), frame->frame_number());

  renderer_->BeginFrame(frame, gpu_uploader, paper_scene_, std::move(cameras), output_image);
  {
    TRACE_DURATION("gfx", "WaterfallDemo::DrawFrame[scene]");
    demo_scenes_[current_scene_]->Update(object_stopwatch_, paper_scene_.get(), renderer_.get());
  }
  renderer_->FinalizeFrame();
  escher::SemaphorePtr upload_semaphore;
  if (gpu_uploader->HasContentToUpload()) {
    upload_semaphore = escher::Semaphore::New(escher()->vk_device());
    gpu_uploader->AddSignalSemaphore(upload_semaphore);
  }
  gpu_uploader->Submit();
  renderer_->EndFrame(std::move(upload_semaphore));
}
