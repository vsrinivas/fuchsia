// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trait_updater_delegate_impl.h"
#include "src/connectivity/weave/adaptation/configuration_manager_delegate_impl.h"

#include <weave/trait/description/DeviceIdentityTrait.h>

namespace nl::Weave::DeviceLayer {
namespace {
using Profiles::DataManagement_Current::ResourceIdentifier;
const ResourceIdentifier self_res_id(ResourceIdentifier::RESOURCE_TYPE_RESERVED,
                                     ResourceIdentifier::SELF_NODE_ID);
}  // namespace

WEAVE_ERROR TraitUpdaterDelegateImpl::Init() {
  std::vector<std::string> applets_list;
  WEAVE_ERROR err = ConfigurationMgrImpl().GetAppletPathList(applets_list);
  if (err == WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND) {
    return WEAVE_NO_ERROR;
  }
  if (err != WEAVE_NO_ERROR) {
    return err;
  }
  return InitApplets(applets_list);
}

WEAVE_ERROR TraitUpdaterDelegateImpl::InitApplets(std::vector<std::string>& names) {
  if (names.size() == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  for (auto it = names.begin(); it != names.end(); it++) {
    std::unique_ptr<weavestack::applets::AppletsLoader> m;
    // Create the module and retrieve the loader.
    zx_status_t status = weavestack::applets::AppletsLoader::CreateWithModule(it->c_str(), &m);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to load module " << it->c_str();
      return status;
    }
    // Use the loader and call CreateApplet.
    weavestack::applets::Applet a = m->CreateApplet(callbacks_);

    // check if applet is true or valid.
    applets_loader_.push_back(std::move(m));
    applets_.push_back(std::move(a));
  }
  return ZX_OK;
}

void TraitUpdaterDelegateImpl::HandleWeaveDeviceEvent(const WeaveDeviceEvent* event) {
  if (!event) {
    return;
  }

  for (auto it = applets_.begin(); it != applets_.end(); it++) {
    it->HandleEvent(event);
  }
}

}  // namespace nl::Weave::DeviceLayer
