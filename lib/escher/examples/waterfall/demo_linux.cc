// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "demo.h"

#include <GLFW/glfw3.h>
#include <iostream>

static Demo* g_demo = nullptr;
static KeyboardHandler* g_keyboard_handler = nullptr;
static GLFWwindow* g_window;

// Helper for Demo::InitGlfw().
static void DemoGlfwErrorCallback(int err_code, const char* err_desc) {
  std::cerr << "GLFW ERROR: " << err_code << " " << err_desc << std::endl;
}

static void DemoGlfwKeyCallback(GLFWwindow* window,
                                int key,
                                int scancode,
                                int action,
                                int mods) {
  FTL_CHECK(g_keyboard_handler);

  // We only care about presses, not releases.
  if (action == GLFW_PRESS) {
    switch (key) {
      case GLFW_KEY_ESCAPE:
        g_keyboard_handler->MaybeFireCallback("ESCAPE");
        break;
      case GLFW_KEY_SPACE:
        g_keyboard_handler->MaybeFireCallback("SPACE");
        break;
      case GLFW_KEY_0:
      case GLFW_KEY_1:
      case GLFW_KEY_2:
      case GLFW_KEY_3:
      case GLFW_KEY_4:
      case GLFW_KEY_5:
      case GLFW_KEY_6:
      case GLFW_KEY_7:
      case GLFW_KEY_8:
      case GLFW_KEY_9: {
        char digit = '0' + (key - GLFW_KEY_0);
        g_keyboard_handler->MaybeFireCallback(std::string(1, digit));
        break;
      }
      case GLFW_KEY_A:
      case GLFW_KEY_B:
      case GLFW_KEY_C:
      case GLFW_KEY_D:
      case GLFW_KEY_E:
      case GLFW_KEY_F:
      case GLFW_KEY_G:
      case GLFW_KEY_H:
      case GLFW_KEY_I:
      case GLFW_KEY_J:
      case GLFW_KEY_K:
      case GLFW_KEY_L:
      case GLFW_KEY_M:
      case GLFW_KEY_N:
      case GLFW_KEY_O:
      case GLFW_KEY_P:
      case GLFW_KEY_Q:
      case GLFW_KEY_R:
      case GLFW_KEY_S:
      case GLFW_KEY_T:
      case GLFW_KEY_U:
      case GLFW_KEY_V:
      case GLFW_KEY_W:
      case GLFW_KEY_X:
      case GLFW_KEY_Y:
      case GLFW_KEY_Z: {
        char letter = 'A' + (key - GLFW_KEY_A);
        g_keyboard_handler->MaybeFireCallback(std::string(1, letter));
        break;
      }
      default:
        break;
    }
  }
}

void Demo::InitWindowSystem() {
  FTL_CHECK(!g_demo);
  g_demo = this;
  g_keyboard_handler = &keyboard_handler_;

  FTL_CHECK(glfwInit());
  glfwSetErrorCallback(DemoGlfwErrorCallback);
}

void Demo::ShutdownWindowSystem() {
  FTL_CHECK(g_demo);
  g_demo = nullptr;
  g_keyboard_handler = nullptr;
  g_window = nullptr;

  // TODO: close window, and... ?

  glfwTerminate();
}

void Demo::SetShouldQuit() {
  glfwSetWindowShouldClose(g_window, GLFW_TRUE);
  should_quit_ = true;
}

void Demo::CreateWindowAndSurface(const WindowParams& params) {
  FTL_CHECK(!g_window);

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

  GLFWmonitor* monitor =
      params.use_fullscreen ? glfwGetPrimaryMonitor() : nullptr;
  g_window = glfwCreateWindow(params.width, params.height,
                              params.window_name.c_str(), monitor, NULL);
  FTL_CHECK(g_window);

  VkSurfaceKHR surface;
  VkResult err = glfwCreateWindowSurface(instance_, g_window, NULL, &surface);
  FTL_CHECK(!err);
  surface_ = surface;

  glfwSetKeyCallback(g_window, DemoGlfwKeyCallback);
}

void Demo::AppendPlatformSpecificInstanceExtensionNames(
    InstanceParams* params) {
  // Get names of extensions required by GLFW.
  uint32_t extensions_count;
  const char** extensions =
      glfwGetRequiredInstanceExtensions(&extensions_count);
  for (uint32_t i = 0; i < extensions_count; ++i) {
    params->extension_names.emplace_back(std::string(extensions[i]));
  }
}

void Demo::PollEvents() {
  glfwPollEvents();
}
