// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#ifndef __cplusplus
#include <stdatomic.h>
#endif
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ask clang format not to mess up the indentation:
// clang-format off

#ifdef __cplusplus
// We cannot use <stdatomic.h> with C++ code as _Atomic qualifier defined by
// C11 is not valid in C++11. There is not a single standard name that can
// be used in both C and C++. C++ <atomic> defines names which are equivalent
// to those in <stdatomic.h>, but these are contained in the std namespace.
//
// The proper C++ version would be 'using mx_futex_t = std::atomic_int;' but
// in the GCC Magenta build, we don't have a C++ <atomic> and hence cannot use
// it; instead we use this workaround until we decide what the correct solution
// should be.
typedef int mx_futex_t;
#else
typedef atomic_int mx_futex_t;
#endif

// global kernel object id.
typedef uint64_t mx_koid_t;

#define MX_KOID_INVALID ((uint64_t) 0)

// The kind of an exception.
typedef enum {
    // These are architectural exceptions.
    // Further information can be found in report.context.arch.

    // General exception not covered by another value.
    MX_EXCP_GENERAL = 0,
    MX_EXCP_FATAL_PAGE_FAULT = 1,
    MX_EXCP_UNDEFINED_INSTRUCTION = 2,
    MX_EXCP_SW_BREAKPOINT = 3,
    MX_EXCP_HW_BREAKPOINT = 4,

    MX_EXCP_MAX_ARCH = 99,

    // Synthetic exceptions.

    // A thread has started.
    MX_EXCP_START = 100,

    // A thread or process has exited or otherwise terminated.
    // N.B. "gone" notifications are not responded to.
    MX_EXCP_GONE = 101,
} mx_excp_type_t;

#define MX_EXCP_IS_ARCH(excp) ((excp) <= MX_EXCP_MAX_ARCH)

typedef struct x86_64_exc_data {
    uint64_t vector;
    uint64_t err_code;
    uint64_t cr2;
} x86_64_exc_data_t;

typedef struct arm64_exc_data {
    uint64_t far;
} arm64_exc_data_t;

#define ARCH_ID_UNKNOWN        0u
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
        mx_vaddr_t pc;
        union {
            x86_64_exc_data_t x86_64;
            arm64_exc_data_t  arm_64;
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
    uint32_t /*mx_excp_type_t*/ type;
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

// Flags for mx_task_resume()
#define MX_RESUME_EXCEPTION (1)
// Indicates that we should resume the thread from stopped-in-exception state
// (default resume does not do so)

#define MX_RESUME_NOT_HANDLED (2)
// Only meaningful when combined with MX_RESUME_EXCEPTION
// Indicates that instead of resuming from the faulting instruction we instead
// let any additional exception handlers (eg, system after process) take a shot
// at it, and if there are no additional handlers, the thread will terminate

// Flags for mx_object_bind_exception_port.
#define MX_EXCEPTION_PORT_DEBUGGER (1)
// When binding an exception port to a process, set the process's debugger
// exception port.

// Valid topics for mx_object_get_info.
typedef enum {
    MX_INFO_HANDLE_VALID = 1,
    MX_INFO_HANDLE_BASIC,
    MX_INFO_PROCESS,
    MX_INFO_PROCESS_THREADS,
} mx_object_info_topic_t;

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
    MX_OBJ_TYPE_LOG                 = 12,
    MX_OBJ_TYPE_WAIT_SET            = 13,
    MX_OBJ_TYPE_SOCKET              = 14,
    MX_OBJ_TYPE_RESOURCE            = 15,
    MX_OBJ_TYPE_EVENT_PAIR          = 16,
    MX_OBJ_TYPE_JOB                 = 17,
    MX_OBJ_TYPE_LAST
} mx_obj_type_t;

typedef enum {
    MX_OBJ_PROP_NONE            = 0,
    MX_OBJ_PROP_WAITABLE        = 1,
} mx_obj_props_t;

// Common MX_INFO header
typedef struct mx_info_header {
    uint32_t topic;              // identifies the info struct
    uint16_t avail_topic_size;   // “native” size of the struct
    uint16_t topic_size;         // size of the returned struct (<=topic_size)
    uint32_t avail_count;        // number of records the kernel has
    uint32_t count;              // number of records returned (limited by buffer size)
} mx_info_header_t;

#define mx_info_nth_record(rec, n) (&(rec)[n])

typedef struct mx_record_handle_basic {
    mx_koid_t koid;
    mx_rights_t rights;
    uint32_t type;                // mx_obj_type_t;
    uint32_t props;               // mx_obj_props_t;
} mx_record_handle_basic_t;

// Returned for topic MX_INFO_HANDLE_BASIC
typedef struct mx_info_handle_basic {
    mx_info_header_t hdr;
    mx_record_handle_basic_t rec;
} mx_info_handle_basic_t;

typedef struct mx_record_process {
    int return_code;
} mx_record_process_t;

// Returned for topic MX_INFO_PROCESS
typedef struct mx_info_process {
    mx_info_header_t hdr;
    mx_record_process_t rec;
} mx_info_process_t;

typedef struct mx_record_process_thread {
    mx_koid_t koid;
} mx_record_process_thread_t;

// Returned for topic MX_INFO_PROCESS_THREADS
typedef struct mx_info_process_threads {
    mx_info_header_t hdr;
    mx_record_process_thread_t rec[];
} mx_info_process_threads_t;

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

#define MX_PCI_NO_IRQ_MAPPING UINT32_MAX

