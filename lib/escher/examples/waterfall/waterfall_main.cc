// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "demo.h"

#include "escher/escher.h"
#include "escher/escher_process_init.h"
#include "escher/geometry/tessellation.h"
#include "escher/geometry/types.h"
#include "escher/material/material.h"
#include "escher/renderer/paper_renderer.h"
#include "escher/scene/model.h"
#include "escher/scene/stage.h"
#include "escher/shape/modifier_wobble.h"
#include "escher/util/stopwatch.h"
#include "escher/vk/vulkan_swapchain_helper.h"
#include "ftl/logging.h"

static constexpr int kDemoWidth = 2160;
static constexpr int kDemoHeight = 1440;
bool g_show_debug_info = false;

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
      default:
        break;
    }
  }
}

std::unique_ptr<Demo> create_demo() {
  Demo::WindowParams window_params;
  window_params.window_name = "Escher Waterfall Demo (Vulkan)";
  window_params.width = kDemoWidth;
  window_params.height = kDemoHeight;
  window_params.desired_swapchain_image_count = 2;

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
    using escher::MeshAttribute;
    using escher::MeshSpec;
    using escher::Object;
    using escher::ShapeModifier;

    escher::VulkanContext vulkan_context = demo->GetVulkanContext();

    escher::Escher escher(vulkan_context, demo->GetVulkanSwapchain());
    auto renderer = escher.NewPaperRenderer();
    escher::VulkanSwapchainHelper swapchain_helper(demo->GetVulkanSwapchain(),
                                                   renderer);

    vec2 focus;
    escher::Stage stage;
    stage.Resize(escher::SizeI(kDemoWidth, kDemoHeight), 1.0,
                 escher::SizeI(0, 0));
    stage.set_brightness(0.0);
    float brightness_change = 0.0;

    // AppTestScene scene;

    escher::Stopwatch stopwatch;
    uint64_t frame_count = 0;
    uint64_t first_frame_microseconds;

    // escher::Model model = scene.GetModel(stage.viewing_volume(), focus);

    auto checkerboard = ftl::MakeRefCounted<escher::Texture>(
        escher.NewCheckerboardImage(32, 24), vulkan_context.device,
        vk::Filter::eNearest);

    auto blue = ftl::MakeRefCounted<escher::Material>();
    auto pink = ftl::MakeRefCounted<escher::Material>();
    auto green = ftl::MakeRefCounted<escher::Material>();
    auto blue_green = ftl::MakeRefCounted<escher::Material>();
    auto purple = ftl::MakeRefCounted<escher::Material>(checkerboard);
    blue->set_color(vec3(0.188f, 0.188f, 0.788f));
    pink->set_color(vec3(0.929f, 0.678f, 0.925f));
    green->set_color(vec3(0.259f, 0.956f, 0.667));
    blue_green->set_color(vec3(0.039f, 0.788f, 0.788f));
    purple->set_color(vec3(0.588f, 0.239f, 0.729f));

    Object rectangle(
        Object::NewRect(vec2(80.f, 80.f), vec2(2000.f, 1280.f), 8.f, purple));
    Object circle1(Object::NewCircle(vec2(612.f, 212.f), 200.f, 5.f, blue));
    Object circle2(Object::NewCircle(vec2(412.f, 800.f), 200.f, 5.f, blue));
    Object circle3(Object::NewCircle(vec2(162.f, 412.f), 120.f, 3.f, blue));
    Object circle4(Object::NewCircle(vec2(850.f, 600.f), 120.f, 3.f, blue));

    // Create meshes for fancy wobble effect.
    MeshSpec spec{MeshAttribute::kPosition | MeshAttribute::kPositionOffset |
                  MeshAttribute::kPerimeterPos | MeshAttribute::kUV};
    auto ring_mesh1 = escher::NewRingMesh(&escher, spec, 8, vec2(0.f, 0.f),
                                          300.f, 250.f, 18.f, -15.f);
    auto ring_mesh2 = escher::NewRingMesh(&escher, spec, 8, vec2(0.f, 0.f),
                                          200.f, 150.f, 11.f, -8.f);
    auto ring_mesh3 = escher::NewRingMesh(&escher, spec, 8, vec2(0.f, 0.f),
                                          100.f, 50.f, 5.f, -2.f);
    Object ring1(ring_mesh1, vec3(512.f, 512.f, 6.f), pink);
    ring1.set_shape_modifiers(ShapeModifier::kWobble);
    Object ring2(ring_mesh2, vec3(512.f, 512.f, 4.f), green);
    ring2.set_shape_modifiers(ShapeModifier::kWobble);
    Object ring3(ring_mesh3, vec3(512.f, 512.f, 2.f), blue_green);
    ring3.set_shape_modifiers(ShapeModifier::kWobble);

    constexpr float TWO_PI = 6.28318530718f;
    escher::ModifierWobble wobble_data{{{-0.3f * TWO_PI, 0.4f, 7.f * TWO_PI},
                                        {-0.2f * TWO_PI, 0.2f, 23.f * TWO_PI},
                                        {1.f * TWO_PI, 0.6f, 5.f * TWO_PI}}};
    ring1.set_shape_modifier_data(wobble_data);
    ring2.set_shape_modifier_data(wobble_data);
    ring3.set_shape_modifier_data(wobble_data);

    std::vector<Object> objects{circle1,   circle2, circle3, circle4,
                                rectangle, ring1,   ring2,   ring3};
    escher::Model model(objects);

    while (!glfwWindowShouldClose(demo->GetWindow())) {
      renderer->set_show_debug_info(g_show_debug_info);

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
      model.set_time(stopwatch.GetElapsedSeconds());
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
