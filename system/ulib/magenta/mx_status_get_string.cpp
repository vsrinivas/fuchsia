// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/status.h>

#include "private.h"

const char* _mx_status_get_string(mx_status_t status) {
    switch (status) {
    case MX_OK: return "MX_OK";
    case MX_ERR_INTERNAL: return "MX_ERR_INTERNAL";
    case MX_ERR_NOT_SUPPORTED: return "MX_ERR_NOT_SUPPORTED";
    case MX_ERR_NO_RESOURCES: return "MX_ERR_NO_RESOURCES";
    case MX_ERR_NO_MEMORY: return "MX_ERR_NO_MEMORY";
    case MX_ERR_CALL_FAILED: return "MX_ERR_CALL_FAILED";
    case MX_ERR_INTERNAL_INTR_RETRY: return "MX_ERR_INTERNAL_INTR_KILLED_RETRY";
    case MX_ERR_INVALID_ARGS: return "MX_ERR_INVALID_ARGS";
    case MX_ERR_BAD_HANDLE: return "MX_ERR_BAD_HANDLE";
    case MX_ERR_WRONG_TYPE: return "MX_ERR_WRONG_TYPE";
    case MX_ERR_BAD_SYSCALL: return "MX_ERR_BAD_SYSCALL";
    case MX_ERR_OUT_OF_RANGE: return "MX_ERR_OUT_OF_RANGE";
    case MX_ERR_BUFFER_TOO_SMALL: return "MX_ERR_BUFFER_TOO_SMALL";
    case MX_ERR_BAD_STATE: return "MX_ERR_BAD_STATE";
    case MX_ERR_TIMED_OUT: return "MX_ERR_TIMED_OUT";
    case MX_ERR_SHOULD_WAIT: return "MX_ERR_SHOULD_WAIT";
    case MX_ERR_CANCELED: return "MX_ERR_CANCELED";
    case MX_ERR_PEER_CLOSED: return "MX_ERR_PEER_CLOSED";
    case MX_ERR_NOT_FOUND: return "MX_ERR_NOT_FOUND";
    case MX_ERR_ALREADY_EXISTS: return "MX_ERR_ALREADY_EXISTS";
    case MX_ERR_ALREADY_BOUND: return "MX_ERR_ALREADY_BOUND";
    case MX_ERR_UNAVAILABLE: return "MX_ERR_UNAVAILABLE";
    case MX_ERR_ACCESS_DENIED: return "MX_ERR_ACCESS_DENIED";
    case MX_ERR_IO: return "MX_ERR_IO";
    case MX_ERR_IO_REFUSED: return "MX_ERR_IO_REFUSED";
    case MX_ERR_IO_DATA_INTEGRITY: return "MX_ERR_IO_DATA_INTEGRITY";
    case MX_ERR_IO_DATA_LOSS: return "MX_ERR_IO_DATA_LOSS";
    case MX_ERR_IO_NOT_PRESENT: return "MX_ERR_IO_NOT_PRESENT";
    case MX_ERR_BAD_PATH: return "MX_ERR_BAD_PATH";
    case MX_ERR_NOT_DIR: return "MX_ERR_NOT_DIR";
    case MX_ERR_NOT_FILE: return "MX_ERR_NOT_FILE";
    case MX_ERR_FILE_BIG: return "MX_ERR_FILE_BIG";
    case MX_ERR_NO_SPACE: return "MX_ERR_NO_SPACE";
    case MX_ERR_STOP: return "MX_ERR_STOP";
    case MX_ERR_NEXT: return "MX_ERR_NEXT";
    case MX_ERR_PROTOCOL_NOT_SUPPORTED: return "MX_ERR_PROTOCOL_NOT_SUPPORTED";
    case MX_ERR_ADDRESS_UNREACHABLE: return "MX_ERR_ADDRESS_UNREACHABLE";
    case MX_ERR_ADDRESS_IN_USE: return "MX_ERR_ADDRESS_IN_USE";
    case MX_ERR_NOT_CONNECTED: return "MX_ERR_NOT_CONNECTED";
    case MX_ERR_CONNECTION_REFUSED: return "MX_ERR_CONNECTION_REFUSED";
    case MX_ERR_CONNECTION_RESET: return "MX_ERR_CONNECTION_RESET";
    case MX_ERR_CONNECTION_ABORTED: return "MX_ERR_CONNECTION_ABORTED";
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

VDSO_INTERFACE_FUNCTION(mx_status_get_string);

// Generated with:
// grep '#define'  system/public/magenta/errors.h | grep -v MX_OK |
// sed 's/.*MX_ERR_/MX_ERR_/g' | sed 's/\s.*//g' |
// awk '{print "case "$1": return \""$1"\";";}'
