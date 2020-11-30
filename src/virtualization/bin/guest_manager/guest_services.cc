// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/guest_manager/guest_services.h"

#include <lib/fidl/cpp/vector.h>
#include <lib/zx/channel.h>

GuestServices::GuestServices(fuchsia::virtualization::LaunchInfo launch_info)
    : launch_info_(std::move(launch_info)) {
  services_.AddService<fuchsia::virtualization::LaunchInfoProvider>(
      [this](fidl::InterfaceRequest<fuchsia::virtualization::LaunchInfoProvider> request) {
        binding_.Bind(std::move(request));
      });
}

fuchsia::sys::ServiceListPtr GuestServices::ServeDirectory() {
  auto services = fuchsia::sys::ServiceList::New();
  services->names.push_back(fuchsia::virtualization::LaunchInfoProvider::Name_);
  services->provider = services_.AddBinding();
  return services;
}

void GuestServices::GetLaunchInfo(GetLaunchInfoCallback callback) {
  callback(std::move(launch_info_));
}
