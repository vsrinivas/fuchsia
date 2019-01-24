// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

zx_status_t netboot_prepare_zbi(zx_handle_t nbkernel_vmo, zx_handle_t nbbootdata_vmo,
                                const uint8_t* cmdline, uint32_t cmdline_size, zx_handle_t* kernel,
                                zx_handle_t* bootdata);
