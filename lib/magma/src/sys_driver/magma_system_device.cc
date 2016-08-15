// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system_device.h"
#include "magma_system_connection.h"
#include "magma_util/macros.h"

uint32_t MagmaSystemDevice::GetDeviceId() { return msd_device_get_id(msd_dev()); }

std::unique_ptr<MagmaSystemConnection> MagmaSystemDevice::Open(msd_client_id client_id)
{
    msd_connection* connection = msd_device_open(msd_dev(), client_id);
    if (!connection)
        return DRETP(nullptr, "msd_device_open failed");

    return std::unique_ptr<MagmaSystemConnection>(new MagmaSystemConnection(
        this, msd_connection_unique_ptr_t(connection, &msd_connection_close)));
}