// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/zx_status.h"

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace debug_ipc {

const char* ZxStatusToString(zx_status_t status) {
  switch (status) {
    case kZxOk:
      return "ZX_OK";
    case kZxErrInternal:
      return "ZX_ERR_INTERNAL";
    case kZxErrNotSupported:
      return "ZX_ERR_NOT_SUPPORTED";
    case kZxErrNoResources:
      return "ZX_ERR_NO_RESOURCES";
    case kZxErrNoMemory:
      return "ZX_ERR_NO_MEMORY";
    case kZxErrInternalIntrRetry:
      return "ZX_ERR_INTERNAL_INTR_RETRY";
    case kZxErrInvalidArgs:
      return "ZX_ERR_INVALID_ARGS";
    case kZxErrBadHandle:
      return "ZX_ERR_BAD_HANDLE";
    case kZxErrWrongType:
      return "ZX_ERR_WRONG_TYPE";
    case kZxErrBadSyscall:
      return "ZX_ERR_BAD_SYSCALL";
    case kZxErrOutOfRange:
      return "ZX_ERR_OUT_OF_RANGE";
    case kZxErrBufferTooSmall:
      return "ZX_ERR_BUFFER_TOO_SMALL";
    case kZxErrBadState:
      return "ZX_ERR_BAD_STATE";
    case kZxErrTimedOut:
      return "ZX_ERR_TIMED_OUT";
    case kZxErrShouldWait:
      return "ZX_ERR_SHOULD_WAIT";
    case kZxErrCanceled:
      return "ZX_ERR_CANCELED";
    case kZxErrPeerClosed:
      return "ZX_ERR_PEER_CLOSED";
    case kZxErrNotFound:
      return "ZX_ERR_NOT_FOUND";
    case kZxErrAlreadyExists:
      return "ZX_ERR_ALREADY_EXISTS";
    case kZxErrAlreadyBound:
      return "ZX_ERR_ALREADY_BOUND";
    case kZxErrUnavailable:
      return "ZX_ERR_UNAVAILABLE";
    case kZxErrAccessDenied:
      return "ZX_ERR_ACCESS_DENIED";
    case kZxErrIO:
      return "ZX_ERR_IO";
    case kZxErrIoRefused:
      return "ZX_ERR_REFUSED";
    case kZxErrIoDataIntegrity:
      return "ZX_ERR_IO_DATA_INTEGRITY";
    case kZxErrIoDataLoss:
      return "ZX_ERR_IO_DATA_LOSS";
    case kZxErrIoNotPresent:
      return "ZX_ERR_IO_NOT_PRESENT";
    case kZxErrIoOverrun:
      return "ZX_ERR_IO_OVERRUN";
    case kZxErrIoMissedDeadline:
      return "ZX_ERR_IO_MISSED_DEADLINE";
    case kZxErrIoInvalid:
      return "ZX_ERR_IO_INVALID";
    case kZxErrBadPath:
      return "ZX_ERR_BAD_PATH";
    case kZxErrNotDir:
      return "ZX_ERR_NOT_DIR";
    case kZxErrNotFile:
      return "ZX_ERR_NOT_FILE";
    case kZxErrFileBig:
      return "ZX_ERR_FILE_BIG";
    case kZxErrNoSpace:
      return "ZX_ERR_NO_SPACE";
    case kZxErrNotEmpty:
      return "ZX_ERR_NOT_EMPTY";
    case kZxErrStop:
      return "ZX_ERR_STOP";
    case kZxErrNext:
      return "ZX_ERR_NEXT";
    case kZxErrAsync:
      return "ZX_ERR_ASYNC";
    case kZxErrProtocolNotSupported:
      return "ZX_ERR_PROTOCOL_NOT_SUPPORTED";
    case kZxErrAddressUnreachable:
      return "ZX_ERR_ADDRESS_UNREACHABLE";
    case kZxErrAddressInUse:
      return "ZX_ERR_ADDRESS_IN_USE";
    case kZxErrNotConnected:
      return "ZX_ERR_NOT_CONNECTED";
    case kZxErrConnectionRefused:
      return "ZX_ERR_CONNECTION_REFUSED";
    case kZxErrConnectionReset:
      return "ZX_ERR_CONNECTION_RESET";
    case kZxErrConnectionAborted:
      return "ZX_ERR_CONNECTION_ABORTED";
  }

  return "<unknown>";
}

}  // namespace debug_ipc
