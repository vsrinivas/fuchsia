// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/guest_manager/guest_services.h"

#include <lib/fidl/cpp/vector.h>
#include <lib/zx/channel.h>

GuestServices::GuestServices(fuchsia::guest::LaunchInfo launch_info)
    : launch_info_(std::move(launch_info)) {
  services_.AddService<fuchsia::guest::vmm::LaunchInfoProvider>(
      [this](fidl::InterfaceRequest<fuchsia::guest::vmm::LaunchInfoProvider>
                 request) { binding_.Bind(std::move(request)); });
}

fuchsia::sys::ServiceListPtr GuestServices::ServeDirectory() {
  auto services = fuchsia::sys::ServiceList::New();
  services->names.push_back(fuchsia::guest::vmm::LaunchInfoProvider::Name_);
  services->provider = services_.AddBinding();
  return services;
}

void GuestServices::GetLaunchInfo(GetLaunchInfoCallback callback) {
  callback(std::move(launch_info_));
}
