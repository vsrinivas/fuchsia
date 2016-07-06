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

// TODO(jamesr): message pipe creation options.
mojo_result_t mojo_create_message_pipe(mojo_handle_t* handle0, mojo_handle_t* handle1);

mojo_result_t mojo_read_message(mojo_handle_t handle, void* bytes, uint32_t* num_bytes,
                                mojo_handle_t* handles, uint32_t* num_handles,
                                mojo_handle_signals_t flags);

mojo_result_t mojo_write_message(mojo_handle_t handle, const void* bytes, uint32_t num_bytes,
                                 const mojo_handle_t* handles, uint32_t num_handles, uint32_t flags);

__END_CDECLS;
