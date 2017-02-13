// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/run_component/application_loader_impl.h"

#include "lib/ftl/functional/make_copyable.h"

namespace component {

void ApplicationLoaderImpl::LoadApplication(
    const fidl::String& url,
    const ApplicationLoader::LoadApplicationCallback& callback_) {
  ApplicationLoader::LoadApplicationCallback callback(callback_);

  component_index_->GetComponent(
      url, [url, callback](
               ComponentManifestPtr component_manifest,
               fidl::InterfaceHandle<ComponentResources> resources_handle,
               network::NetworkErrorPtr error) {
        if (error) {
          FTL_LOG(ERROR) << "Failed to load component manifest for " << url;
          FTL_LOG(ERROR) << error->description << "(" << error->code << ")";
          callback(app::ApplicationPackage::New());
          return;
        }
        FTL_CHECK(component_manifest);

        if (!component_manifest->application) {
          FTL_LOG(ERROR) << "Component " << url << " not an application";
          callback(app::ApplicationPackage::New());
          return;
        }
        if (!component_manifest->resources) {
          FTL_LOG(ERROR) << "Component " << url << " doesn't have resources";
          callback(app::ApplicationPackage::New());
          return;
        }
        FTL_CHECK(resources_handle);

        auto resources =
            ComponentResourcesPtr::Create(std::move(resources_handle));

        resources->GetResource(
            component_manifest->application->resource, ftl::MakeCopyable([
              url, callback, resources = std::move(resources)
            ](mx::vmo data, network::NetworkErrorPtr error) {
              auto package = app::ApplicationPackage::New();
              if (error) {
                FTL_LOG(ERROR)
                    << "Failed to load component package for " << url;
                FTL_LOG(ERROR)
                    << error->description << "(" << error->code << ")";
              } else {
                package->data = std::move(data);
              }
              callback(std::move(package));
            }));
      });
}

}  // namespace component
