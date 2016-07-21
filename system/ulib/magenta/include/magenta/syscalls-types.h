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
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ask clang format not to mess up the indentation:
// clang-format off

// process id
typedef int32_t mx_pid_t;

// thread id
typedef int32_t mx_tid_t;

typedef uint32_t mx_exception_behaviour_t;
#define MX_EXCEPTION_BEHAVIOUR_DEFAULT 0
#define MX_EXCEPTION_MAX_BEHAVIOUR     0

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

// The argument to mx_mark_exception_handled.
typedef uint32_t mx_exception_status_t;
#define MX_EXCEPTION_STATUS_NOT_HANDLED 0
#define MX_EXCEPTION_STATUS_RESUME 1

// Valid topics for mx_handle_get_info.
typedef enum {
    MX_INFO_HANDLE_VALID,
    MX_INFO_HANDLE_BASIC,
    MX_INFO_PROCESS,
} mx_handle_info_topic_t;

typedef enum {
    MX_OBJ_TYPE_NONE                = 0,
    MX_OBJ_TYPE_PROCESS             = 1,
    MX_OBJ_TYPE_THREAD              = 2,
    MX_OBJ_TYPE_VMEM                = 3,
    MX_OBJ_TYPE_MESSAGE_PIPE        = 4,
    MX_OBJ_TYPE_EVENT               = 5,
    MX_OBJ_TYPE_IOPORT              = 6,
    MX_OBJ_TYPE_DATA_PIPE_PRODUCER  = 7,
    MX_OBJ_TYPE_DATA_PIPE_CONSUMER  = 8,
    MX_OBJ_TYPE_INTERRUPT           = 9,
    MX_OBJ_TYPE_IOMAP               = 10,
    MX_OBJ_TYPE_PCI_DEVICE          = 11,
    MX_OBJ_TYPE_PCI_INT             = 12,
    MX_OBJ_TYPE_LOG                 = 13,
    MX_OBJ_TYPE_LAST
} mx_obj_type_t;

typedef enum {
    MX_OBJ_PROP_NONE            = 0,
    MX_OBJ_PROP_WAITABLE        = 1,
} mx_obj_props_t;

// Returned for topic MX_INFO_HANDLE_BASIC
typedef struct mx_handle_basic_info {
    mx_rights_t rights;
    uint32_t type;                // mx_obj_type_t;
    uint32_t props;               // mx_obj_props_t;
} mx_handle_basic_info_t;

// Returned for topic MX_INFO_PROCESS
typedef struct mx_process_info {
    int return_code;
} mx_process_info_t;


// Defines and structures related to mx_pci_*()
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


// Defines and structures for mx_log_*()
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
#define MX_LOG_FLAG_READABLE  0x40000000

// Defines and structures for mx_io_port_*()
typedef struct mx_io_packet {
    uint64_t key;
    mx_time_t timestamp;
    mx_size_t bytes;
    mx_signals_t signals;
    uint32_t reserved;
} mx_io_packet_t;

typedef struct mx_user_packet {
    uint64_t key;
    uint64_t param[3];
} mx_user_packet_t;

#define MX_IOPORT_OPT_128_SLOTS   0
#define MX_IOPORT_OPT_1K_SLOTS    1

// Buffer size limits on the cprng syscalls
#define MX_CPRNG_DRAW_MAX_LEN        256
#define MX_CPRNG_ADD_ENTROPY_MAX_LEN 256

#ifdef __cplusplus
}
#endif
