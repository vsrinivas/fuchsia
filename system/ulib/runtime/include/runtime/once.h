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

#include <system/compiler.h>

__BEGIN_CDECLS

typedef struct {
    int futex;
} mxr_once_t;

#define MXR_ONCE_INIT {0}

#pragma GCC visibility push(hidden)

// Calls the function exactly once across all threads using the same mxr_once_t.
void mxr_once(mxr_once_t*, void (*)(void));

#pragma GCC visibility pop

__END_CDECLS
