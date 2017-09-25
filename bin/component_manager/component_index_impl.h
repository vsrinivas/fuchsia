// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_COMPONENT_INDEX_IMPL_H_
#define APPS_COMPONENT_INDEX_IMPL_H_

#include "lib/component/fidl/component.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/network/fidl/network_service.fidl.h"
#include "peridot/bin/component_manager/component_resources_impl.h"
#include "peridot/bin/component_manager/resource_loader.h"

namespace component {

class ComponentIndexImpl : public ComponentIndex {
 public:
  explicit ComponentIndexImpl(network::NetworkServicePtr network_service);

  void GetComponent(const fidl::String& component_id_,
                    const GetComponentCallback& callback_) override;

  void FindComponentManifests(
      fidl::Map<fidl::String, fidl::String> filter_fidl,
      const FindComponentManifestsCallback& callback) override;

 private:
  void LoadComponentIndex(const std::string& contents, const std::string& path);

  std::shared_ptr<ResourceLoader> resource_loader_;

  // A list of component URIs that are installed locally.
  std::vector<std::string> local_index_;

  fidl::BindingSet<ComponentResources, std::unique_ptr<ComponentResourcesImpl>>
      resources_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ComponentIndexImpl);
};

}  // namespace component

#endif  // APPS_COMPONENT_INDEX_IMPL_H_
