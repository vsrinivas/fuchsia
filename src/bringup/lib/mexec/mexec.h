// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_LIB_MEXEC_MEXEC_H_
#define SRC_BRINGUP_LIB_MEXEC_MEXEC_H_

#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

namespace mexec {

// Given an mexec-privileged resource, this method prepares the desired data
// ZBI to be passed to `zx_system_mexec()`: it is extended with the
// system-specified items given by `zx_system_mexec_payload_get()`, as well as
// a SECURE_ENTROPY item for good measure.
//
// Returns
// * ZX_ERR_IO_DATA_INTEGRITY: if any ZBI format or storage access errors are
//   encountered;
// * any status returned by `zx_system_mexec_payload_get()`.
//
zx_status_t PrepareDataZbi(zx::unowned_resource resource, zx::unowned_vmo data_zbi);

}  // namespace mexec

#endif  // SRC_BRINGUP_LIB_MEXEC_MEXEC_H_