typedef struct mx_pci_init_arg {
    // Dimensions: device id, function id, legacy pin number
    // MX_PCI_NO_IRQ_MAPPING if no mapping specified.
    uint32_t dev_pin_to_global_irq[32][8][4];

    uint32_t num_irqs;
    struct {
        uint32_t global_irq;
        bool level_triggered;
        bool active_high;
    } irqs[32];

    uint32_t ecam_window_count;
    struct {
        uint64_t base;
        size_t size;
        uint8_t bus_start;
        uint8_t bus_end;
    } ecam_windows[];
} mx_pci_init_arg_t;

#define MX_PCI_INIT_ARG_MAX_ECAM_WINDOWS 1
#define MX_PCI_INIT_ARG_MAX_SIZE (sizeof(((mx_pci_init_arg_t*)NULL)->ecam_windows[0]) * \
                                  MX_PCI_INIT_ARG_MAX_ECAM_WINDOWS + \
                                  sizeof(mx_pci_init_arg_t))

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

// Defines and structures for mx_port_*()

#define MX_PORT_MAX_PKT_SIZE   128u

#define MX_PORT_PKT_TYPE_KERN      0u
#define MX_PORT_PKT_TYPE_IOSN      1u
#define MX_PORT_PKT_TYPE_USER      2u
#define MX_PORT_PKT_TYPE_EXCEPTION 3u

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

// Structure for mx_waitset_*():

typedef struct mx_waitset_result {
    uint64_t cookie;
    mx_status_t wait_result;
    uint32_t reserved;
    mx_signals_state_t signals_state;
} mx_waitset_result_t;

// Defines for mx_datapipe_*():

#define MX_DATAPIPE_WRITE_FLAG_ALL_OR_NONE  1u
// Mask for all the valid MX_DATAPIPE_WRITE_FLAG_... flags:
#define MX_DATAPIPE_WRITE_FLAG_MASK         1u

// DISCARD, QUERY, and PEEK are mutually exclusive.
#define MX_DATAPIPE_READ_FLAG_ALL_OR_NONE   1u
#define MX_DATAPIPE_READ_FLAG_DISCARD       2u
#define MX_DATAPIPE_READ_FLAG_QUERY         4u
#define MX_DATAPIPE_READ_FLAG_PEEK          8u
// Mask for all the valid MX_DATAPIPE_READ_FLAG_... flags:
#define MX_DATAPIPE_READ_FLAG_MASK          15u

// Buffer size limits on the cprng syscalls
#define MX_CPRNG_DRAW_MAX_LEN        256
#define MX_CPRNG_ADD_ENTROPY_MAX_LEN 256

// Object properties.

// Argument is MX_POLICY_BAD_HANDLE_... (below, uint32_t).
#define MX_PROP_BAD_HANDLE_POLICY           1u
// Argument is a uint32_t.
#define MX_PROP_NUM_STATE_KINDS             2u
// Argument is an mx_size_t.
#define MX_PROP_DATAPIPE_READ_THRESHOLD     3u
// Argument is an mx_size_t.
#define MX_PROP_DATAPIPE_WRITE_THRESHOLD    4u

// Policies for MX_PROP_BAD_HANDLE_POLICY:
#define MX_POLICY_BAD_HANDLE_IGNORE         0u
#define MX_POLICY_BAD_HANDLE_LOG            1u
#define MX_POLICY_BAD_HANDLE_EXIT           2u

// Socket flags and limits.
#define MX_SOCKET_CONTROL                   1u
#define MX_SOCKET_HALF_CLOSE                2u
#define MX_SOCKET_CONTROL_MAX_LEN           1024u

// mx_thread_read_state, mx_thread_write_state
// The maximum size of thread state, in bytes, that can be processed by the
// read_state/write_state syscalls. It exists so code can expect a sane limit
// on the amount of memory needed to process the request.
#define MX_MAX_THREAD_STATE_SIZE 4096u

// The "general regs" are by convention in regset 0.
#define MX_THREAD_STATE_REGSET0 0u
#define MX_THREAD_STATE_REGSET1 1u
#define MX_THREAD_STATE_REGSET2 2u
#define MX_THREAD_STATE_REGSET3 3u
#define MX_THREAD_STATE_REGSET4 4u
#define MX_THREAD_STATE_REGSET5 5u
#define MX_THREAD_STATE_REGSET6 6u
#define MX_THREAD_STATE_REGSET7 7u
#define MX_THREAD_STATE_REGSET8 8u
#define MX_THREAD_STATE_REGSET9 9u

#ifndef DEPRECATE_COMPAT_SYSCALLS
typedef struct mx_waitset_result mx_wait_set_result_t;
#define MX_IO_PORT_MAX_PKT_SIZE MX_PORT_MAX_PKT_SIZE
#define MX_IO_PORT_PKT_TYPE_KERN MX_PORT_PKT_TYPE_KERN
#define MX_IO_PORT_PKT_TYPE_IOSN MX_PORT_PKT_TYPE_IOSN
#define MX_IO_PORT_PKT_TYPE_USER MX_PORT_PKT_TYPE_USER
#define MX_IO_PORT_PKT_TYPE_EXCEPTION MX_PORT_PKT_TYPE_EXCEPTION
#endif

// VM Object opcodes
#define MX_VMO_OP_COMMIT                1u
#define MX_VMO_OP_DECOMMIT              2u
#define MX_VMO_OP_LOCK                  3u
#define MX_VMO_OP_UNLOCK                4u
#define MX_VMO_OP_LOOKUP                5u
#define MX_VMO_OP_CACHE_SYNC            6u

#ifdef __cplusplus
}
#endif
