// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_COMPONENT_MANAGER_COMPONENT_RESOURCES_IMPL_H_
#define PERIDOT_BIN_COMPONENT_MANAGER_COMPONENT_RESOURCES_IMPL_H_

#include <utility>

#include "lib/component/fidl/component.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/network/fidl/network_service.fidl.h"
#include "peridot/bin/component_manager/resource_loader.h"

namespace component {

class ComponentResourcesImpl : public ComponentResources {
 public:
  ComponentResourcesImpl(fidl::Map<fidl::String, fidl::String> resource_urls,
                         std::shared_ptr<ResourceLoader> resource_loader)
      : resource_urls_(std::move(resource_urls)),
        resource_loader_(std::move(resource_loader)) {}
  void GetResourceNames(const GetResourceNamesCallback& callback) override;
  void GetResourceURLs(const GetResourceURLsCallback& callback) override;
  void GetResource(const fidl::String& resource_name,
                   const GetResourceCallback& callback_) override;

 private:
  fidl::Map<fidl::String, fidl::String> resource_urls_;
  std::shared_ptr<ResourceLoader> resource_loader_;
};

}  // namespace component

#endif  // PERIDOT_BIN_COMPONENT_MANAGER_COMPONENT_RESOURCES_IMPL_H_
