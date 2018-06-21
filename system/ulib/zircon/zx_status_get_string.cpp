// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/status.h>

#include "private.h"

const char* _zx_status_get_string(zx_status_t status) {
    switch (status) {
    case ZX_OK: return "ZX_OK";
    case ZX_ERR_INTERNAL: return "ZX_ERR_INTERNAL";
    case ZX_ERR_NOT_SUPPORTED: return "ZX_ERR_NOT_SUPPORTED";
    case ZX_ERR_NO_RESOURCES: return "ZX_ERR_NO_RESOURCES";
    case ZX_ERR_NO_MEMORY: return "ZX_ERR_NO_MEMORY";
    case ZX_ERR_INTERNAL_INTR_RETRY: return "ZX_ERR_INTERNAL_INTR_KILLED_RETRY";
    case ZX_ERR_INVALID_ARGS: return "ZX_ERR_INVALID_ARGS";
    case ZX_ERR_BAD_HANDLE: return "ZX_ERR_BAD_HANDLE";
    case ZX_ERR_WRONG_TYPE: return "ZX_ERR_WRONG_TYPE";
    case ZX_ERR_BAD_SYSCALL: return "ZX_ERR_BAD_SYSCALL";
    case ZX_ERR_OUT_OF_RANGE: return "ZX_ERR_OUT_OF_RANGE";
    case ZX_ERR_BUFFER_TOO_SMALL: return "ZX_ERR_BUFFER_TOO_SMALL";
    case ZX_ERR_BAD_STATE: return "ZX_ERR_BAD_STATE";
    case ZX_ERR_TIMED_OUT: return "ZX_ERR_TIMED_OUT";
    case ZX_ERR_SHOULD_WAIT: return "ZX_ERR_SHOULD_WAIT";
    case ZX_ERR_CANCELED: return "ZX_ERR_CANCELED";
    case ZX_ERR_PEER_CLOSED: return "ZX_ERR_PEER_CLOSED";
    case ZX_ERR_NOT_FOUND: return "ZX_ERR_NOT_FOUND";
    case ZX_ERR_ALREADY_EXISTS: return "ZX_ERR_ALREADY_EXISTS";
    case ZX_ERR_ALREADY_BOUND: return "ZX_ERR_ALREADY_BOUND";
    case ZX_ERR_UNAVAILABLE: return "ZX_ERR_UNAVAILABLE";
    case ZX_ERR_ACCESS_DENIED: return "ZX_ERR_ACCESS_DENIED";
    case ZX_ERR_IO: return "ZX_ERR_IO";
    case ZX_ERR_IO_REFUSED: return "ZX_ERR_IO_REFUSED";
    case ZX_ERR_IO_INVALID: return "ZX_ERR_IO_INVALID";
    case ZX_ERR_IO_DATA_INTEGRITY: return "ZX_ERR_IO_DATA_INTEGRITY";
    case ZX_ERR_IO_DATA_LOSS: return "ZX_ERR_IO_DATA_LOSS";
    case ZX_ERR_IO_NOT_PRESENT: return "ZX_ERR_IO_NOT_PRESENT";
    case ZX_ERR_BAD_PATH: return "ZX_ERR_BAD_PATH";
    case ZX_ERR_NOT_DIR: return "ZX_ERR_NOT_DIR";
    case ZX_ERR_NOT_FILE: return "ZX_ERR_NOT_FILE";
    case ZX_ERR_FILE_BIG: return "ZX_ERR_FILE_BIG";
    case ZX_ERR_NO_SPACE: return "ZX_ERR_NO_SPACE";
    case ZX_ERR_STOP: return "ZX_ERR_STOP";
    case ZX_ERR_NEXT: return "ZX_ERR_NEXT";
    case ZX_ERR_PROTOCOL_NOT_SUPPORTED: return "ZX_ERR_PROTOCOL_NOT_SUPPORTED";
    case ZX_ERR_ADDRESS_UNREACHABLE: return "ZX_ERR_ADDRESS_UNREACHABLE";
    case ZX_ERR_ADDRESS_IN_USE: return "ZX_ERR_ADDRESS_IN_USE";
    case ZX_ERR_NOT_CONNECTED: return "ZX_ERR_NOT_CONNECTED";
    case ZX_ERR_CONNECTION_REFUSED: return "ZX_ERR_CONNECTION_REFUSED";
    case ZX_ERR_CONNECTION_RESET: return "ZX_ERR_CONNECTION_RESET";
    case ZX_ERR_CONNECTION_ABORTED: return "ZX_ERR_CONNECTION_ABORTED";
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

VDSO_INTERFACE_FUNCTION(zx_status_get_string);

// Generated with:
// grep '#define'  system/public/zircon/errors.h | grep -v ZX_OK |
// sed 's/.*ZX_ERR_/ZX_ERR_/g' | sed 's/\s.*//g' |
// awk '{print "case "$1": return \""$1"\";";}'
