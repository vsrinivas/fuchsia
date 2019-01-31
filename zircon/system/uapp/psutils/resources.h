// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Returns a new handle to the root resource, which the caller
// is responsible for closing.
// See docs/objects/resource.md
zx_status_t get_root_resource(zx_handle_t* root_resource);

__END_CDECLS
