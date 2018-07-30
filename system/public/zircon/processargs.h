// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_PROCESSARGS_H_
#define ZIRCON_PROCESSARGS_H_

#include <zircon/compiler.h>
#include <zircon/types.h>
#include <stdint.h>

__BEGIN_CDECLS

// This is a protocol for passing state to a new process
// via a message in a channel.

#define ZX_PROCARGS_PROTOCOL ((uint32_t)0x4150585du) // MXPA
#define ZX_PROCARGS_VERSION ((uint32_t)0x0001000u)

typedef struct zx_proc_args zx_proc_args_t;

struct zx_proc_args {
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
#define PA_PROC_SELF             0x01u

// Handle to the initial thread of our own process.
#define PA_THREAD_SELF           0x02u

// Handle to a Job object which can be used to make child processes. The
// Job can be the same as the one used to create this process or it can
// be different.
#define PA_JOB_DEFAULT           0x03u

// Handle to the root of our address space
#define PA_VMAR_ROOT             0x04u

// Handle to the VMAR used to load the initial program image.
#define PA_VMAR_LOADED           0x05u


// --- Loader Service and VMO Handles ---
// Used by libc init (or equivalent) and dynamic loader

// Service for loading shared libraries.
// See |fuchsia.ldsvc.Loader| for the interface definition.
#define PA_LDSVC_LOADER          0x10u

// Handle to the VMO containing the ELF image of the system vDSO.  This
// handle is duplicable, transferable, readable, and executable, but not
// writable.  The contents of the VM object should be treated like any
// other general-purpose ELF file image of type ET_DYN.  A process only
// needs this handle so that it can map the vDSO into new processes it
// might create or propagate it on to its children so they can do so.
// Each process's own vDSO was mapped in by its creator before the
// process started, its address passed as an argument to entry point.
#define PA_VMO_VDSO              0x11u

// Handle to the VMO used to map the initial thread's stack.  This
// handle usually has all rights.  The protocol between process creator
// and new process is that this entire VM object has been mapped in
// before the process starts.  The initial value for the SP register in
// the new process is the high edge of the mapping (assuming stacks grow
// downwards), adjusted down as required by the particular machine's C
// calling convention for function entry.  Thus the new process can
// compute its exact stack bounds by subtracting the size reported by
// this VMO from the (adjusted back up) initial SP value.
#define PA_VMO_STACK             0x13u

// VM object handle for the main executable file
#define PA_VMO_EXECUTABLE        0x14u

// Used by kernel and userboot during startup
#define PA_VMO_BOOTDATA          0x1Au

// Used by kernel and userboot during startup
#define PA_VMO_BOOTFS            0x1Bu

// Used by the kernel to export debug information as a file in bootfs.  When
// devmgr starts, it looks for handles of this type, and adds them as files in
// /boot/kernel/<vmo-name>.
#define PA_VMO_KERNEL_FILE       0x1Cu


// --- Namespace Handles ---

// A handle which will handle OPEN requests relative
// to a particular path which is specified by the
// nametable entry referred to by the "arg" field
#define PA_NS_DIR                0x20u


// --- FDIO Handles ---
// Used by libfdio for passing fdtable, fsroot, etc

// Handle types the fdio library uses
#define PA_FDIO_REMOTE           0x32u
#define PA_FDIO_PIPE             0x33u
#define PA_FDIO_EVENT            0x34u
#define PA_FDIO_LOGGER           0x35u
#define PA_FDIO_SOCKET           0x36u

// Server endpoint for handling connection to appmgr services.
#define PA_DIRECTORY_REQUEST     0x3Bu

// Used by devmgr and devhosts
#define PA_RESOURCE              0x3Fu


// --- Various ---

// Handle types for one-off use and prototyping
#define PA_USER0                 0xF0u
#define PA_USER1                 0xF1u
#define PA_USER2                 0xF2u

__END_CDECLS

#endif // ZIRCON_PROCESSARGS_H_
