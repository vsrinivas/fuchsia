// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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