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

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ask clang format not to mess up the indentation:
// clang-format off

typedef int32_t mx_handle_t;
#define MX_HANDLE_INVALID         ((mx_handle_t)0)

// Same as kernel status_t
typedef int32_t mx_status_t;

// time in nanoseconds
typedef uint64_t mx_time_t;
#define MX_TIME_INFINITE UINT64_MAX

typedef uint32_t mx_signals_t;
#define MX_SIGNAL_NONE            ((mx_signals_t)0u)
#define MX_SIGNAL_READABLE        ((mx_signals_t)1u << 0)
#define MX_SIGNAL_WRITABLE        ((mx_signals_t)1u << 1)
#define MX_SIGNAL_PEER_CLOSED     ((mx_signals_t)1u << 2)
#define MX_SIGNAL_SIGNALED        ((mx_signals_t)1u << 3)

#define MX_SIGNAL_USER0           ((mx_signals_t)1u << 4)
#define MX_SIGNAL_USER1           ((mx_signals_t)1u << 5)
#define MX_SIGNAL_USER2           ((mx_signals_t)1u << 6)
#define MX_SIGNAL_USER3           ((mx_signals_t)1u << 7)
#define MX_SIGNAL_USER_ALL        ((mx_signals_t)15u << 4)

typedef struct {
    mx_signals_t satisfied;
    mx_signals_t satisfiable;
} mx_signals_state_t;

typedef uint32_t mx_rights_t;
#define MX_RIGHT_NONE             ((mx_rights_t)0u)
#define MX_RIGHT_DUPLICATE        ((mx_rights_t)1u << 0)
#define MX_RIGHT_TRANSFER         ((mx_rights_t)1u << 1)
#define MX_RIGHT_READ             ((mx_rights_t)1u << 2)
#define MX_RIGHT_WRITE            ((mx_rights_t)1u << 3)
#define MX_RIGHT_EXECUTE          ((mx_rights_t)1u << 4)
#define MX_RIGHT_SAME_RIGHTS      ((mx_rights_t)1u << 31)

// flags to vm map routines
#define MX_VM_FLAG_FIXED          (1u << 0)
#define MX_VM_FLAG_PERM_READ      (1u << 1)
#define MX_VM_FLAG_PERM_WRITE     (1u << 2)
#define MX_VM_FLAG_PERM_EXECUTE   (1u << 3)

// flags to message pipe routines
#define MX_FLAG_REPLY_PIPE        (1u << 0)

// virtual address
typedef uintptr_t mx_vaddr_t;

// physical address
typedef uintptr_t mx_paddr_t;

// size
typedef uintptr_t mx_size_t;
typedef intptr_t mx_ssize_t;


// Maximum string length for kernel names (process name, thread name, etc)
#define MX_MAX_NAME_LEN           (32)

// m_status_t error codes. Must match values in include/err.h
#define NO_ERROR                (0)

// Internal failures
#define ERR_INTERNAL            (-1)
// TODO: Remove ERR_GENERIC once all uses are gone.
#define ERR_GENERIC             (-1)
#define ERR_NOT_SUPPORTED       (-2)
#define ERR_NOT_FOUND           (-3)
#define ERR_NO_MEMORY           (-4)
#define ERR_NO_RESOURCES        (-5)

// Parameter errors
#define ERR_BAD_SYSCALL         (-10)
#define ERR_BAD_HANDLE          (-11)
#define ERR_INVALID_ARGS        (-12)
#define ERR_OUT_OF_RANGE        (-13)
#define ERR_NOT_ENOUGH_BUFFER   (-14)
#define ERR_ALREADY_EXISTS      (-16)

// Precondition or state errors
#define ERR_BAD_STATE           (-20)
#define ERR_NOT_READY           (-21)
#define ERR_TIMED_OUT           (-22)
#define ERR_BUSY                (-23)
#define ERR_CANCELLED           (-24)
#define ERR_CHANNEL_CLOSED      (-25)
#define ERR_INTERRUPTED         (-26)

// Permission check errors
#define ERR_ACCESS_DENIED       (-30)

// Input-output errors
#define ERR_IO                  (-40)
// TODO: Make more generic (ERR_IO_NACK?)
#define ERR_I2C_NACK            (-41)
// TODO: Rename to something more generic like ERR_DATA_INTEGRITY
#define ERR_CHECKSUM_FAIL       (-42)

// Filesystem specific errors
#define ERR_BAD_PATH            (-50)
#define ERR_NOT_DIR             (-51)
#define ERR_NOT_FILE            (-52)
// TODO: Confusing name - is this the same as POSIX ELOOP?
#define ERR_RECURSE_TOO_DEEP    (-53)

// Garbage bin
// TODO: Replace with INVALID_ARGS
#define ERR_NOT_VALID           (-91)

// TODO: Should just be NOT_SUPPORTED
#define ERR_NOT_IMPLEMENTED     (-92)

// TODO: Replace with ERR_INVALID_ARGS or ERR_NOT_ENOUGH_BUFFER
#define ERR_TOO_BIG             (-93)

// TODO: This appears to be used as a bool, does it need a distinct code?
#define ERR_FAULT               (-95)

// TODO: Replace with either ACCESS_DENIED or NOT_SUPPORTED as appropriate
#define ERR_NOT_ALLOWED         (-96)

// TODO: These all seem like state errors, should they just be ERR_BAD_STATE?
#define ERR_ALREADY_STARTED     (-99)
#define ERR_NOT_BLOCKED         (-100)
#define ERR_THREAD_DETACHED     (-101)

// TODO: This is a variant of ERR_BAD_STATE
#define ERR_NOT_MOUNTED         (-104)

// interrupt flags
#define MX_FLAG_REMAP_IRQ  0x1

#ifdef __cplusplus
}
#endif
