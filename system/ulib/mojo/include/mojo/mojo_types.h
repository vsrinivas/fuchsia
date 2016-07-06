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
#include <stddef.h>
#include <stdint.h>

__BEGIN_CDECLS

#ifdef _KERNEL
#error "Mojo header files should only be used in userspace"
#endif

typedef uint32_t mojo_handle_t;
#define MOJO_HANDLE_INVALID ((mojo_handle_t)0)

typedef uint32_t mojo_result_t;
// The following defines must match the MOJO_RESULT_* defines in Mojo's system/types.h
#define MOJO_RESULT_OK ((mojo_result_t)0)
#define MOJO_RESULT_CANCELLED ((mojo_result_t)1)
#define MOJO_RESULT_UNKNOWN ((mojo_result_t)2)
#define MOJO_RESULT_INVALID_ARGUMENT ((mojo_result_t)3)
#define MOJO_RESULT_DEADLINE_EXCEEDED ((mojo_result_t)4)
#define MOJO_RESULT_NOT_FOUND ((mojo_result_t)5)
#define MOJO_RESULT_ALREADY_EXISTS ((mojo_result_t)6)
#define MOJO_RESULT_PERMISSION_DENIED ((mojo_result_t)7)
#define MOJO_RESULT_RESOURCE_EXHAUSTED ((mojo_result_t)8)
#define MOJO_RESULT_FAILED_PRECONDITION ((mojo_result_t)9)
#define MOJO_RESULT_ABORTED ((mojo_result_t)10)
#define MOJO_RESULT_OUT_OF_RANGE ((mojo_result_t)11)
#define MOJO_RESULT_UNIMPLEMENTED ((mojo_result_t)12)
#define MOJO_RESULT_INTERNAL ((mojo_result_t)13)
#define MOJO_RESULT_UNAVAILABLE ((mojo_result_t)14)
#define MOJO_RESULT_DATA_LOSS ((mojo_result_t)15)
#define MOJO_RESULT_BUSY ((mojo_result_t)16)
#define MOJO_RESULT_SHOULD_WAIT ((mojo_result_t)17)

typedef uint64_t mojo_deadline_t;
#define MOJO_DEADLINE_INDEFINITE ((mojo_deadline_t)-1)

typedef uint32_t mojo_handle_signals_t;
#define MOJO_HANDLE_SIGNAL_NONE ((mojo_handle_signals_t)0u)
#define MOJO_HANDLE_SIGNAL_READABLE ((mojo_handle_signals_t)1 << 0u)
#define MOJO_HANDLE_SIGNAL_WRITABLE ((mojo_handle_signals_t)1 << 1u)
#define MOJO_HANDLE_SIGNAL_PEER_CLOSED ((mojo_handle_signals_t)1 << 2u)
#define MOJO_HANDLE_SIGNAL_SIGNALED ((mojo_handle_signals_t)1 << 3u)

typedef uint32_t mojo_event_options_t;
#define MOJO_EVENT_INITALLY_SIGNALED ((mojo_event_options_t)1 << 0u)

__END_CDECLS
