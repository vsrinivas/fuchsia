// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/profile.h>

#include <zircon/syscalls.h>
#include <zircon/syscalls/profile.h>

namespace zx {

zx_status_t profile::create(const resource& resource, const zx_profile_info_t* info, profile* result) {
    return zx_profile_create(resource.get(), info, result->reset_and_get_address());
}

} // namespace zx
