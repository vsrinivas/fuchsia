// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/ui/base_view/embedded_view_utils.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

namespace scenic {

EmbeddedViewInfo LaunchComponentAndCreateView(const fuchsia::sys::LauncherPtr& launcher,
                                              const std::string& component_url,
                                              const std::vector<std::string>& component_args) {
  FX_DCHECK(launcher);

  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  EmbeddedViewInfo info;

  // Configure the information to launch the component with.
  fuchsia::sys::LaunchInfo launch_info;
  info.app_services = sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);
  launch_info.url = component_url;
  launch_info.arguments =
      fidl::VectorPtr(std::vector<std::string>(component_args.begin(), component_args.end()));

  launcher->CreateComponent(std::move(launch_info), info.controller.NewRequest());

  info.view_provider = info.app_services->Connect<fuchsia::ui::app::ViewProvider>();

  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> services_to_child_view;
  info.services_to_child_view = services_to_child_view.NewRequest();

  info.view_provider->CreateView(std::move(view_token.value),
                                 info.services_from_child_view.NewRequest(),
                                 std::move(services_to_child_view));

  info.view_holder_token = std::move(view_holder_token);

  return info;
}

}  // namespace scenic
