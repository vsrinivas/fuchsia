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

typedef struct {
    int futex;
} mxr_mutex_t;

#define MXR_MUTEX_INIT ((mxr_mutex_t){0})

mx_status_t mxr_mutex_trylock(mxr_mutex_t* mutex);
mx_status_t mxr_mutex_timedlock(mxr_mutex_t* mutex, mx_time_t timeout);
void mxr_mutex_lock(mxr_mutex_t* mutex);
void mxr_mutex_unlock(mxr_mutex_t* mutex);

__END_CDECLS
