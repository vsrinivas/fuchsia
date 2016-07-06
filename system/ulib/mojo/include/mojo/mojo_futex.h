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

mojo_result_t mojo_futex_wait(int* value_ptr, int current_value, mojo_deadline_t timeout);
mojo_result_t mojo_futex_wake(int* value_ptr, uint32_t count);
mojo_result_t mojo_futex_requeue(int* wake_ptr, uint32_t wake_count, int current_value,
                                 int* requeue_ptr, uint32_t requeue_count);

__END_CDECLS;
