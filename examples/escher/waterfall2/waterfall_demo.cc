// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/escher/waterfall2/waterfall_demo.h"

#include "garnet/examples/escher/waterfall/scenes/paper_demo_scene1.h"
#include "garnet/examples/escher/waterfall/scenes/ring_tricks2.h"
#include "lib/escher/defaults/default_shader_program_factory.h"
#include "lib/escher/geometry/tessellation.h"
#include "lib/escher/paper/paper_scene.h"
#include "lib/escher/scene/camera.h"
#include "lib/escher/scene/viewing_volume.h"
#include "lib/escher/shape/mesh.h"
#include "lib/escher/util/enum_utils.h"
#include "lib/escher/util/trace_macros.h"
#include "lib/escher/vk/shader_module_template.h"
#include "lib/escher/vk/shader_program.h"
#include "lib/escher/vk/texture.h"

using namespace escher;

static constexpr float kNear = 200.f;
static constexpr float kFar = -1.f;

WaterfallDemo::WaterfallDemo(DemoHarness* harness, int argc, char** argv)
    : Demo(harness, "Waterfall Demo") {
  ProcessCommandLineArgs(argc, argv);

  // Initialize filesystem with files before creating renderer; it will use them
  // to generate the necessary ShaderPrograms.
  escher()->shader_program_factory()->filesystem()->InitializeWithRealFiles(
      {"shaders/model_renderer/main.frag", "shaders/model_renderer/main.vert",
       "shaders/model_renderer/default_position.vert",
       "shaders/model_renderer/shadow_map_generation.frag",
       "shaders/model_renderer/shadow_map_lighting.frag",
       "shaders/model_renderer/wobble_position.vert",
       "shaders/paper/common/use.glsl",
       "shaders/paper/frag/main_ambient_light.frag",
       "shaders/paper/frag/main_point_light.frag",
       "shaders/paper/vert/compute_model_space_position.vert",
       "shaders/paper/vert/compute_world_space_position.vert",
       "shaders/paper/vert/main_shadow_volume_extrude.vert",
       "shaders/paper/vert/vertex_attributes.vert"});

  renderer_ = escher::PaperRenderer2::New(GetEscherWeakPtr());

  renderer_config_.shadow_type = PaperRendererShadowType::kShadowVolume;
  renderer_config_.msaa_sample_count = 2;
  renderer_config_.num_depth_buffers =
      harness->GetVulkanSwapchain().images.size();
  renderer_->SetConfig(renderer_config_);

  InitializePaperScene(harness->GetWindowParams());
  InitializeDemoScenes();
}

WaterfallDemo::~WaterfallDemo() {
  // Print out FPS stats.  Omit the first frame when computing the average,
  // because it is generating pipelines.
  auto microseconds = stopwatch_.GetElapsedMicroseconds();
  double fps = (frame_count_ - 2) * 1000000.0 /
               (microseconds - first_frame_microseconds_);
  FXL_LOG(INFO) << "Average frame rate: " << fps;
  FXL_LOG(INFO) << "First frame took: " << first_frame_microseconds_ / 1000.0
                << " milliseconds";

  escher()->Cleanup();
}

void WaterfallDemo::InitializePaperScene(
    const DemoHarness::WindowParams& window_params) {
  paper_scene_ = fxl::MakeRefCounted<PaperScene>();

  // Number of lights can be cycled via keyboard event.  Light positions and
  // colors are animated by UpdateLighting().
  paper_scene_->point_lights.resize(1);

  paper_scene_->bounding_box = escher::BoundingBox(
      vec3(0.f, 0.f, kFar),
      vec3(window_params.width, window_params.height, kNear));
}

