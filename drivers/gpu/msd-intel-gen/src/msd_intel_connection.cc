// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_connection.h"
#include "magma_util/dlog.h"

void msd_connection_close(msd_connection* connection)
{
    delete MsdIntelConnection::cast(connection);
}

msd_context* msd_connection_create_context(msd_connection* connection)
{
    DLOG("TODO: msd_connection_create_context");
    return nullptr;
}

void msd_context_destroy(msd_context* ctx) { DLOG("TODO: msd_device_destroy_context"); }
