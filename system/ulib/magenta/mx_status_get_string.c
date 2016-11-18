// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/status.h>

const char* _mx_status_get_string(mx_status_t status) {
    switch (status) {
    case NO_ERROR: return "NO_ERROR";
    case ERR_INTERNAL: return "ERR_INTERNAL";
    case ERR_NOT_SUPPORTED: return "ERR_NOT_SUPPORTED";
    case ERR_NO_RESOURCES: return "ERR_NO_RESOURCES";
    case ERR_NO_MEMORY: return "ERR_NO_MEMORY";
    case ERR_INVALID_ARGS: return "ERR_INVALID_ARGS";
    case ERR_WRONG_TYPE: return "ERR_WRONG_TYPE";
    case ERR_BAD_SYSCALL: return "ERR_BAD_SYSCALL";
    case ERR_BAD_HANDLE: return "ERR_BAD_HANDLE";
    case ERR_OUT_OF_RANGE: return "ERR_OUT_OF_RANGE";
    case ERR_BUFFER_TOO_SMALL: return "ERR_BUFFER_TOO_SMALL";
    case ERR_BAD_STATE: return "ERR_BAD_STATE";
    case ERR_NOT_FOUND: return "ERR_NOT_FOUND";
    case ERR_ALREADY_EXISTS: return "ERR_ALREADY_EXISTS";
    case ERR_ALREADY_BOUND: return "ERR_ALREADY_BOUND";
    case ERR_TIMED_OUT: return "ERR_TIMED_OUT";
    case ERR_HANDLE_CLOSED: return "ERR_HANDLE_CLOSED";
    case ERR_REMOTE_CLOSED: return "ERR_REMOTE_CLOSED";
    case ERR_UNAVAILABLE: return "ERR_UNAVAILABLE";
    case ERR_SHOULD_WAIT: return "ERR_SHOULD_WAIT";
    case ERR_ACCESS_DENIED: return "ERR_ACCESS_DENIED";
    case ERR_IO: return "ERR_IO";
    case ERR_IO_REFUSED: return "ERR_IO_REFUSED";
    case ERR_IO_DATA_INTEGRITY: return "ERR_IO_DATA_INTEGRITY";
    case ERR_IO_DATA_LOSS: return "ERR_IO_DATA_LOSS";
    case ERR_BAD_PATH: return "ERR_BAD_PATH";
    case ERR_NOT_DIR: return "ERR_NOT_DIR";
    case ERR_NOT_FILE: return "ERR_NOT_FILE";
    default: return "(UNKNOWN)";

    // TODO(mcgrathr): Having this extra case here (a value far away from
    // the other values) forces LLVM to disable its switch->table-lookup
    // optimization.  That optimization produces a table of pointers in
    // rodata, which is not PIC-friendly (requires a dynamic reloc for each
    // element) and so makes the vDSO build bomb out at link time.  Some
    // day we'll teach LLVM either to disable this optimization in PIC mode
    // when it would result in dynamic relocs, or (ideally) to generate a
    // PIC-friendly lookup table like it does for jump tables.
    case 99999: return "(UNKNOWN)";
    }
}

__typeof(mx_status_get_string) mx_status_get_string
    __attribute__((weak, alias("_mx_status_get_string")));

// Generated with:
// grep '#define'  system/public/magenta/errors.h | grep -v NO_ERROR |
// sed 's/.*ERR_/ERR_/g' | sed 's/\s.*//g' |
// awk '{print "case "$1": return \""$1"\";";}'
