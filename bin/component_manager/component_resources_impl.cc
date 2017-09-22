// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/component_manager/component_resources_impl.h"

#include "lib/fxl/functional/make_copyable.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fsl/vmo/strings.h"

namespace component {

void ComponentResourcesImpl::GetResourceNames(
    const GetResourceNamesCallback& callback) {
  fidl::Array<fidl::String> resource_names;
  for (auto i = resource_urls_.cbegin(); i != resource_urls_.cend(); ++i) {
    resource_names.push_back(i.GetKey());
  }
  callback(std::move(resource_names));
}

void ComponentResourcesImpl::GetResourceURLs(
    const GetResourceURLsCallback& callback) {
  callback(resource_urls_.Clone());
}

void ComponentResourcesImpl::GetResource(const fidl::String& resource_name,
                                         const GetResourceCallback& callback_) {
  const GetResourceCallback& callback(callback_);

  auto i = resource_urls_.find(resource_name);
  if (i == resource_urls_.end()) {
    FXL_LOG(ERROR) << "Requested invalid resource " << resource_name;
    auto error = network::NetworkError::New();
    error->code = 404;
    error->description = "Not Found";
    callback(zx::vmo(), std::move(error));
    return;
  }
  const std::string& url = i.GetValue();

  resource_loader_->LoadResource(
      url, [callback](zx::vmo vmo, network::NetworkErrorPtr error) {
        // TODO(ianloic): can the callback be called directly?
        callback(std::move(vmo), std::move(error));
      });
}

}  // namespace component
