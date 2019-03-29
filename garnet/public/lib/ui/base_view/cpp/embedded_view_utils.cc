// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/base_view/cpp/embedded_view_utils.h"

#include <lib/ui/scenic/cpp/view_token_pair.h>
#include "src/lib/fxl/logging.h"

namespace scenic {

EmbeddedViewInfo LaunchComponentAndCreateView(
    const fuchsia::sys::LauncherPtr& launcher, const std::string& component_url,
    const std::vector<std::string>& component_args) {
  FXL_DCHECK(launcher);

  auto [view_token, view_holder_token] = scenic::NewViewTokenPair();

  EmbeddedViewInfo info;

  launcher->CreateComponent(
      {.url = component_url,
       .arguments = fidl::VectorPtr(std::vector<std::string>(
           component_args.begin(), component_args.end())),
       .directory_request = info.app_services.NewRequest()},
      info.controller.NewRequest());

  info.app_services.ConnectToService(info.view_provider.NewRequest());

  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> services_to_child_view;
  info.services_to_child_view = services_to_child_view.NewRequest();

  info.view_provider->CreateView(std::move(view_token.value),
                                 info.services_from_child_view.NewRequest(),
                                 std::move(services_to_child_view));

  info.view_holder_token = std::move(view_holder_token);

  return info;
}

}  // namespace scenic
