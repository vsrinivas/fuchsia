// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/device/ethernet.h>
#include <magenta/types.h>

__BEGIN_CDECLS;

mx_status_t eth_ioring_create(size_t entries, size_t entry_size,
                              eth_ioring_t* cli, eth_ioring_t* srv);
void eth_ioring_destroy(eth_ioring_t* ioring);

__END_CDECLS;
