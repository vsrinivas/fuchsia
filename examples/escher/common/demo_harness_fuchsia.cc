// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/escher/common/demo_harness_fuchsia.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <zx/time.h>

#include "garnet/examples/escher/common/demo.h"
#include "lib/escher/util/trace_macros.h"

// When running on Fuchsia, New() instantiates a DemoHarnessFuchsia.
std::unique_ptr<DemoHarness> DemoHarness::New(
    DemoHarness::WindowParams window_params,
    DemoHarness::InstanceParams instance_params) {
  auto harness = new DemoHarnessFuchsia(nullptr, window_params);
  harness->Init(std::move(instance_params));
  return std::unique_ptr<DemoHarness>(harness);
}

DemoHarnessFuchsia::DemoHarnessFuchsia(async::Loop* loop,
                                       WindowParams window_params)
    : DemoHarness(window_params),
      loop_(loop),
      owned_loop_(loop_ ? nullptr : new async::Loop()),
      startup_context_(fuchsia::sys::StartupContext::CreateFromStartupInfo()) {
  // Provide a PseudoDir where the demo can register debugging services.
  fbl::RefPtr<fs::PseudoDir> debug_dir(fbl::AdoptRef(new fs::PseudoDir));
  startup_context()->outgoing().debug_dir()->AddEntry("demo", debug_dir);
  filesystem_ = escher::HackFilesystem::New(debug_dir);

  if (!loop_) {
    loop_ = owned_loop_.get();
  }
}

void DemoHarnessFuchsia::InitWindowSystem() {}

vk::SurfaceKHR DemoHarnessFuchsia::CreateWindowAndSurface(
    const WindowParams& params) {
  VkMagmaSurfaceCreateInfoKHR create_info = {
      .sType = VK_STRUCTURE_TYPE_MAGMA_SURFACE_CREATE_INFO_KHR,
      .pNext = nullptr,
  };
  VkSurfaceKHR surface;
  VkResult err =
      vkCreateMagmaSurfaceKHR(instance(), &create_info, nullptr, &surface);
  FXL_CHECK(!err);
  return surface;
}

void DemoHarnessFuchsia::AppendPlatformSpecificInstanceExtensionNames(
    InstanceParams* params) {
  params->extension_names.insert(VK_KHR_SURFACE_EXTENSION_NAME);
  params->extension_names.insert(VK_KHR_MAGMA_SURFACE_EXTENSION_NAME);
}

void DemoHarnessFuchsia::ShutdownWindowSystem() {}

void DemoHarnessFuchsia::Run(Demo* demo) {
  FXL_CHECK(!demo_);
  demo_ = demo;
  async::PostTask(loop_->async(), [this] { this->RenderFrameOrQuit(); });
  loop_->Run();
}

void DemoHarnessFuchsia::RenderFrameOrQuit() {
  FXL_CHECK(demo_);  // Must be running.
  if (ShouldQuit()) {
    loop_->Quit();
    device().waitIdle();
  } else {
    demo_->MaybeDrawFrame();
    async::PostDelayedTask(loop_->async(),
                           [this] { this->RenderFrameOrQuit(); }, zx::msec(1));
  }
}
