// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/escher/waterfall/waterfall_demo.h"

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
#include "src/ui/lib/escher/util/trace_macros.h"
#include "src/ui/lib/escher/vk/shader_module_template.h"
#include "src/ui/lib/escher/vk/shader_program.h"
#include "src/ui/lib/escher/vk/texture.h"

using namespace escher;

static constexpr float kNear = 1.f;
static constexpr float kFar = -200.f;
static constexpr size_t kMaxNumPointLights = 2;

WaterfallDemo::WaterfallDemo(escher::EscherWeakPtr escher_in, int argc, char** argv)
    : Demo(std::move(escher_in), "Waterfall Demo") {
  ProcessCommandLineArgs(argc, argv);

  // Initialize filesystem with files before creating renderer; it will use them
  // to generate the necessary ShaderPrograms.
  escher()->shader_program_factory()->filesystem()->InitializeWithRealFiles(
      kPaperRendererShaderPaths);

  renderer_ = escher::PaperRenderer::New(GetEscherWeakPtr());

  renderer_config_.debug_frame_number = true;
  renderer_config_.shadow_type = PaperRendererShadowType::kShadowVolume;
  renderer_config_.msaa_sample_count =
      ESCHER_CHECKED_VK_RESULT(escher()->device()->caps().GetMatchingSampleCount({2U, 4U, 1U}));
  renderer_config_.num_depth_buffers = 2;
  renderer_config_.depth_stencil_format =
      ESCHER_CHECKED_VK_RESULT(escher()->device()->caps().GetMatchingDepthStencilFormat(
          {vk::Format::eD24UnormS8Uint, vk::Format::eD32SfloatS8Uint}));

  renderer_->SetConfig(renderer_config_);

  // Start with 1 light.  Number of lights can be cycled via CycleNumLights().  Light positions
  // and colors are animated by UpdateLighting().
  paper_scene_ = fxl::MakeRefCounted<PaperScene>();
  paper_scene_->point_lights.resize(1);
}

WaterfallDemo::~WaterfallDemo() {}

void WaterfallDemo::SetWindowSize(vk::Extent2D window_size) {
  FXL_CHECK(paper_scene_);
  if (window_size_ == window_size)
    return;

  window_size_ = window_size;
  paper_scene_->bounding_box =
      escher::BoundingBox(vec3(0.f, 0.f, kFar), vec3(window_size.width, window_size.height, kNear));

  InitializeDemoScenes();
}

void WaterfallDemo::InitializeDemoScenes() {
  demo_scenes_.clear();
  demo_scenes_.emplace_back(new PaperDemoScene1(this));
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
  FXL_LOG(INFO) << "WaterfallDemo number of point lights: " << num_point_lights;
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
      FXL_CHECK(false) << "animation_state_ must be 0-2, is: " << animation_state_;
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
        FXL_LOG(INFO) << "Camera projection mode: " << kCameraModeStrings[camera_projection_mode_];
        return true;
      }
      // Toggle display of debug information.
      case 'D': {
        show_debug_info_ = !show_debug_info_;
        renderer_config_.debug = show_debug_info_;
        FXL_LOG(INFO) << "WaterfallDemo " << (show_debug_info_ ? "enabled" : "disabled")
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
        auto sample_count = renderer_config_.msaa_sample_count;
        if (sample_count == 1) {
          sample_count = 2;
        } else if (sample_count == 2) {
          sample_count = 4;
        } else {
          sample_count = 1;
        }
        FXL_LOG(INFO) << "MSAA sample count: " << unsigned{sample_count};
        renderer_config_.msaa_sample_count = sample_count;
        renderer_->SetConfig(renderer_config_);
        return true;
      }
      // Cycle through shadow algorithms..
      case 'S': {
        auto& shadow_type = renderer_config_.shadow_type;
        shadow_type = EnumCycle(shadow_type);
        while (!renderer_->SupportsShadowType(shadow_type)) {
          FXL_LOG(INFO) << "WaterfallDemo skipping unsupported shadow type: " << shadow_type;
          shadow_type = EnumCycle(shadow_type);
        }
        renderer_->SetConfig(renderer_config_);
        FXL_LOG(INFO) << "WaterfallDemo changed shadow type: " << renderer_config_;
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
        FXL_LOG(INFO) << "Current scene index: " << current_scene_;
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
      FXL_DCHECK(false);
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
    // TODO(ES-170): add HDR support to address this.
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
    FXL_DCHECK(num_point_lights == 2);

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
