// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <GLFW/glfw3.h>
#include <ShaderLang.h>
#include <vulkan/vulkan.hpp>

#include "ftl/logging.h"

#include "demo.h"

class Waterfall {
public:
  Waterfall(Demo *demo) : demo_(demo) {}
  ~Waterfall() {}

private:
  Demo *demo_;
};

std::unique_ptr<Demo> create_demo() {
  Demo::WindowParams window_params;
  window_params.window_name = "Escher Waterfall Demo (Vulkan)";

  Demo::InstanceParams instance_params;

  // TODO: use make_unique().
  return std::unique_ptr<Demo>(new Demo(instance_params, window_params));
}

int main(int argc, char **argv) {
  auto demo = create_demo();
  Waterfall waterfall(demo.get());

  while (!glfwWindowShouldClose(demo->GetWindow())) {
    glfwPollEvents();
  }

  std::cerr << "HELLO WATERFALL MAIN " << std::endl;
  return 0;
}
