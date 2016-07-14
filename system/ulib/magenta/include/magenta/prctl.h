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

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__x86_64__)

enum { ARCH_SET_FS = 0,
       ARCH_GET_FS = 1,
       ARCH_SET_GS = 2,
       ARCH_GET_GS = 3 };

#elif defined(__aarch64__)

enum {
    ARCH_SET_TPIDRRO_EL0 = 0,
};

#elif defined(__arm__)

enum {
    ARCH_SET_CP15_READONLY = 0,
};

#else
#error "need to define PRCTL enum for your architecture"
#endif

#ifdef __cplusplus
}
#endif
