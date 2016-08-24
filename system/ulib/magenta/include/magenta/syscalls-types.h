// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ask clang format not to mess up the indentation:
// clang-format off

// global kernel object id.
typedef uint64_t mx_koid_t;

#define MX_KOID_INVALID ((uint64_t) 0)

// The kind of the exception, at a high level.
typedef uint32_t mx_exception_type_t;

// Types of exceptions.
// Synthetic exceptions are expanded here instead of having them all under
// one major type as there aren't that many and we have 16-32 bits to
// represent all the possibilities (even 8 bits would be enough).

// Further specificity is provided in the "context" field.
#define MX_EXCEPTION_TYPE_ARCH 0
// A synthetic exception for threads.
#define MX_EXCEPTION_TYPE_START 1
// A synthetic exception for threads and processes.
// N.B. "gone" notifications are not responded to.
#define MX_EXCEPTION_TYPE_GONE 2

typedef struct x86_64_exc_frame {
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t vector;
    uint64_t err_code;
    uint64_t ip, cs, flags;
    uint64_t user_sp, user_ss;
} x86_64_exc_frame_t;

typedef struct arm64_exc_frame {
    uint64_t r[30];
    uint64_t lr;
    uint64_t usp;
    uint64_t elr;
    uint64_t spsr;
} arm64_exc_frame_t;

#define ARCH_ID_UKNOWN         0u
#define ARCH_ID_X86_64         1u
#define ARCH_ID_ARM_64         2u

// data associated with an exception (siginfo in linux parlance)
typedef struct mx_exception_context {
    // One of ARCH_ID above.
    uint32_t arch_id;
    // The process of the thread with the exception.
    mx_koid_t pid;

    // The thread that got the exception.
    // This is zero in "process gone" notifications.
    mx_koid_t tid;

    struct {
        // The value here depends on exception_type.
        uint32_t subtype;
        mx_vaddr_t pc;
        union {
            x86_64_exc_frame_t x86_64;
            arm64_exc_frame_t  arm_64;
        } u;
        // TODO(dje): add more stuff, revisit packing
        // For an example list of things one might add, see linux siginfo.
    } arch;
} mx_exception_context_t;

// The common header of all exception reports.
// TODO(dje): For now we assume all exceptions are thread-related.
// A reasonably safe assumption, but the intent is to not preclude
// other kinds of exceptions should a need ever arise.
typedef struct mx_exception_header {
    // The actual size, in bytes, of the report (including this field),
    // but *not* including mx_packet_header_t.
    uint32_t size;

    // While IWBN to use an enum here, it's still not portable in C.
    // TODO(dje): Balance generic-ness vs architecture-specific-ness.
    uint32_t /*mx_exception_type_t*/ type;
} mx_exception_header_t;

// Data reported to an exception handler for most exceptions.
typedef struct mx_exception_report {
    mx_exception_header_t header;
    // The remainder of the report is exception-specific.
    // TODO(dje): For now we KISS and use the same struct for everything.
    mx_exception_context_t context;
} mx_exception_report_t;

// The status argument to _magenta_mark_exception_handled.
// Negative values are for internal use only.
typedef enum {
    MX_EXCEPTION_STATUS_HANDLER_GONE = -2,
    MX_EXCEPTION_STATUS_WAITING = -1,
    // As an analogy, this would be like typing "c" in gdb after a segfault.
    // In linux the signal would be delivered to the thread, which would
    // either terminate the process or run a signal handler if defined.
    // In magenta this gives the next signal handler in the list a crack at
    // the exception.
    MX_EXCEPTION_STATUS_NOT_HANDLED = 0,
    // As an analogy, this would be like typing "sig 0" in gdb after a
    // segfault. The faulting instruction will be retried. If, for example, it
    // segfaults again then the user is back in the debugger again, which is
    // working as intended.
    // Note: We don't, currently at least, support delivering a different
    // exception (signal in linux parlance) to the thread. As an analogy, this
    // would be like typing "sig 8" in gdb after getting a segfault (which is
    // signal 11).
    MX_EXCEPTION_STATUS_RESUME = 1
} mx_exception_status_t;

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
    MX_OBJ_TYPE_WAIT_SET            = 14,
    MX_OBJ_TYPE_SOCKET              = 15,
    MX_OBJ_TYPE_RESOURCE            = 16,
    MX_OBJ_TYPE_LAST
} mx_obj_type_t;

typedef enum {
    MX_OBJ_PROP_NONE            = 0,
    MX_OBJ_PROP_WAITABLE        = 1,
} mx_obj_props_t;

// Returned for topic MX_INFO_HANDLE_BASIC
typedef struct mx_handle_basic_info {
    mx_koid_t koid;
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

#define MX_IO_PORT_MAX_PKT_SIZE   128u

#define MX_IO_PORT_PKT_TYPE_KERN      0u
#define MX_IO_PORT_PKT_TYPE_IOSN      1u
#define MX_IO_PORT_PKT_TYPE_USER      2u
#define MX_IO_PORT_PKT_TYPE_EXCEPTION 3u

typedef struct mx_packet_header {
    uint64_t key;
    uint32_t type;
    uint32_t extra;
} mx_packet_header_t;

typedef struct mx_io_packet {
    mx_packet_header_t hdr;
    mx_time_t timestamp;
    mx_size_t bytes;
    mx_signals_t signals;
    uint32_t reserved;
} mx_io_packet_t;

typedef struct mx_exception_packet {
    mx_packet_header_t hdr;
    mx_exception_report_t report;
} mx_exception_packet_t;

// Structures for mx_wait_set_*()
typedef struct mx_wait_set_result {
    uint64_t cookie;
    mx_status_t wait_result;
    uint32_t reserved;
    mx_signals_state_t signals_state;
} mx_wait_set_result_t;

// Buffer size limits on the cprng syscalls
#define MX_CPRNG_DRAW_MAX_LEN        256
#define MX_CPRNG_ADD_ENTROPY_MAX_LEN 256

// Object properties.

#define MX_PROP_BAD_HANDLE_POLICY      1u

#define MX_POLICY_BAD_HANDLE_IGNORE    0u
#define MX_POLICY_BAD_HANDLE_LOG       1u
#define MX_POLICY_BAD_HANDLE_EXIT      2u

#ifdef __cplusplus
}
#endif
