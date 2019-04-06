// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_A11Y_A11Y_MANAGER_UTIL_H_
#define GARNET_BIN_A11Y_A11Y_MANAGER_UTIL_H_

#include <lib/fsl/handles/object_info.h>
#include <lib/zx/event.h>

namespace a11y_manager {

// Utility function to extract Koid from an event.
zx_koid_t GetKoid(const zx::object_base& object);

}  // namespace a11y_manager

#endif  // GARNET_BIN_A11Y_A11Y_MANAGER_UTIL_H_
