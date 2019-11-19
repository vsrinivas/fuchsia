// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_RESOURCE_CONTEXT_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_RESOURCE_CONTEXT_H_

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"

namespace scenic_impl {
namespace gfx {

class Session;

// Contains dependencies needed by various Resource subclasses. Used to decouple
// Resource from Engine; enables dependency injection in tests.
//
// The objects in ResourceContext must be guaranteed to have a lifecycle longer
// than Resource. For this reason, ResourceContext should not be passed from
// Resource to other classes.
struct ResourceContext {
  vk::Device vk_device;
  vk::PhysicalDevice vk_physical_device;
  vk::DispatchLoaderDynamic vk_loader;
  escher::VulkanDeviceQueues::Caps vk_device_queues_capabilities;
  escher::ResourceRecycler* escher_resource_recycler;
  escher::ImageFactory* escher_image_factory;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_RESOURCE_CONTEXT_H_
