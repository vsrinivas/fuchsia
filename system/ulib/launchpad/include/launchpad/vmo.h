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

#pragma once

#include <magenta/types.h>
#include <stddef.h>
#include <system/compiler.h>

__BEGIN_CDECLS

mx_handle_t launchpad_vmo_from_mem(const void* data, size_t len);

// These functions return ERR_IO to indicate an error in the POSIXish
// underlying calls, meaning errno has been set with a POSIX-style error.
// Other errors are verbatim from the mx_vm_object_* calls.
mx_handle_t launchpad_vmo_from_fd(int fd);
mx_handle_t launchpad_vmo_from_file(const char* filename);

__END_CDECLS
