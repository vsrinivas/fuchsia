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

    return std::unique_ptr<MagmaSystemContext>(
        new MagmaSystemContext(msd_context_unique_ptr_t(msd_ctx, &msd_context_destroy)));
}