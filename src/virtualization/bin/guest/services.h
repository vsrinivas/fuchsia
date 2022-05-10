// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_GUEST_SERVICES_H_
#define SRC_VIRTUALIZATION_BIN_GUEST_SERVICES_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/zx/status.h>

#include "sdk/lib/sys/cpp/component_context.h"

// Create a connection to the fuchsia.virtualization.Manager service.
zx::status<fuchsia::virtualization::ManagerSyncPtr> ConnectToManager(
    sys::ComponentContext* context);

#endif  // SRC_VIRTUALIZATION_BIN_GUEST_SERVICES_H_
