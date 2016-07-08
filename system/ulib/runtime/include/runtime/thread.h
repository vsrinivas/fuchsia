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
#include <system/compiler.h>

__BEGIN_CDECLS

typedef int (*mxr_thread_entry_t)(void*);

typedef struct mxr_thread mxr_thread_t;

mx_status_t mxr_thread_create(mxr_thread_entry_t entry, void* arg, const char* name, mxr_thread_t** thread_out);
mx_status_t mxr_thread_join(mxr_thread_t* thread, int* return_value_out);
mx_status_t mxr_thread_detach(mxr_thread_t* thread);

// get magenta handle to thread or current thread if passed NULL
mx_handle_t mxr_thread_get_handle(mxr_thread_t* thread);

__END_CDECLS
