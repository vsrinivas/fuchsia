// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// This is a protocol for passing state to a new process
// via a message in a channel.

#define MX_PROCARGS_PROTOCOL 0x4150585d // MXPA
#define MX_PROCARGS_VERSION 0x0001000

typedef struct mx_proc_args mx_proc_args_t;

struct mx_proc_args {
    // Protocol and version identifiers to allow for
    // different process start message protocols and
    // versioning of the same.
    uint32_t protocol;
    uint32_t version;

    // Offset from start of message to handle info
    // array, which contains one uint32_t per handle
    // passed along with the message.
    uint32_t handle_info_off;

    // Offset from start of message to arguments and
    // count of arguments.  Arguments are provided as
    // a set of null-terminated utf-8 strings, one
    // after the other.
    uint32_t args_off;
    uint32_t args_num;

    // Offset from start of message to environment strings and count of
    // them.  Environment entries are provided as a set of null-terminated
    // UTF-8 strings, one after the other.  Canonically each string has
    // the form "NAME=VALUE", but nothing enforces this.
    uint32_t environ_off;
    uint32_t environ_num;

    // Offset from start of message to name strings and count of them.
    // These strings are packed similar to the argument strings,
    // but are referenced by PA_NS_* handle table entries and used
    // to set up namespaces.
    //
    // Specifically: In a handle table entry with PA_HND_TYPE(info)
    // of PA_NS_*, PA_HND_ARG(info) is an index into this name table.
    uint32_t names_off;
    uint32_t names_num;
};

// Handle Info entries associate a type and optional
// argument with each handle included in the process
// arguments message.
#define PA_HND(type, arg)          (((type)&0xFF)| (((arg)&0xFFFF)<<16))
#define PA_HND_TYPE(n)             ((n) & 0xFF)
#define PA_HND_SUBTYPE(n)          (((n) >> 8) & 0xFF)
#define PA_HND_ARG(n)              (((n) >> 16) & 0xFFFF)

// --- Core Runtime Handles ---
// Used by libc init (or equivalent) and dynamic loader

// Handle to our own process.
#define PA_PROC_SELF             0x01

// Handle to the initial thread of our own process.
#define PA_THREAD_SELF           0x02

// Handle to a Job object which can be used to make child processes. The
// Job can be the same as the one used to create this process or it can
// be different.
#define PA_JOB_DEFAULT           0x03

// Handle to the root of our address space
#define PA_VMAR_ROOT             0x04

// Handle to the VMAR used to load the initial program image.
#define PA_VMAR_LOADED           0x05


// --- Loader Service and VMO Handles ---
// Used by libc init (or equivalent) and dynamic loader

// Channel for dynamic loader service
#define PA_SVC_LOADER            0x10

// Handle to the VMO containing the ELF image of the system vDSO.  This
// handle is duplicable, transferable, readable, and executable, but not
// writable.  The contents of the VM object should be treated like any
// other general-purpose ELF file image of type ET_DYN.  A process only
// needs this handle so that it can map the vDSO into new processes it
// might create or propagate it on to its children so they can do so.
// Each process's own vDSO was mapped in by its creator before the
// process started, its address passed as an argument to entry point.
#define PA_VMO_VDSO              0x11

// Handle to the VMO used to map the initial thread's stack.  This
// handle usually has all rights.  The protocol between process creator
// and new process is that this entire VM object has been mapped in
// before the process starts.  The initial value for the SP register in
// the new process is the high edge of the mapping (assuming stacks grow
// downwards), adjusted down as required by the particular machine's C
// calling convention for function entry.  Thus the new process can
// compute its exact stack bounds by subtracting the size reported by
// this VMO from the (adjusted back up) initial SP value.
#define PA_VMO_STACK             0x13

// VM object handle for the main executable file
#define PA_VMO_EXECUTABLE        0x14

// Used by kernel and userboot during startup
#define PA_VMO_BOOTDATA          0x1A

// Used by kernel and userboot during startup
#define PA_VMO_BOOTFS            0x1B


// --- Namespace Handles ---

// A handle which will handle OPEN requests relative
// to a particular path which is specified by the
// nametable entry refereed to by the "arg" field
#define PA_NS_DIR                0x20


// --- MXIO Handles ---
// Used by libmxio for passing fdtable, fsroot, etc

// Handle types the mxio library uses
#define PA_MXIO_ROOT             0x30
#define PA_MXIO_CWD              0x31
#define PA_MXIO_REMOTE           0x32
#define PA_MXIO_PIPE             0x33
#define PA_MXIO_EVENT            0x34
#define PA_MXIO_LOGGER           0x35
#define PA_MXIO_SOCKET           0x36

// Client endpoint for remoteio "/svc" directory provided
// to enable outbound connections to services.
#define PA_SERVICE_ROOT          0x3A

// Server endpoint for remoteio "/svc" directory provided
// to enable handling of inbound connections to services
#define PA_SERVICE_REQUEST       0x3B

// Used by devmgr and devhosts
#define PA_RESOURCE              0x3F


// --- Various ---

// Handle types used by the application model
#define PA_APP_LAUNCHER          0x41
#define PA_APP_SERVICES          0x43

// Handle types for one-off use and prototyping
#define PA_USER0                 0xF0
#define PA_USER1                 0xF1
#define PA_USER2                 0xF2


// Dynamic Loader Service Messages
// Used by dynamic loader to obtain objects to link.
typedef struct mx_loader_svc_msg mx_loader_svc_msg_t;
struct mx_loader_svc_msg {
    mx_txid_t txid;
    uint32_t opcode;
    int32_t arg;
    uint32_t reserved0;
    uint32_t reserved1;
    uint8_t data[0];
};

#define LOADER_SVC_OP_STATUS 0
// reply message, arg=status

#define LOADER_SVC_OP_DONE 1
// Clean shutdown of service

#define LOADER_SVC_OP_LOAD_OBJECT 2
// arg=0, data[] object name (asciiz)
// reply includes vmo handle on success

#define LOADER_SVC_OP_DEBUG_PRINT 3
// arg=0, data[] debug text (asciiz)

#define LOADER_SVC_OP_LOAD_SCRIPT_INTERP 4
// arg=0, data[] object name (asciiz)
// reply includes vmo handle on success


// --- Compatibility Defines ---
// TODO: remove once Fuchsia deps are resolved
#define MX_HND_INFO(type, arg) (((type)&0xFF)| (((arg)&0xFFFF)<<16))
#define MX_HND_INFO_TYPE(n) ((n) & 0xFF)

#define MX_HND_TYPE_USER1 PA_USER1
#define MX_HND_TYPE_MXIO_PIPE PA_MXIO_PIPE
#define MX_HND_TYPE_JOB PA_JOB_DEFAULT
#define MX_HND_TYPE_MXIO_ROOT PA_MXIO_ROOT
#define MX_HND_TYPE_MXIO_LOGGER PA_MXIO_LOGGER

#ifdef __cplusplus
}
#endif
