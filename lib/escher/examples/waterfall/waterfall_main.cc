// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmath>
#include <iostream>

#include "demo.h"

#include "escher/escher.h"
#include "escher/escher_process_init.h"
#include "escher/examples/waterfall/scenes/demo_scene.h"
#include "escher/examples/waterfall/scenes/wobbly_rings_scene.h"
#include "escher/geometry/types.h"
#include "escher/renderer/paper_renderer.h"
#include "escher/scene/stage.h"
#include "escher/util/stopwatch.h"
#include "escher/vk/vulkan_swapchain_helper.h"
#include "ftl/logging.h"

static constexpr int kDemoWidth = 1600;
static constexpr int kDemoHeight = 1024;

// Material design places objects from 0.0f to 24.0f.
static constexpr float kNear = 24.f;
static constexpr float kFar = 0.f;

bool g_show_debug_info = false;
bool g_enable_lighting = true;
int g_current_scene = 0;

static void key_callback(GLFWwindow* window,
                         int key,
                         int scancode,
                         int action,
                         int mods) {
  // We only care about presses, not releases.
  if (action == GLFW_PRESS) {
    switch (key) {
      case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        break;
      case GLFW_KEY_D:
        g_show_debug_info = !g_show_debug_info;
        break;
      case GLFW_KEY_SPACE:
        g_enable_lighting = !g_enable_lighting;
        break;
      case GLFW_KEY_0:
        g_current_scene = 10;
        break;
      case GLFW_KEY_1:
      case GLFW_KEY_2:
      case GLFW_KEY_3:
      case GLFW_KEY_4:
      case GLFW_KEY_5:
      case GLFW_KEY_6:
      case GLFW_KEY_7:
      case GLFW_KEY_8:
      case GLFW_KEY_9:
        g_current_scene = key - GLFW_KEY_0 - 1;
      default:
        break;
    }
  }
}

std::unique_ptr<Demo> create_demo(bool use_fullscreen) {
  Demo::WindowParams window_params;
  window_params.window_name = "Escher Waterfall Demo (Vulkan)";
  window_params.width = kDemoWidth;
  window_params.height = kDemoHeight;
  window_params.desired_swapchain_image_count = 2;
  window_params.use_fullscreen = use_fullscreen;

  Demo::InstanceParams instance_params;

  if (use_fullscreen) {
    FTL_LOG(INFO) << "Using fullscreen window: " << window_params.width << "x"
                  << window_params.height;
  } else {
    FTL_LOG(INFO) << "Using 'windowed' window: " << window_params.width << "x"
                  << window_params.height;
  }

  return std::make_unique<Demo>(instance_params, window_params);
}

int main(int argc, char** argv) {
  bool use_fullscreen = false;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp("--fullscreen", argv[i])) {
      use_fullscreen = true;
    }
  }

  auto demo = create_demo(use_fullscreen);
  glfwSetKeyCallback(demo->GetWindow(), key_callback);

  escher::GlslangInitializeProcess();
  {
    escher::VulkanContext vulkan_context = demo->GetVulkanContext();

    escher::Escher escher(vulkan_context, demo->GetVulkanSwapchain());
    auto renderer = escher.NewPaperRenderer();
    escher::VulkanSwapchainHelper swapchain_helper(demo->GetVulkanSwapchain(),
                                                   renderer);

    escher::vec2 focus;
    escher::Stage stage;
    stage.Resize(escher::SizeI(kDemoWidth, kDemoHeight), 1.0,
                 escher::SizeI(0, 0));
    stage.set_viewing_volume(
        escher::ViewingVolume(kDemoWidth, kDemoHeight, kNear, kFar));
    // TODO: perhaps lights should be initialized by the various demo scenes.
    stage.set_key_light(escher::DirectionalLight(
        escher::vec2(-0.25f * M_PI, 0.25f * M_PI), 0.25f * M_PI, 1.f));
    stage.set_fill_light(escher::AmbientLight(0.3f));

    escher::Stopwatch stopwatch;
    uint64_t frame_count = 0;
    uint64_t first_frame_microseconds;

    // Create list of scenes
    std::vector<std::unique_ptr<Scene>> scenes;
    scenes.emplace_back(new WobblyRingsScene(&vulkan_context, &escher));
    scenes.emplace_back(new DemoScene(&vulkan_context, &escher));
    for (auto& scene : scenes) {
      scene->Init(&stage);
    }

    while (!glfwWindowShouldClose(demo->GetWindow())) {
      g_current_scene = g_current_scene % scenes.size();
      auto& scene = scenes.at(g_current_scene);
      escher::Model* model = scene->Update(stopwatch, frame_count, &stage);

      renderer->set_show_debug_info(g_show_debug_info);
      renderer->set_enable_lighting(g_enable_lighting);

      swapchain_helper.DrawFrame(stage, *model);

      if (++frame_count == 1) {
        first_frame_microseconds = stopwatch.GetElapsedMicroseconds();
        stopwatch.Reset();
      }

      glfwPollEvents();
    }

    vulkan_context.device.waitIdle();

    auto microseconds = stopwatch.GetElapsedMicroseconds();
    double fps = (frame_count - 1) * 1000000.0 / microseconds;
    FTL_LOG(INFO) << "Average frame rate: " << fps;
    FTL_LOG(INFO) << "First frame took: " << first_frame_microseconds / 1000.0
                  << " milliseconds";
  }
  escher::GlslangFinalizeProcess();

  return 0;
}
