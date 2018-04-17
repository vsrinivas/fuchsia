// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/macros.h"
#include "msd.h"

msd_connection_t* msd_device_open(msd_device_t* dev, msd_client_id_t client_id)
{
    return DRETP(nullptr, "not implemented");
}

void msd_device_destroy(msd_device_t* dev) {}

magma_status_t msd_device_query(msd_device_t* device, uint64_t id, uint64_t* value_out)
{
    return MAGMA_STATUS_UNIMPLEMENTED;
}

void msd_device_dump_status(msd_device_t* device) {}

magma_status_t msd_device_display_get_size(msd_device_t* dev, magma_display_size* size_out)
{
    return MAGMA_STATUS_UNIMPLEMENTED;
}
