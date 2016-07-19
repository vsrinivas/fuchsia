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

#ifndef MOJO_PUBLIC_C_BINDINGS_STRUCT_H_
#define MOJO_PUBLIC_C_BINDINGS_STRUCT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Validates that a given buffer has a mojo struct header and that the size of
// the struct in the header matches the size of the buffer.
bool mojo_validate_struct_header(const void* data, size_t size);

typedef struct struct_header {
    uint32_t num_bytes;
    uint32_t version;
} mojo_struct_header_t;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // MOJO_PUBLIC_C_BINDINGS_STRUCT_H_
