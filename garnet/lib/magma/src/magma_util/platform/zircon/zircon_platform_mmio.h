// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "platform_mmio.h"
#include <ddk/mmio-buffer.h>

namespace magma {

class ZirconPlatformMmio : public PlatformMmio {
public:
    ZirconPlatformMmio(mmio_buffer_t mmio);

    ~ZirconPlatformMmio();

private:
    mmio_buffer_t mmio_;
};

} // namespace magma
