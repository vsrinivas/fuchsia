// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/component/cpp/environment_services_helper.h"

#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>

#include "lib/component/cpp/startup_context.h"

namespace component {

std::shared_ptr<component::Services> GetEnvironmentServices() {
  auto services = std::make_shared<component::Services>();
  services->Bind(subtle::CreateStaticServiceRootHandle());
  return services;
}

}  // namespace component
