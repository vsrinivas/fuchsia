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

#pragma GCC visibility push(hidden)

#include <magenta/types.h>

void print(mx_handle_t log, const char* s, ...) __attribute__((sentinel));
_Noreturn void fail(mx_handle_t log, mx_status_t status, const char* msg);

static inline void check(mx_handle_t log,
                         mx_status_t status, const char* msg) {
    if (status != NO_ERROR)
        fail(log, status, msg);
}

#pragma GCC visibility pop
