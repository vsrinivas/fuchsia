// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <magenta/types.h>
#include <sys/types.h>

// collection of utility routines for virtio

namespace virtio {

// helper routine to create a contiguous vmo, map it, and return the virtual and physical address
mx_status_t map_contiguous_memory(size_t size, uintptr_t* va, mx_paddr_t* pa);

} // namespace virtio
