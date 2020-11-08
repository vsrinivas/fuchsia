// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_NETSVC_ZBI_H_
#define SRC_BRINGUP_BIN_NETSVC_ZBI_H_

#include <lib/zx/vmo.h>
#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <string_view>

zx_status_t netboot_prepare_zbi(zx::vmo nbkernel, zx::vmo nbdata, std::string_view cmdline,
                                zx::vmo* kernel_zbi, zx::vmo* data_zbi);

#endif  // SRC_BRINGUP_BIN_NETSVC_ZBI_H_
