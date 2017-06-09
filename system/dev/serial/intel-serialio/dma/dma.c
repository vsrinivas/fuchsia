// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>

#include <magenta/types.h>

#include <intel-serialio/serialio.h>

mx_status_t intel_serialio_bind_dma(mx_device_t* dev) {
    // Not implemented yet.
    return MX_ERR_NOT_SUPPORTED;
}
