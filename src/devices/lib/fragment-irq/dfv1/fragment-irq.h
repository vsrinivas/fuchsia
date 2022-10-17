// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/device.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/status.h>

namespace fragment_irq {

// Get an interrupt with name |fragment_name|.
zx::result<zx::interrupt> GetInterrupt(zx_device_t* dev, const char* fragment_name);

// Get interrupt index |which|. This will attempt to use fragments and FIDL to get the
// interrupt.
zx::result<zx::interrupt> GetInterrupt(zx_device_t* dev, uint32_t which);
}  // namespace fragment_irq
