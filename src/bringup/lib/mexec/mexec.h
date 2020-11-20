// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_LIB_MEXEC_MEXEC_H_
#define SRC_BRINGUP_LIB_MEXEC_MEXEC_H_

#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

namespace mexec {

// Boot performs an mexec (see
// https://fuchsia.dev/fuchsia-src/reference/syscalls/system_mexec), given a
// resource conferring that privilege, a channel connected to an service
// implementing fuchsia.device.manager.Administrator (to ensure an orderly
// shutdown of devices), and kernel and data ZBIs (as described by the
// protocol's documentation).
zx_status_t Boot(zx::resource resource, zx::channel devmgr_channel, zx::vmo kernel_zbi,
                 zx::vmo data_zbi);

}  // namespace mexec

#endif  // SRC_BRINGUP_LIB_MEXEC_MEXEC_H_
