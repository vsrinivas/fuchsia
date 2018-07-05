// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/escher/waterfall/waterfall_demo.h"

#include "garnet/examples/escher/waterfall/scenes/demo_scene.h"
#include "garnet/examples/escher/waterfall/scenes/ring_tricks1.h"
#include "garnet/examples/escher/waterfall/scenes/ring_tricks2.h"
#include "garnet/examples/escher/waterfall/scenes/ring_tricks3.h"
#include "garnet/examples/escher/waterfall/scenes/uber_scene.h"
#include "garnet/examples/escher/waterfall/scenes/uber_scene2.h"
#include "garnet/examples/escher/waterfall/scenes/uber_scene3.h"
#include "garnet/examples/escher/waterfall/scenes/wobbly_ocean_scene.h"
#include "garnet/examples/escher/waterfall/scenes/wobbly_rings_scene.h"
#include "lib/escher/renderer/shadow_map.h"
#include "lib/escher/scene/camera.h"

// Material design places objects from 0.0f to 24.0f.
static constexpr float kNear = 100.f;
static constexpr float kFar = -1.f;

// Directional light is 50% intensity; ambient light will adjust automatically.
static constexpr float kLightIntensity = 0.5f;

// Directional light parameters.
static constexpr float kLightDispersion = M_PI * 0.15f;
static constexpr float kLightElevationRadians = M_PI / 3.f;

static constexpr size_t kOffscreenBenchmarkFrameCount = 1000;

WaterfallDemo::WaterfallDemo(DemoHarness* harness, int argc, char** argv)
    : Demo(harness),
      renderer_(escher::PaperRenderer::New(GetEscherWeakPtr())),
      shadow_renderer_(escher::ShadowMapRenderer::New(
          GetEscherWeakPtr(), renderer_->model_data(),
          renderer_->model_renderer())),
      moment_shadow_renderer_(escher::MomentShadowMapRenderer::New(
          GetEscherWeakPtr(), renderer_->model_data(),
          renderer_->model_renderer())),
      swapchain_helper_(harness->GetVulkanSwapchain(),
                        escher()->vulkan_context().device,
                        escher()->vulkan_context().queue) {
  ProcessCommandLineArgs(argc, argv);
  InitializeEscherStage(harness->GetWindowParams());
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
}

void WaterfallDemo::ProcessCommandLineArgs(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (!strcmp("--scene", argv[i])) {
      if (i == argc - 1) {
        FXL_LOG(ERROR) << "--scene must be followed by a numeric argument";
      } else {
        char* end;
        int scene = strtol(argv[i + 1], &end, 10);
        if (argv[i + 1] == end) {
          FXL_LOG(ERROR) << "--scene must be followed by a numeric argument";
        } else {
          current_scene_ = scene;
        }
      }
    } else if (!strcmp("--debug", argv[i])) {
      show_debug_info_ = true;
    } else if (!strcmp("--no-debug", argv[i])) {
      show_debug_info_ = false;
    }
  }
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

void WaterfallDemo::InitializeDemoScenes() {
  scenes_.emplace_back(new RingTricks2(this));
  scenes_.emplace_back(new UberScene3(this));
  scenes_.emplace_back(new WobblyOceanScene(this));
  scenes_.emplace_back(new WobblyRingsScene(
      this, vec3(0.012, 0.047, 0.427), vec3(0.929f, 0.678f, 0.925f),
      vec3(0.259f, 0.956f, 0.667), vec3(0.039f, 0.788f, 0.788f),
      vec3(0.188f, 0.188f, 0.788f), vec3(0.588f, 0.239f, 0.729f)));
  scenes_.emplace_back(new UberScene2(this));
  scenes_.emplace_back(new RingTricks3(this));
  // scenes_.emplace_back(new RingTricks1(this));

  const int kNumColorsInScheme = 4;
  vec3 color_schemes[4][kNumColorsInScheme]{
      {vec3(0.565, 0.565, 0.560), vec3(0.868, 0.888, 0.438),
       vec3(0.905, 0.394, 0.366), vec3(0.365, 0.376, 0.318)},
      {vec3(0.299, 0.263, 0.209), vec3(0.986, 0.958, 0.553),
       vec3(0.773, 0.750, 0.667), vec3(0.643, 0.785, 0.765)},
      {vec3(0.171, 0.245, 0.120), vec3(0.427, 0.458, 0.217),
       vec3(0.750, 0.736, 0.527), vec3(0.366, 0.310, 0.280)},
      {vec3(0.170, 0.255, 0.276), vec3(0.300, 0.541, 0.604),
       vec3(0.637, 0.725, 0.747), vec3(0.670, 0.675, 0.674)},
  };
  for (auto& color_scheme : color_schemes) {
    // Convert colors from sRGB
    for (int i = 0; i < kNumColorsInScheme; i++) {
      color_scheme[i] = escher::SrgbToLinear(color_scheme[i]);
    }

    // Create a new scheme with each color scheme
    scenes_.emplace_back(new WobblyRingsScene(
        this, color_scheme[0], color_scheme[1], color_scheme[1],
        color_scheme[1], color_scheme[2], color_scheme[3]));
  }
  for (auto& scene : scenes_) {
    scene->Init(&stage_);
  }
}

