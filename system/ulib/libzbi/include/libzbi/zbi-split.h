// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "zbi.h"

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Check the complete ZBI in the VMO and split it into kernel and data parts.
// The original VMO is unmodified but the handle is always consumed.
zbi_result_t zbi_split_complete(zx_handle_t zbi_vmo,
                                zx_handle_t* kernel_vmo,
                                zx_handle_t* data_vmo);

__END_CDECLS
