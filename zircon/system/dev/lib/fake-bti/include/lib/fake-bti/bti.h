// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

zx_status_t fake_bti_create(zx_handle_t* out);
void fake_bti_destroy(zx_handle_t h);

__END_CDECLS
