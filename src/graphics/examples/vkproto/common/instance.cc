// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/examples/vkproto/common/instance.h"

#include <iostream>
#include <memory>
#include <vector>

#include "src/graphics/examples/vkproto/common/utils.h"

#include <vulkan/vulkan.hpp>

namespace {

const std::vector<const char *> s_required_props = {
    VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef __Fuchsia__
    VK_FUCHSIA_IMAGEPIPE_SURFACE_EXTENSION_NAME,
#endif
};

void PrintProps(const std::vector<const char *> &props, const char *msg) {
  printf("%s\n", msg);
  for (const auto &prop : props) {
    printf("\t%s\n", prop);
  }
  printf("\n");
}

#if USE_GLFW
std::vector<const char *> GetExtensionsGLFW(bool enable_validation) {
  uint32_t num_extensions = 0;
  const char **glfw_extensions;
  glfw_extensions = glfwGetRequiredInstanceExtensions(&num_extensions);
  std::vector<const char *> extensions(glfw_extensions, glfw_extensions + num_extensions);
  if (enable_validation) {
    extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  return extensions;
}
#else
std::vector<const char *> GetExtensionsPrivate(bool validation_layers_enabled) {
  std::vector<const char *> extensions;
#ifdef __Fuchsia__
  const char *kMagmaLayer = "VK_LAYER_FUCHSIA_imagepipe_swapchain_fb";
#else
  const char *kMagmaLayer = nullptr;
#endif

  std::vector<const char *> required_props = s_required_props;
  if (validation_layers_enabled) {
    required_props.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  if (FindRequiredProperties(s_required_props, vkp::INSTANCE_EXT_PROP, kMagmaLayer)) {
    extensions.insert(extensions.end(), required_props.begin(), required_props.end());
  }

  return extensions;
}
#endif

void AddRequiredExtensions(bool validation_layers_enabled, std::vector<const char *> *extensions) {
  std::vector<const char *> required_extensions;
#if USE_GLFW
  required_extensions = GetExtensionsGLFW(validation_layers_enabled_);
#else
  required_extensions = GetExtensionsPrivate(validation_layers_enabled);
#endif

  if (!required_extensions.empty())
    extensions->insert(extensions->end(), required_extensions.begin(), required_extensions.end());
}

}  // namespace

namespace vkp {

Instance::Instance(const vk::InstanceCreateInfo &instance_info, bool validation_layers_enabled,
                   std::vector<const char *> extensions, std::vector<const char *> layers,
                   vk::Optional<const vk::AllocationCallbacks> allocator)
    : instance_info_(instance_info),
      validation_layers_enabled_(validation_layers_enabled),
      extensions_(std::move(extensions)),
      layers_(std::move(layers)),
      allocator_(allocator) {}

Instance::Instance(Instance &&other) noexcept
    : initialized_(other.initialized_),
      instance_info_(other.instance_info_),
      validation_layers_enabled_(other.validation_layers_enabled_),
      extensions_(std::move(other.extensions_)),
      layers_(std::move(other.layers_)),
      allocator_(other.allocator_) {}

Instance::~Instance() {
  if (initialized_) {
    initialized_ = false;
  }
}

bool Instance::Init() {
  RTN_IF_MSG(false, (initialized_ == true), "Already initialized.\n");

  // Extensions
  AddRequiredExtensions(validation_layers_enabled_, &extensions_);

  // Require api version 1.1 if it hasn't been set.
  vk::ApplicationInfo app_info;
  app_info.apiVersion = VK_MAKE_VERSION(1, 1, 0);
  if (instance_info_.pApplicationInfo == nullptr) {
    instance_info_.pApplicationInfo = &app_info;
  } else if (instance_info_.pApplicationInfo->apiVersion == 0) {
    RTN_MSG(false,
            "Must set vk::ApplicationInfo::apiVersion when customizing vk::ApplicationInfo.\n");
  }

// Layers
#ifdef __Fuchsia__
  layers_.emplace_back("VK_LAYER_FUCHSIA_imagepipe_swapchain_fb");
#endif

  if (validation_layers_enabled_) {
    layers_.emplace_back("VK_LAYER_KHRONOS_validation");
  }

  if (!FindRequiredProperties(layers_, vkp::INSTANCE_LAYER_PROP))
    return false;

  if (instance_info_.ppEnabledLayerNames && instance_info_.enabledLayerCount) {
    // Tack on custom layers.
    std::copy(instance_info_.ppEnabledLayerNames,
              instance_info_.ppEnabledLayerNames + instance_info_.enabledLayerCount,
              std::back_inserter(layers_));
  }
  instance_info_.enabledLayerCount = static_cast<uint32_t>(layers_.size());
  instance_info_.ppEnabledLayerNames = layers_.data();

  if (instance_info_.ppEnabledExtensionNames && instance_info_.enabledExtensionCount) {
    // Tack on custom extensions.
    std::copy(instance_info_.ppEnabledExtensionNames,
              instance_info_.ppEnabledExtensionNames + instance_info_.enabledExtensionCount,
              std::back_inserter(extensions_));
  }
  instance_info_.enabledExtensionCount = static_cast<uint32_t>(extensions_.size());
  instance_info_.ppEnabledExtensionNames = extensions_.data();

  PrintProps(layers_, "Enabled Layers");
  PrintProps(extensions_, "Enabled Instance Extensions");

  vk::Instance *instance = new vk::Instance;
  vk::Result r_instance = vk::createInstance(&instance_info_, nullptr /* pAllocator */, instance);
  RTN_IF_VKH_ERR(false, r_instance, "Failed to create instance\n");
  instance_.reset(instance, [](vk::Instance *instance) {
    if (instance) {
      instance->destroy();
      delete instance;
    }
  });

  initialized_ = true;
  return true;
}

std::shared_ptr<vk::Instance> Instance::shared() {
  RTN_IF_MSG(nullptr, !initialized_, "Instance hasn't been initialized");
  return instance_;
}

const vk::Instance &Instance::get() const { return *instance_; }

Instance::Builder::Builder() : allocator_(nullptr) {}

Instance::Builder &Instance::Builder::set_allocator(
    const vk::Optional<const vk::AllocationCallbacks> &v) {
  allocator_ = v;
  return *this;
}

Instance::Builder &Instance::Builder::set_instance_info(const vk::InstanceCreateInfo &v) {
  instance_info_ = v;
  return *this;
}

Instance::Builder &Instance::Builder::set_validation_layers_enabled(bool v) {
  validation_layers_enabled_ = v;
  return *this;
}

Instance::Builder &Instance::Builder::set_extensions(std::vector<const char *> v) {
  extensions_ = std::move(v);
  return *this;
}

Instance::Builder &Instance::Builder::set_layers(std::vector<const char *> v) {
  layers_ = std::move(v);
  return *this;
}

std::unique_ptr<Instance> Instance::Builder::Unique() const {
  auto instance = std::make_unique<Instance>(instance_info_, validation_layers_enabled_,
                                             extensions_, layers_, allocator_);
  if (!instance->Init()) {
    RTN_MSG(nullptr, "Failed to initialize Instance.\n")
  }
  return instance;
}

std::shared_ptr<Instance> Instance::Builder::Shared() const {
  auto instance = std::make_shared<Instance>(instance_info_, validation_layers_enabled_,
                                             extensions_, layers_, allocator_);
  if (!instance->Init()) {
    RTN_MSG(nullptr, "Failed to initialize Instance.\n")
  }
  return instance;
}

Instance Instance::Builder::Build() const {
  vkp::Instance instance(instance_info_, validation_layers_enabled_, extensions_, layers_,
                         allocator_);
  if (!instance.Init()) {
    RTN_MSG(instance, "Failed to initialize Instance.\n")
  }
  return instance;
}

}  // namespace vkp