bool WaterfallDemo::HandleKeyPress(std::string key) {
  if (key.size() > 1) {
    if (key == "SPACE") {
      shadow_mode_ = static_cast<ShadowMode>((shadow_mode_ + 1) %
                                             ShadowMode::kNumShadowModes);
      return true;
    }
    return Demo::HandleKeyPress(key);
  } else {
    char key_char = key[0];
    switch (key_char) {
      case 'A':
        enable_ssdo_acceleration_ = !enable_ssdo_acceleration_;
        FXL_LOG(INFO) << "Enable SSDO acceleration: "
                      << (enable_ssdo_acceleration_ ? "true" : "false");
        return true;
      case 'B':
        run_offscreen_benchmark_ = true;
        return true;
      case 'C':
        camera_projection_mode_ = (camera_projection_mode_ + 1) % 3;
        FXL_LOG(INFO) << "Camera projection mode: " << camera_projection_mode_;
        return true;
      case 'D':
        show_debug_info_ = !show_debug_info_;
        return true;
      case 'M':
        stop_time_ = !stop_time_;
        return true;
      case 'P':
        profile_one_frame_ = true;
        return true;
      case 'S':
        sort_by_pipeline_ = !sort_by_pipeline_;
        FXL_LOG(INFO) << "Sort object by pipeline: "
                      << (sort_by_pipeline_ ? "true" : "false");
        return true;
      case '1':
        current_scene_ = 0;
        return true;
      case '2':
        current_scene_ = 1;
        return true;
      case '3':
        current_scene_ = 2;
        return true;
      case '4':
        current_scene_ = 3;
        return true;
      case '5':
        current_scene_ = 4;
        return true;
      case '6':
        current_scene_ = 5;
        return true;
      case '7':
        current_scene_ = 6;
        return true;
      case '8':
        current_scene_ = 7;
        return true;
      case '9':
        current_scene_ = 8;
        return true;
      case '0':
        current_scene_ = 9;
        return true;
      default:
        return Demo::HandleKeyPress(key);
    }
  }
}

