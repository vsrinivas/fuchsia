// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system_context.h"
#include "magma_system_connection.h"
#include "magma_system_device.h"
#include "magma_util/macros.h"

std::unique_ptr<MagmaSystemContext> MagmaSystemContext::Create(MagmaSystemConnection* connection)
{
    auto msd_ctx = msd_connection_create_context(connection->msd_connection());
    if (!msd_ctx)
        return DRETP(nullptr, "Failed to create msd context");

    // capture the connection here on the premise that the connection will always outlive
    // all of its contexts
    auto deleter = [connection](msd_context* msd_ctx) {
        msd_connection_destroy_context(connection->msd_connection(), msd_ctx);
    };

    return std::unique_ptr<MagmaSystemContext>(
        new MagmaSystemContext(msd_context_unique_ptr_t(msd_ctx, deleter)));
}