// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

typedef struct pciroot_protocol_ops {
} pciroot_protocol_ops_t;

typedef struct pciroot_protocol {
    pciroot_protocol_ops_t* ops;
    void* ctx;
} pciroot_protocol_t;

__END_CDECLS;
