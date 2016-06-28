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

// Enum used to select PCIe IRQ modes
typedef enum {
    MX_PCIE_IRQ_MODE_DISABLED = 0,
    MX_PCIE_IRQ_MODE_LEGACY   = 1,
    MX_PCIE_IRQ_MODE_MSI      = 2,
    MX_PCIE_IRQ_MODE_MSI_X    = 3,
} mx_pci_irq_mode_t;

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
#define NO_ERROR                (0)

// Internal failures
// TODO: Rename ERR_GENERIC -> ERR_INTERNAL
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

// TODO: This appears to be obsolete, see if we need it.
#define ERR_NOT_CONFIGURED      (-94)

// TODO: This appears to be used as a bool, does it need a distinct code?
#define ERR_FAULT               (-95)

// TODO: Replace with either ACCESS_DENIED or NOT_SUPPORTED as appropriate
#define ERR_NOT_ALLOWED         (-96)

// TODO: Remove ERR_OFFLINE
#define ERR_OFFLINE             (-97)

// TODO: These all seem like state errors, should they just be ERR_BAD_STATE?
#define ERR_NO_MSG              (-98)
#define ERR_ALREADY_STARTED     (-99)
#define ERR_NOT_BLOCKED         (-100)
#define ERR_THREAD_DETACHED     (-101)

// TODO: Reconcile with ERR_CANCELLED
#define ERR_OBJECT_DESTROYED    (-102)

// TODO: These two are variants of ERR_BAD_STATE
#define ERR_ALREADY_MOUNTED     (-103)
#define ERR_NOT_MOUNTED         (-104)

// TODO: One user of this code, remove it.
#define ERR_PARTIAL_WRITE       (-105)

// interrupt flags
#define MX_FLAG_REMAP_IRQ  0x1

#ifdef __cplusplus
}
#endif
