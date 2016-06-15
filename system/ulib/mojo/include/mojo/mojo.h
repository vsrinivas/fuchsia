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

#include <runtime/compiler.h>
#include "mojo_types.h"

__BEGIN_CDECLS;

mojo_result_t mojo_wait(const mojo_handle_t* handles, const mojo_handle_signals_t* signals,
                        uint32_t num_handles, uint32_t* result_index, mojo_deadline_t deadline,
                        mojo_handle_signals_t* satisfied_signals,
                        mojo_handle_signals_t* satisfiable_signals);

mojo_result_t mojo_close(mojo_handle_t handle);

void mojo_exit(int ec) __attribute__((noreturn));

// returns elapsed microseconds since boot
uint64_t mojo_current_time(void);

__END_CDECLS;
