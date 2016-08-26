// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_connection.h"
#include "magma_util/dlog.h"

void msd_connection_close(msd_connection* connection)
{
    delete MsdIntelAbiConnection::cast(connection);
}

msd_context* msd_connection_create_context(msd_connection* connection)
{
    auto context = MsdIntelAbiConnection::cast(connection)->ptr()->CreateContext();
    if (!context)
        return DRETP(nullptr, "MsdIntelConnection::CreateContext failed");
    return new MsdIntelAbiContext(std::move(context));
}