static escher::Camera GenerateCamera(int camera_projection_mode,
                                     const escher::ViewingVolume& volume) {
  switch (camera_projection_mode) {
    case 0:
      return escher::Camera::NewOrtho(volume);

    case 1:
      return escher::Camera::NewPerspective(
          volume,
          glm::translate(
              vec3(-volume.width() / 2, -volume.height() / 2, -10000)),
          glm::radians(8.f));
    case 2: {
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

void WaterfallDemo::DrawFrame() {
  current_scene_ = current_scene_ % scenes_.size();
  auto& scene = scenes_.at(current_scene_);
  escher::Model* model = scene->Update(stopwatch_, frame_count_, &stage_);
  escher::Model* overlay_model = scene->UpdateOverlay(
      stopwatch_, frame_count_, swapchain_helper_.swapchain().width,
      swapchain_helper_.swapchain().height);

  renderer_->set_show_debug_info(show_debug_info_);
  renderer_->set_sort_by_pipeline(sort_by_pipeline_);
  renderer_->set_enable_ssdo_acceleration(enable_ssdo_acceleration_);
  switch (shadow_mode_) {
    case ShadowMode::kNone:
      renderer_->set_shadow_type(escher::PaperRendererShadowType::kNone);
      break;
    case ShadowMode::kSsdo:
      renderer_->set_shadow_type(escher::PaperRendererShadowType::kSsdo);
      break;
    case ShadowMode::kShadowMap:
      renderer_->set_shadow_type(escher::PaperRendererShadowType::kShadowMap);
      break;
    case ShadowMode::kMomentShadowMap:
      renderer_->set_shadow_type(
          escher::PaperRendererShadowType::kMomentShadowMap);
      break;
    default:
      FXL_LOG(ERROR) << "Invalid shadow_mode_: " << shadow_mode_;
      shadow_mode_ = ShadowMode::kNone;
      renderer_->set_shadow_type(escher::PaperRendererShadowType::kNone);
  }

  escher::Camera camera =
      GenerateCamera(camera_projection_mode_, stage_.viewing_volume());

  if (run_offscreen_benchmark_) {
    run_offscreen_benchmark_ = false;
    stopwatch_.Stop();
    renderer_->set_show_debug_info(false);

    renderer_->RunOffscreenBenchmark(
        kDemoWidth, kDemoHeight, swapchain_helper_.swapchain().format,
        kOffscreenBenchmarkFrameCount,
        [this, model, &camera, overlay_model](
            const escher::FramePtr& frame,
            const escher::ImagePtr& color_image_out) {
          renderer_->DrawFrame(frame, stage_, *model, camera, color_image_out,
                               escher::ShadowMapPtr(), overlay_model);
        });
    renderer_->set_show_debug_info(show_debug_info_);
    if (!stop_time_) {
      stopwatch_.Start();
    }
  }

  if (stop_time_) {
    stopwatch_.Stop();
  } else {
    stopwatch_.Start();
  }

  if (animate_light_) {
    light_azimuth_radians_ += 0.02;
  }
  vec3 light_direction = glm::normalize(vec3(-cos(light_azimuth_radians_),
                                             -sin(light_azimuth_radians_),
                                             -tan(kLightElevationRadians)));

  stage_.set_key_light(escher::DirectionalLight(
      escher::vec2(light_azimuth_radians_, kLightElevationRadians),
      kLightDispersion, vec3(kLightIntensity)));

  auto frame =
      escher()->NewFrame("Waterfall Demo", ++frame_count_, profile_one_frame_);

  escher::ShadowMapPtr shadow_map;
  if (shadow_mode_ == kShadowMap || shadow_mode_ == kMomentShadowMap) {
    const vec3 directional_light_color(kLightIntensity);
    renderer_->set_ambient_light_color(vec3(1.f) - directional_light_color);
    const auto& shadow_renderer =
        shadow_mode_ == kShadowMap ? shadow_renderer_ : moment_shadow_renderer_;
    shadow_map = shadow_renderer->GenerateDirectionalShadowMap(
        frame, stage_, *model, light_direction, directional_light_color);
  }

  swapchain_helper_.DrawFrame(frame, renderer_.get(), stage_, *model, camera,
                              shadow_map, overlay_model);

  if (frame_count_ == 1) {
    first_frame_microseconds_ = stopwatch_.GetElapsedMicroseconds();
    stopwatch_.Reset();
  } else if (frame_count_ % 200 == 0) {
    profile_one_frame_ = true;

    // Print out FPS stats.  Omit the first frame when computing the
    // average, because it is generating pipelines.
    auto microseconds = stopwatch_.GetElapsedMicroseconds();
    double fps = (frame_count_ - 2) * 1000000.0 /
                 (microseconds - first_frame_microseconds_);
    FXL_LOG(INFO) << "---- Average frame rate: " << fps;
    FXL_LOG(INFO) << "---- Total GPU memory: "
                  << (escher()->GetNumGpuBytesAllocated() / 1024) << "kB";
  } else {
    profile_one_frame_ = false;
  }
}
