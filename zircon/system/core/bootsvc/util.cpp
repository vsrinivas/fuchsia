// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <zircon/process.h>
#include <zircon/processargs.h>

namespace bootsvc {

const char* const kLastPanicFilePath = "log/last-panic.txt";

fbl::Vector<zx::vmo> RetrieveBootdata() {
    fbl::Vector<zx::vmo> vmos;
    zx::vmo vmo;
    for (unsigned n = 0;
         vmo.reset(zx_take_startup_handle(PA_HND(PA_VMO_BOOTDATA, n))), vmo.is_valid();
         n++) {
        vmos.push_back(std::move(vmo));
    }
    return vmos;
}

} // namespace bootsvc
