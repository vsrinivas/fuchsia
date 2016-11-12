// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_connection.h"
#include "magma_util/dlog.h"
#include <errno.h>

void msd_connection_close(msd_connection* connection)
{
    delete MsdIntelAbiConnection::cast(connection);
}

msd_context* msd_connection_create_context(msd_connection* connection)
{
    // Backing store creation deferred until context is used.
    // TODO(MA-71) pass a reference to the connection's ppgtt
    return new MsdIntelAbiContext(
        std::make_unique<ClientContext>(MsdIntelAbiConnection::cast(connection)->ptr(), nullptr));
}

int32_t msd_connection_wait_rendering(struct msd_connection* connection, struct msd_buffer* buf)
{
    if (!MsdIntelAbiConnection::cast(connection)
             ->ptr()
             ->WaitRendering((MsdIntelAbiBuffer::cast(buf)->ptr())))
        return DRET(-ETIMEDOUT);
    return 0;
}
