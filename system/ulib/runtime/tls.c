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

#include <runtime/tls.h>

#include <runtime/atomic.h>
#include <runtime/process.h>

mxr_tls_t mxr_tls_allocate(void) {
    mxr_tls_t* next_slot = &mxr_process_get_info()->next_tls_slot;
    mxr_tls_t slot = atomic_add_uint32(next_slot, 1);
    if (slot < MXR_TLS_SLOT_MAX)
        return slot;
    atomic_store_uint32(next_slot, MXR_TLS_SLOT_MAX);
    return MXR_TLS_SLOT_INVALID;
}
