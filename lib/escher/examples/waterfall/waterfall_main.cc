// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "demo.h"

#include "escher/escher.h"
#include "escher/escher_process_init.h"
#include "escher/geometry/types.h"
#include "escher/material/material.h"
#include "escher/renderer/paper_renderer.h"
#include "escher/scene/model.h"
#include "escher/scene/stage.h"
#include "escher/util/stopwatch.h"
#include "escher/vk/vulkan_swapchain_helper.h"
#include "ftl/logging.h"

static void key_callback(GLFWwindow* window,
                         int key,
                         int scancode,
                         int action,
                         int mods) {
  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    glfwSetWindowShouldClose(window, GLFW_TRUE);
}

std::unique_ptr<Demo> create_demo() {
  Demo::WindowParams window_params;
  window_params.window_name = "Escher Waterfall Demo (Vulkan)";

  Demo::InstanceParams instance_params;

  // TODO: use make_unique().
  return std::unique_ptr<Demo>(new Demo(instance_params, window_params));
}

int main(int argc, char** argv) {
  auto demo = create_demo();
  glfwSetKeyCallback(demo->GetWindow(), key_callback);

  escher::GlslangInitializeProcess();
  {
    using escher::vec2;
    using escher::vec3;

    escher::VulkanContext vulkan_context = demo->GetVulkanContext();

    escher::Escher escher(vulkan_context, demo->GetVulkanSwapchain());
    escher::VulkanSwapchainHelper swapchain_helper(demo->GetVulkanSwapchain(),
                                                   escher.NewPaperRenderer());

    vec2 focus;
    escher::Stage stage;
    stage.Resize(escher::SizeI(1024, 1024), 1.0, escher::SizeI(0, 0));
    stage.set_brightness(0.0);
    float brightness_change = 0.0;

    // AppTestScene scene;

    escher::Stopwatch stopwatch;
    uint64_t frame_count = 0;
    uint64_t first_frame_microseconds;

    // escher::Model model = scene.GetModel(stage.viewing_volume(), focus);

    auto checkerboard = ftl::MakeRefCounted<escher::Texture>(
        escher.NewCheckerboardImage(16, 16), vulkan_context.device,
        vk::Filter::eNearest);

    auto yellow = ftl::MakeRefCounted<escher::Material>();
    yellow->set_color(vec3(0.937f, 0.886f, 0.184f));
    auto purple = ftl::MakeRefCounted<escher::Material>(checkerboard);
    purple->set_color(vec3(0.588f, 0.239f, 0.729f));

    escher::Object circle(
        escher::Object::NewCircle(vec2(512.f, 512.f), 256.f, 5.f, yellow));
    escher::Object rectangle(escher::Object::NewRect(
        vec2(12.f, 300.f), vec2(1000.f, 300.f), 6.f, purple));

    std::vector<escher::Object> objects{circle, rectangle};
    escher::Model model(objects);

    while (!glfwWindowShouldClose(demo->GetWindow())) {
      if ((frame_count % 200) == 0) {
        stage.set_brightness(0.0);
        brightness_change = 0.01;
      } else if ((frame_count % 100) == 0) {
        stage.set_brightness(1.0);
        brightness_change = -0.01;
      } else {
        stage.set_brightness(stage.brightness() + brightness_change);
      }

      model.set_blur_plane_height(12.0f);
      swapchain_helper.DrawFrame(stage, model);

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
