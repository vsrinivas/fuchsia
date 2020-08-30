// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ISOLATED_DEVMGR_V2_COMPONENT_BIND_DEVFS_TO_NAMESPACE_H_
#define SRC_LIB_ISOLATED_DEVMGR_V2_COMPONENT_BIND_DEVFS_TO_NAMESPACE_H_

#include <lib/zx/status.h>
#include <zircon/types.h>

namespace isolated_devmgr {

// Performs one-time set up for the isolated-devmgr, including a call to BindDevfsToNamespace.
zx::status<> OneTimeSetUp();

// Binds /dev from the isolated-devmgr to the current namespace.
zx::status<> BindDevfsToNamespace();

}  // namespace isolated_devmgr

#endif  // SRC_LIB_ISOLATED_DEVMGR_V2_COMPONENT_BIND_DEVFS_TO_NAMESPACE_H_
