// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/escher/common/demo_harness_fuchsia.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/zx/time.h>

#include <memory>

#include <hid/usages.h>

#include "lib/vfs/cpp/pseudo_dir.h"
#include "src/ui/examples/escher/common/demo.h"
#include "src/ui/lib/escher/util/trace_macros.h"

namespace {

class DemoKeyDispatcher : public fuchsia::ui::input::InputDevice {
 public:
  using Callback = fit::function<void(std::string)>;

  DemoKeyDispatcher(Callback callback) : callback_(std::move(callback)) {}

 private:
  // |fuchsia::ui::input::InputDevice|
  void DispatchReport(fuchsia::ui::input::InputReport report) override {
    if (report.keyboard) {
      DispatchDelta(std::move(report.keyboard->pressed_keys));
    }
  }

  // Dispatch only keys that are newly pressed.
  void DispatchDelta(std::vector<uint32_t> pressed_keys) {
    for (uint32_t key : pressed_keys) {
      // Since this is a demo harness, we can assume a small number of pressed
      // keys. However, if this assumption breaks, we can switch to std::bitset.
      if (std::find(pressed_keys_.begin(), pressed_keys_.end(), key) == pressed_keys_.end()) {
        DispatchKey(key);
      }
    }
    pressed_keys_ = std::move(pressed_keys);
  }

  void DispatchKey(uint32_t hid) {
    if (hid >= HID_USAGE_KEY_A && hid <= HID_USAGE_KEY_Z) {
      callback_(std::string(1, 'A' + hid - HID_USAGE_KEY_A));
    } else if (hid >= HID_USAGE_KEY_1 && hid <= HID_USAGE_KEY_9) {
      callback_(std::string(1, '1' + hid - HID_USAGE_KEY_1));
    } else {
      switch (hid) {
        // Unlike ASCII, HID_USAGE_KEY_0 comes after 9.
        case HID_USAGE_KEY_0:
          callback_("0");
          break;
        case HID_USAGE_KEY_ENTER:
        case HID_USAGE_KEY_KP_ENTER:
          callback_("RETURN");
          break;
        case HID_USAGE_KEY_ESC:
          callback_("ESCAPE");
          break;
        case HID_USAGE_KEY_SPACE:
          callback_("SPACE");
          break;
        default:
          break;
      }
    }
  }

  Callback callback_;
  std::vector<uint32_t> pressed_keys_;
};

}  // namespace

// When running on Fuchsia, New() instantiates a DemoHarnessFuchsia.
std::unique_ptr<DemoHarness> DemoHarness::New(DemoHarness::WindowParams window_params,
                                              DemoHarness::InstanceParams instance_params) {
  auto harness = new DemoHarnessFuchsia(nullptr, window_params);
  harness->Init(std::move(instance_params));
  return std::unique_ptr<DemoHarness>(harness);
}

DemoHarnessFuchsia::DemoHarnessFuchsia(async::Loop* loop, WindowParams window_params)
    : DemoHarness(window_params),
      loop_(loop),
      owned_loop_(loop_ ? nullptr : new async::Loop(&kAsyncLoopConfigAttachToCurrentThread)),
      trace_provider_((loop_ ? loop_ : owned_loop_.get())->dispatcher()),
      component_context_(sys::ComponentContext::Create()),
      input_reader_(this) {
  // Provide a PseudoDir where the demo can register debugging services.
  auto debug_dir = std::make_shared<vfs::PseudoDir>();
  component_context()->outgoing()->debug_dir()->AddSharedEntry("demo", debug_dir);
  filesystem_ = escher::HackFilesystem::New(debug_dir);

  if (!loop_) {
    loop_ = owned_loop_.get();
  }
}

void DemoHarnessFuchsia::InitWindowSystem() { input_reader_.Start(); }

vk::SurfaceKHR DemoHarnessFuchsia::CreateWindowAndSurface(const WindowParams& params) {
  VkImagePipeSurfaceCreateInfoFUCHSIA create_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGEPIPE_SURFACE_CREATE_INFO_FUCHSIA,
      .pNext = nullptr,
  };
  VkSurfaceKHR surface;
  VkResult err = vkCreateImagePipeSurfaceFUCHSIA(instance(), &create_info, nullptr, &surface);
  FXL_CHECK(!err);
  return surface;
}

void DemoHarnessFuchsia::AppendPlatformSpecificInstanceExtensionNames(InstanceParams* params) {
  params->extension_names.insert(VK_KHR_SURFACE_EXTENSION_NAME);
  params->extension_names.insert(VK_FUCHSIA_IMAGEPIPE_SURFACE_EXTENSION_NAME);
  params->extension_names.insert(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
  params->extension_names.insert(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
  params->layer_names.insert("VK_LAYER_FUCHSIA_imagepipe_swapchain_fb");
}

void DemoHarnessFuchsia::AppendPlatformSpecificDeviceExtensionNames(std::set<std::string>* names) {
  names->insert(VK_FUCHSIA_EXTERNAL_MEMORY_EXTENSION_NAME);
}

void DemoHarnessFuchsia::ShutdownWindowSystem() {}

void DemoHarnessFuchsia::RunForPlatform(Demo* demo) {
  async::PostTask(loop_->dispatcher(), [this, demo] { this->RenderFrameOrQuit(demo); });
  loop_->Run();
}

void DemoHarnessFuchsia::RegisterDevice(
    fuchsia::ui::input::DeviceDescriptor descriptor,
    fidl::InterfaceRequest<fuchsia::ui::input::InputDevice> input_device) {
  if (descriptor.keyboard) {
    input_devices_.AddBinding(std::make_unique<DemoKeyDispatcher>([this](std::string key) {
                                this->HandleKeyPress(std::move(key));
                              }),
                              std::move(input_device));
  }
}

void DemoHarnessFuchsia::RenderFrameOrQuit(Demo* demo) {
  if (ShouldQuit()) {
    loop_->Quit();
    device().waitIdle();
  } else {
    MaybeDrawFrame();
    async::PostDelayedTask(
        loop_->dispatcher(), [this, demo] { this->RenderFrameOrQuit(demo); }, zx::msec(1));
  }
}
