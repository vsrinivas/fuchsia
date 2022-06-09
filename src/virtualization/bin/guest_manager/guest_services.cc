// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/guest_manager/guest_services.h"

#include <lib/fidl/cpp/vector.h>
#include <lib/zx/channel.h>

GuestServices::GuestServices(fuchsia::virtualization::GuestConfig cfg) : cfg_(std::move(cfg)) {
  services_.AddPublicService<fuchsia::virtualization::GuestConfigProvider>(
      bindings_.GetHandler(this));
}

fuchsia::sys::ServiceListPtr GuestServices::ServeDirectory() {
  auto services = std::make_unique<fuchsia::sys::ServiceList>();
  services->names.emplace_back(fuchsia::virtualization::GuestConfigProvider::Name_);
  services_.Serve(services->provider.NewRequest().TakeChannel());
  return services;
}

void GuestServices::Get(GetCallback callback) { callback(std::move(cfg_)); }