void WaterfallDemo::InitializeDemoScenes() {
  demo_scenes_.emplace_back(new PaperDemoScene1(this));
  demo_scenes_.emplace_back(new RingTricks2(this));
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

bool WaterfallDemo::HandleKeyPress(std::string key) {
  if (key.size() > 1) {
    if (key == "SPACE") {
      // Start/stop the animation stopwatch.
      animation_stopwatch_.Toggle();
      return true;
    }
    return Demo::HandleKeyPress(key);
  } else {
    char key_char = key[0];
    switch (key_char) {
      // Cycle through camera projection modes.
      case 'C': {
        camera_projection_mode_ = (camera_projection_mode_ + 1) % 4;
        const char* kCameraModeStrings[4] = {"orthographic", "perspective",
                                             "tilted perspective",
                                             "tilted perspective from corner"};
        FXL_LOG(INFO) << "Camera projection mode: "
                      << kCameraModeStrings[camera_projection_mode_];
        return true;
      }
      // Toggle display of debug information.
      case 'D': {
        show_debug_info_ = !show_debug_info_;
        renderer_config_.debug = show_debug_info_;
        FXL_LOG(INFO) << "WaterfallDemo "
                      << (show_debug_info_ ? "enabled" : "disabled")
                      << " debugging.";
        renderer_->SetConfig(renderer_config_);
        return true;
      }
      case 'L': {
        uint32_t num_point_lights = (paper_scene_->num_point_lights() + 1) % 3;
        paper_scene_->point_lights.resize(num_point_lights);
        FXL_LOG(INFO) << "WaterfallDemo number of point lights: "
                      << paper_scene_->num_point_lights();
        return true;
      }
      // Cycle through MSAA sample counts.
      case 'M': {
        auto sample_count = renderer_config_.msaa_sample_count;
        if (sample_count == 1) {
          sample_count = 2;
        } else if (sample_count == 2) {
          // TODO(ES-156): there seems to be a RenderPass-caching bug where if
          // we change the RenderPassInfo's images to have a different sample
          // count, then the old cached RenderPass is not flushed from the
          // cache.  For now, just toggle between two values.
          sample_count = 1;
          // sample_count = 4;
        } else {
          sample_count = 1;
        }
        FXL_LOG(INFO) << "MSAA sample count: " << sample_count;
        renderer_config_.msaa_sample_count = sample_count;
        renderer_->SetConfig(renderer_config_);
        return true;
      }
      // Cycle through shadow algorithms..
      case 'S': {
        auto& shadow_type = renderer_config_.shadow_type;
        shadow_type = EnumCycle(shadow_type);
        while (!renderer_->SupportsShadowType(shadow_type)) {
          FXL_LOG(INFO) << "WaterfallDemo skipping unsupported shadow type: "
                        << shadow_type;
          shadow_type = EnumCycle(shadow_type);
        }
        renderer_->SetConfig(renderer_config_);
        FXL_LOG(INFO) << "WaterfallDemo changed shadow type: "
                      << renderer_config_;
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
        current_scene_ =
            (demo_scenes_.size() + (key_char - '0') - 1) % demo_scenes_.size();
        FXL_LOG(INFO) << "Current scene index: " << current_scene_;
        return true;
      default:
        return Demo::HandleKeyPress(key);
    }
  }
}

// Helper function for DrawFrame().
static escher::Camera GenerateCamera(int camera_projection_mode,
                                     const escher::ViewingVolume& volume) {
  switch (camera_projection_mode) {
    // Orthographic full-screen.
    case 0: {
      return escher::Camera::NewOrtho(volume);
    }
    // Perspective where floor plane is full-screen, and parallel to screen.
    case 1: {
      return escher::Camera::NewPerspective(
          volume,
          glm::translate(
              vec3(-volume.width() / 2, -volume.height() / 2, -10000)),
          glm::radians(8.f));
    }
    // Perspective from tilted viewpoint (from x-center of stage).
    case 2: {
      vec3 eye(volume.width() / 2, 6000, 2000);
      vec3 target(volume.width() / 2, volume.height() / 2, 0);
      vec3 up(0, 1, 0);
      return escher::Camera::NewPerspective(
          volume, glm::lookAt(eye, target, up), glm::radians(15.f));
    } break;
    // Perspective from tilted viewpoint (from corner).
    case 3: {
      vec3 eye(volume.width() / 3, 6000, 3000);
      vec3 target(volume.width() / 2, volume.height() / 3, 0);
      vec3 up(0, 1, 0);
      return escher::Camera::NewPerspective(
          volume, glm::lookAt(eye, target, up), glm::radians(15.f));
    } break;
    default:
      // Should not happen.
      FXL_DCHECK(false);
      return escher::Camera::NewOrtho(volume);
  }
}

static void UpdateLighting(PaperScene* paper_scene,
                           const escher::Stopwatch& stopwatch,
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
    pl.color = (vec3(1, 1, 1) - kAmbientLightColor) / float(num_point_lights);
  }

  // Simple animation of point light.
  const float width = paper_scene->width();
  const float height = paper_scene->height();
  if (num_point_lights == 1) {
    paper_scene->point_lights[0].position =
        vec3(width * .3f, height * .3f,
             800.f + 200.f * sin(stopwatch.GetElapsedSeconds() * 1.2f));
  } else {
    FXL_DCHECK(num_point_lights == 2);

    paper_scene->point_lights[0].position =
        vec3(width * .3f, height * .3f,
             800.f + 300.f * sin(stopwatch.GetElapsedSeconds() * 1.2f));
    paper_scene->point_lights[1].position =
        vec3(width * (0.6f + 0.3f * sin(stopwatch.GetElapsedSeconds() * 0.7f)),
             height * (0.4f + 0.2f * sin(stopwatch.GetElapsedSeconds() * 0.6f)),
             900.f);

    // Make the light colors subtly different.
    vec3 color_diff =
        vec3(.02f, -.01f, .04f) * paper_scene->point_lights[0].color;
    paper_scene->point_lights[0].color += color_diff;
    paper_scene->point_lights[1].color -= color_diff;
  }
}

void WaterfallDemo::DrawFrame(const FramePtr& frame,
                              const ImagePtr& output_image) {
  TRACE_DURATION("gfx", "WaterfallDemo::DrawFrame");

  Camera camera = GenerateCamera(camera_projection_mode_,
                                 ViewingVolume(paper_scene_->bounding_box));

  // Animate light positions and intensities.
  UpdateLighting(paper_scene_.get(), stopwatch_, renderer_config_.shadow_type);

  renderer_->BeginFrame(frame, paper_scene_, camera, output_image);
  {
    TRACE_DURATION("gfx", "WaterfallDemo::DrawFrame[scene]");
    demo_scenes_[current_scene_]->Update(animation_stopwatch_, frame_count(),
                                         paper_scene_.get(), renderer_.get());
  }
  renderer_->EndFrame();

  if (++frame_count_ == 1) {
    first_frame_microseconds_ = stopwatch_.GetElapsedMicroseconds();
    stopwatch_.Reset();
  } else if (frame_count_ % 200 == 0) {
    set_enable_gpu_logging(true);

    // Print out FPS stats.  Omit the first frame when computing the
    // average, because it is generating pipelines.
    auto microseconds = stopwatch_.GetElapsedMicroseconds();
    double fps = (frame_count_ - 2) * 1000000.0 /
                 (microseconds - first_frame_microseconds_);
    FXL_LOG(INFO) << "---- Average frame rate: " << fps;
    FXL_LOG(INFO) << "---- Total GPU memory: "
                  << (escher()->GetNumGpuBytesAllocated() / 1024) << "kB";
  } else {
    set_enable_gpu_logging(false);
  }
}
