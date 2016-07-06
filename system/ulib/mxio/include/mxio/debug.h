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
#include <stdio.h>

// raw console printf, to go away before long
void cprintf(const char* fmt, ...);

// per-file chatty debug macro
#define xprintf(fmt...)   \
    do {                  \
        if (MXDEBUG) {    \
            cprintf(fmt); \
        }                 \
    } while (0)
