// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/escher/waterfall2/waterfall_demo.h"

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

// Constructor helper.
ShaderProgramPtr CreateShaderProgram(Escher* escher) {
  escher->shader_program_factory()->filesystem()->InitializeWithRealFiles(
      {"shaders/simple.vert", "shaders/simple.frag"},
      "garnet/examples/escher/waterfall2/");

  // We only obtain the factory above in order to to initialize with files from
  // the filesystem.  The usual, convenient way to obtain a shader program is to
  // ask Escher (which ends up delegating to the factory above).
  return escher->GetGraphicsProgram("shaders/simple.vert",
                                    "shaders/simple.frag", ShaderVariantArgs());
}

WaterfallDemo::WaterfallDemo(DemoHarness* harness, int argc, char** argv)
    : Demo(harness, "Waterfall Demo"),
      renderer_(WaterfallRenderer::New(GetEscherWeakPtr(),
                                       CreateShaderProgram(escher()))) {
  ProcessCommandLineArgs(argc, argv);

  InitializeEscherStage(harness->GetWindowParams());
  InitializeDemoScene();

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
  material_ =
      Material::New(vec4(1, 1, 1, 1),
                    escher()->NewTexture(escher()->NewGradientImage(256, 256),
                                         vk::Filter::eLinear));

  ring_ = escher::NewRingMesh(
      escher(), MeshSpec{MeshAttribute::kPosition2D | MeshAttribute::kUV}, 8,
      vec2(0.f, 0.f), 300.f, 200.f);
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
      case 'D':
        show_debug_info_ = !show_debug_info_;
        return true;
      default:
        return Demo::HandleKeyPress(key);
    }
  }
}

void WaterfallDemo::DrawFrame(const FramePtr& frame,
                              const ImagePtr& output_image) {
  TRACE_DURATION("gfx", "WaterfallDemo::DrawFrame");

  Camera camera = Camera::NewOrtho(stage_.viewing_volume());

  const float x = 900.f + sin(static_cast<float>(frame_count_) / 100.f) * 600.f;
  const float y = 700.f + sin(static_cast<float>(frame_count_) / 80.f) * 400.f;
  Model model({Object(Transform(vec3(x, y, 20)), ring_, material_)});

  renderer_->DrawFrame(frame, stage_, model, camera, output_image);

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
