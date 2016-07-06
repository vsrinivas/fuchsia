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

#include <mojo/mojo_types.h>
#include <system/compiler.h>

__BEGIN_CDECLS;

typedef int (*mojo_thread_start_routine)(void* arg);

mojo_result_t mojo_thread_create(mojo_thread_start_routine entry, void* arg,
                                 mojo_handle_t* out_handle, const char* name);
void mojo_thread_exit(void);
mojo_result_t mojo_thread_join(mojo_handle_t handle, mojo_deadline_t timeout);

__END_CDECLS;
