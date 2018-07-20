// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/escher/waterfall2/waterfall_demo.h"

#include "garnet/examples/escher/waterfall/scenes/ring_tricks2.h"
#include "lib/escher/defaults/default_shader_program_factory.h"
#include "lib/escher/geometry/tessellation.h"
#include "lib/escher/scene/camera.h"
#include "lib/escher/scene/model.h"
#include "lib/escher/shape/mesh.h"
#include "lib/escher/util/trace_macros.h"
#include "lib/escher/vk/shader_module_template.h"
#include "lib/escher/vk/shader_program.h"
#include "lib/escher/vk/texture.h"

using namespace escher;

static constexpr float kNear = 100.f;
static constexpr float kFar = -1.f;

// Directional light is 50% intensity; ambient light will adjust automatically.
static constexpr float kLightIntensity = 0.5f;

WaterfallDemo::WaterfallDemo(DemoHarness* harness, int argc, char** argv)
    : Demo(harness, "Waterfall Demo") {
  ProcessCommandLineArgs(argc, argv);

  InitializeEscherStage(harness->GetWindowParams());
  InitializeDemoScene();

  // Initialize filesystem with files before creating renderer; it will use them
  // to generate the necessary ShaderPrograms.
  escher()->shader_program_factory()->filesystem()->InitializeWithRealFiles(
      {"shaders/model_renderer/main.frag", "shaders/model_renderer/main.vert",
       "shaders/model_renderer/default_position.vert",
       "shaders/model_renderer/shadow_map_generation.frag",
       "shaders/model_renderer/shadow_map_lighting.frag",
       "shaders/model_renderer/wobble_position.vert"});

  renderer_ = WaterfallRenderer::New(GetEscherWeakPtr());
  renderer_->SetNumDepthBuffers(harness->GetVulkanSwapchain().images.size());
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

void WaterfallDemo::InitializeEscherStage(
    const DemoHarness::WindowParams& window_params) {
  stage_.set_viewing_volume(escher::ViewingVolume(
      window_params.width, window_params.height, kNear, kFar));
  stage_.set_key_light(
      escher::DirectionalLight(escher::vec2(1.5f * M_PI, 1.5f * M_PI),
                               0.15f * M_PI, vec3(kLightIntensity)));
  stage_.set_fill_light(escher::AmbientLight(1.f - kLightIntensity));
}

void WaterfallDemo::InitializeDemoScene() {
  auto texture = escher()->NewTexture(escher()->NewGradientImage(4, 256),
                                      vk::Filter::eLinear);
  material_ = Material::New(vec4(1, 1, 1, 1), texture);
  material2_ = Material::New(vec4(0.4f, 0.3f, 1.f, 0.6f), texture);
  material2_->set_opaque(false);

  ring_ = escher::NewRingMesh(
      escher(), MeshSpec{MeshAttribute::kPosition2D | MeshAttribute::kUV}, 4,
      vec2(0.f, 0.f), 300.f, 200.f);

  scene_ = std::make_unique<RingTricks2>(this);
  scene_->Init(&stage_);
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
      // This is where we would handle the space bar.
      return true;
    }
    return Demo::HandleKeyPress(key);
  } else {
    char key_char = key[0];
    switch (key_char) {
      case 'C': {
        camera_projection_mode_ = (camera_projection_mode_ + 1) % 4;
        const char* kCameraModeStrings[4] = {"orthographic", "perspective",
                                             "tilted perspective",
                                             "tilted perspective from corner"};
        FXL_LOG(INFO) << "Camera projection mode: "
                      << kCameraModeStrings[camera_projection_mode_];
        return true;
      }
      case 'D':
        show_debug_info_ = !show_debug_info_;
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

void WaterfallDemo::DrawFrame(const FramePtr& frame,
                              const ImagePtr& output_image) {
  TRACE_DURATION("gfx", "WaterfallDemo::DrawFrame");

  Camera camera =
      GenerateCamera(camera_projection_mode_, stage_.viewing_volume());

  renderer_->DrawFrame(frame, &stage_, camera, stopwatch_, frame_count(),
                       scene_.get(), output_image);

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
