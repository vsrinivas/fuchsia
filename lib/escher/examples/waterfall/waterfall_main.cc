// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "demo.h"

#include "escher/escher.h"
#include "escher/escher_process_init.h"
#include "escher/geometry/types.h"
#include "escher/scene/model.h"
#include "escher/scene/stage.h"
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
    escher::VulkanContext vulkan_context = demo->GetVulkanContext();
    escher::Escher escher(vulkan_context, demo->GetVulkanSwapchain());
    escher::vec2 focus;
    escher::Stage stage;
    // AppTestScene scene;

    while (!glfwWindowShouldClose(demo->GetWindow())) {
      // escher::Model model = scene.GetModel(stage.viewing_volume(), focus);
      escher::Model model;  // dummy model
      model.set_blur_plane_height(12.0f);
      escher.Render(stage, model);

      glfwPollEvents();
    }
    vulkan_context.device.waitIdle();
  }
  escher::GlslangFinalizeProcess();

  return 0;
}
