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

#ifndef PLATFORM_BUFFER_ABI_H
#define PLATFORM_BUFFER_ABI_H

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

struct PlatformBufferToken { int magic_; };

// TODO(MA-19) - define BackingStore
struct BackingStore { int magic_; };

int msd_platform_buffer_alloc(struct PlatformBufferToken** buffer_out, uint64_t size, uint64_t* size_out, uint32_t* handle_out);

void msd_platform_buffer_incref(struct PlatformBufferToken* buffer);

void msd_platform_buffer_decref(struct PlatformBufferToken* buffer);

void msd_platform_buffer_get_size(struct PlatformBufferToken* buffer, uint64_t* size_out);

void msd_platform_buffer_get_handle(struct PlatformBufferToken* buffer, uint32_t* handle_out);

int msd_platform_buffer_get_backing_store(struct PlatformBufferToken* buffer, BackingStore* backing_store);

int msd_platform_buffer_map(struct PlatformBufferToken* buffer, void** addr_out);

int msd_platform_buffer_unmap(struct PlatformBufferToken* buffer);

#if defined(__cplusplus)
}
#endif

#endif // PLATFORM_BUFFER_ABI_H
