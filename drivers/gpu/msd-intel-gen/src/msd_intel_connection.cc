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

void msd_connection_destroy_context(msd_connection* connection, msd_context* ctx)
{
    DLOG("TODO: msd_device_destroy_context");
}
