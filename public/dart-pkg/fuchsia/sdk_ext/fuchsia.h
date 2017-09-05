// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DART_PKG_FUCHSIA_SDK_EXT_FUCHSIA_H_
#define DART_PKG_FUCHSIA_SDK_EXT_FUCHSIA_H_

#include "application/services/application_environment.fidl.h"
#include "application/services/service_provider.fidl.h"

namespace fuchsia {
namespace dart {

void Initialize(fidl::InterfaceHandle<app::ApplicationEnvironment> environment,
                fidl::InterfaceRequest<app::ServiceProvider> outgoing_services);

}  // namespace dart
}  // namespace fuchsia

#endif  // DART_PKG_FUCHSIA_SDK_EXT_FUCHSIA_H_
