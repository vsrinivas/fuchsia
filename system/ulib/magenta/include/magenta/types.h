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

typedef uint32_t mx_rights_t;
#define MX_RIGHT_NONE             ((mx_rights_t)0u)
#define MX_RIGHT_DUPLICATE        ((mx_rights_t)1u << 0)
#define MX_RIGHT_TRANSFER         ((mx_rights_t)1u << 1)
#define MX_RIGHT_READ             ((mx_rights_t)1u << 2)
#define MX_RIGHT_WRITE            ((mx_rights_t)1u << 3)
#define MX_RIGHT_EXECUTE          ((mx_rights_t)1u << 4)

// flags to vm map routines
#define MX_VM_FLAG_FIXED          (1u << 0)
#define MX_VM_FLAG_PERM_READ      (1u << 1)
#define MX_VM_FLAG_PERM_WRITE     (1u << 2)
#define MX_VM_FLAG_PERM_EXECUTE   (1u << 3)

typedef uint32_t mx_exception_behaviour_t;
#define MX_EXCEPTION_BEHAVIOUR_DEFAULT 0
#define MX_EXCEPTION_MAX_BEHAVIOUR     0

// virtual address
typedef uintptr_t mx_vaddr_t;

// physical address
typedef uintptr_t mx_paddr_t;

// size
typedef uintptr_t mx_size_t;
typedef intptr_t mx_ssize_t;

// process id
typedef int32_t mx_pid_t;

// thread id
typedef int32_t mx_tid_t;

// data associated with an exception (siginfo in linux parlance)
typedef struct mx_exception_context {
    // TODO(dje): This value is wip.
    uint32_t type;

    // TODO(dje): add more stuff
} mx_exception_context_t;

// data reported to an exception handler
typedef struct mx_exception_report {
    mx_pid_t pid;
    mx_tid_t tid;
    mx_vaddr_t pc;
    mx_exception_context_t context;
} mx_exception_report_t;

// The argument to _magenta_mark_exception_handled.
typedef uint32_t mx_exception_status_t;
#define MX_EXCEPTION_STATUS_NOT_HANDLED 0
#define MX_EXCEPTION_STATUS_RESUME 1

// information from process_get_info
typedef struct mx_process_info {
    mx_size_t len;

    int return_code;
} mx_process_info_t;

// Valid topics for _magenta_handle_get_info.
#define MX_INFO_HANDLE_VALID 0
#define MX_INFO_HANDLE_BASIC 1

typedef enum {
    MX_OBJ_TYPE_NONE            = 0,
    MX_OBJ_TYPE_PROCESS         = 1,
    MX_OBJ_TYPE_THREAD          = 2,
    MX_OBJ_TYPE_VMEM            = 3,
    MX_OBJ_TYPE_MESSAGE_PIPE    = 4,
    MX_OBJ_TYPE_EVENT           = 5,
    MX_OBJ_TYPE_LOG             = 6,
    MX_OBJ_TYPE_INTERRUPT       = 7,
    MX_OBJ_TYPE_IOMAP           = 8,
    MX_OBJ_TYPE_PCI_DEVICE      = 9,
    MX_OBJ_TYPE_PCI_INT         = 10,
} mx_obj_type_t;

typedef enum {
    MX_OBJ_PROP_NONE            = 0,
    MX_OBJ_PROP_WAITABLE        = 1,
} mx_obj_props_t;

typedef struct handle_basic_info {
    mx_rights_t rights;
    uint32_t type;                // mx_obj_type_t;
    uint32_t props;               // mx_obj_props_t;
} handle_basic_info_t;

// Info returned to dev manager for PCIe devices when probing.
typedef struct mx_pcie_get_nth_info {
    uint16_t vendor_id;
    uint16_t device_id;

    uint8_t  base_class;
    uint8_t  sub_class;
    uint8_t  program_interface;
    uint8_t  revision_id;

    uint8_t  bus_id;
    uint8_t  dev_id;
    uint8_t  func_id;
} mx_pcie_get_nth_info_t;

// Flags which can be used to to control cache policy for APIs which map memory.
typedef enum {
    MX_CACHE_POLICY_CACHED          = 0,
    MX_CACHE_POLICY_UNCACHED        = 1,
    MX_CACHE_POLICY_UNCACHED_DEVICE = 2,
    MX_CACHE_POLICY_WRITE_COMBINING = 3,
} mx_cache_policy_t;

// Log entries and flags
typedef struct mx_log_record {
    uint32_t reserved;
    uint16_t datalen;
    uint16_t flags;
    mx_time_t timestamp;
    char data[0];
} mx_log_record_t;

#define MX_LOG_RECORD_MAX     256

#define MX_LOG_FLAG_KERNEL    0x0100
#define MX_LOG_FLAG_DEVMGR    0x0200
#define MX_LOG_FLAG_CONSOLE   0x0400
#define MX_LOG_FLAG_DEVICE    0x0800
#define MX_LOG_FLAG_MASK      0x0F00

#define MX_LOG_FLAG_WAIT      0x80000000

// Maximum string length for kernel names (process name, thread name, etc)
#define MX_MAX_NAME_LEN           (32)

// m_status_t error codes. Must match values in include/err.h
#define NO_ERROR (0)
#define ERR_GENERIC (-1)
#define ERR_NOT_FOUND (-2)
#define ERR_NOT_READY (-3)
#define ERR_NO_MSG (-4)
#define ERR_NO_MEMORY (-5)
#define ERR_ALREADY_STARTED (-6)
#define ERR_NOT_VALID (-7)
#define ERR_INVALID_ARGS (-8)
#define ERR_NOT_ENOUGH_BUFFER (-9)
#define ERR_NOT_SUSPENDED (-10)
#define ERR_OBJECT_DESTROYED (-11)
#define ERR_NOT_BLOCKED (-12)
#define ERR_TIMED_OUT (-13)
#define ERR_ALREADY_EXISTS (-14)
#define ERR_CHANNEL_CLOSED (-15)
#define ERR_OFFLINE (-16)
#define ERR_NOT_ALLOWED (-17)
#define ERR_BAD_PATH (-18)
#define ERR_ALREADY_MOUNTED (-19)
#define ERR_IO (-20)
#define ERR_NOT_DIR (-21)
#define ERR_NOT_FILE (-22)
#define ERR_RECURSE_TOO_DEEP (-23)
#define ERR_NOT_SUPPORTED (-24)
#define ERR_TOO_BIG (-25)
#define ERR_CANCELLED (-26)
#define ERR_NOT_IMPLEMENTED (-27)
#define ERR_CHECKSUM_FAIL (-28)
#define ERR_CRC_FAIL (-29)
#define ERR_CMD_UNKNOWN (-30)
#define ERR_BAD_STATE (-31)
#define ERR_BAD_LEN (-32)
#define ERR_BUSY (-33)
#define ERR_THREAD_DETACHED (-34)
#define ERR_I2C_NACK (-35)
#define ERR_ALREADY_EXPIRED (-36)
#define ERR_OUT_OF_RANGE (-37)
#define ERR_NOT_CONFIGURED (-38)
#define ERR_NOT_MOUNTED (-39)
#define ERR_FAULT (-40)
#define ERR_NO_RESOURCES (-41)
#define ERR_BAD_HANDLE (-42)
#define ERR_ACCESS_DENIED (-43)
#define ERR_PARTIAL_WRITE (-44)
#define ERR_BAD_SYSCALL  (-45)

// interrupt flags
#define MX_FLAG_REMAP_IRQ  0x1

#ifdef __cplusplus
}
#endif
