// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

namespace debug_ipc {

// This file tracks //zircon/system/public/zircon/errors.h
// That file holds the zx_status_t definitions and it's not bound to change
// much, so copying it should not provide much headaches.
//
// The constant naming convention is an exception and is maintained to be
// consistent with the naming used in zircon.

// As defined in zircon/types.h
using zx_status_t = int32_t;

constexpr zx_status_t kZxOk = 0;

// ======= Internal failures =======
// ZX_ERR_INTERNAL: The system encountered an otherwise unspecified error
// while performing the operation.
constexpr zx_status_t kZxErrInternal = -1;

// ZX_ERR_NOT_SUPPORTED: The operation is not implemented, supported,
// or enabled.
constexpr zx_status_t kZxErrNotSupported = -2;

// ZX_ERR_NO_RESOURCES: The system was not able to allocate some resource
// needed for the operation.
constexpr zx_status_t kZxErrNoResources = -3;

// ZX_ERR_NO_MEMORY: The system was not able to allocate memory needed
// for the operation.
constexpr zx_status_t kZxErrNoMemory = -4;

// -5 used to be ZX_ERR_CALL_FAILED.

// ZX_ERR_INTERNAL_INTR_RETRY: The system call was interrupted, but should be
// retried.  This should not be seen outside of the VDSO.
constexpr zx_status_t kZxErrInternalIntrRetry = -6;

// ======= Parameter errors =======
// ZX_ERR_INVALID_ARGS: an argument is invalid, ex. null pointer
constexpr zx_status_t kZxErrInvalidArgs = -10;

// ZX_ERR_BAD_HANDLE: A specified handle value does not refer to a handle.
constexpr zx_status_t kZxErrBadHandle = -11;

// ZX_ERR_WRONG_TYPE: The subject of the operation is the wrong type to
// perform the operation.
// Example: Attempting a message_read on a thread handle.
constexpr zx_status_t kZxErrWrongType = -12;

// ZX_ERR_BAD_SYSCALL: The specified syscall number is invalid.
constexpr zx_status_t kZxErrBadSyscall = -13;

// ZX_ERR_OUT_OF_RANGE: An argument is outside the valid range for this
// operation.
constexpr zx_status_t kZxErrOutOfRange = -14;

// ZX_ERR_BUFFER_TOO_SMALL: A caller provided buffer is too small for
// this operation.
constexpr zx_status_t kZxErrBufferTooSmall = -15;

// ======= Precondition or state errors =======
// ZX_ERR_BAD_STATE: operation failed because the current state of the
// object does not allow it, or a precondition of the operation is
// not satisfied
constexpr zx_status_t kZxErrBadState = -20;

// ZX_ERR_TIMED_OUT: The time limit for the operation elapsed before
// the operation completed.
constexpr zx_status_t kZxErrTimedOut = -21;

// ZX_ERR_SHOULD_WAIT: The operation cannot be performed currently but
// potentially could succeed if the caller waits for a prerequisite
// to be satisfied, for example waiting for a handle to be readable
// or writable.
// Example: Attempting to read from a channel that has no
// messages waiting but has an open remote will return ZX_ERR_SHOULD_WAIT.
// Attempting to read from a channel that has no messages waiting
// and has a closed remote end will return ZX_ERR_PEER_CLOSED.
constexpr zx_status_t kZxErrShouldWait = -22;

// ZX_ERR_CANCELED: The in-progress operation (e.g. a wait) has been
// canceled.
constexpr zx_status_t kZxErrCanceled = -23;

// ZX_ERR_PEER_CLOSED: The operation failed because the remote end of the
// subject of the operation was closed.
constexpr zx_status_t kZxErrPeerClosed = -24;

// ZX_ERR_NOT_FOUND: The requested entity is not found.
constexpr zx_status_t kZxErrNotFound = -25;

// ZX_ERR_ALREADY_EXISTS: An object with the specified identifier
// already exists.
// Example: Attempting to create a file when a file already exists
// with that name.
constexpr zx_status_t kZxErrAlreadyExists = -26;

// ZX_ERR_ALREADY_BOUND: The operation failed because the named entity
// is already owned or controlled by another entity. The operation
// could succeed later if the current owner releases the entity.
constexpr zx_status_t kZxErrAlreadyBound = -27;

// ZX_ERR_UNAVAILABLE: The subject of the operation is currently unable
// to perform the operation.
// Note: This is used when there's no direct way for the caller to
// observe when the subject will be able to perform the operation
// and should thus retry.
constexpr zx_status_t kZxErrUnavailable = -28;

// ======= Permission check errors =======
// ZX_ERR_ACCESS_DENIED: The caller did not have permission to perform
// the specified operation.
constexpr zx_status_t kZxErrAccessDenied = -30;

// ======= Input-output errors =======
// ZX_ERR_IO: Otherwise unspecified error occurred during I/O.
constexpr zx_status_t kZxErrIO = -40;

// ZX_ERR_REFUSED: The entity the I/O operation is being performed on
// rejected the operation.
// Example: an I2C device NAK'ing a transaction or a disk controller
// rejecting an invalid command, or a stalled USB endpoint.
constexpr zx_status_t kZxErrIoRefused = -41;

// ZX_ERR_IO_DATA_INTEGRITY: The data in the operation failed an integrity
// check and is possibly corrupted.
// Example: CRC or Parity error.
constexpr zx_status_t kZxErrIoDataIntegrity = -42;

// ZX_ERR_IO_DATA_LOSS: The data in the operation is currently unavailable
// and may be permanently lost.
// Example: A disk block is irrecoverably damaged.
constexpr zx_status_t kZxErrIoDataLoss = -43;

// ZX_ERR_IO_NOT_PRESENT: The device is no longer available (has been
// unplugged from the system, powered down, or the driver has been
// unloaded)
constexpr zx_status_t kZxErrIoNotPresent = -44;

// ZX_ERR_IO_OVERRUN: More data was received from the device than expected.
// Example: a USB "babble" error due to a device sending more data than
// the host queued to receive.
constexpr zx_status_t kZxErrIoOverrun = -45;

// ZX_ERR_IO_MISSED_DEADLINE: An operation did not complete within the required
// timeframe. Example: A USB isochronous transfer that failed to complete due to
// an overrun or underrun.
constexpr zx_status_t kZxErrIoMissedDeadline = -46;

// ZX_ERR_IO_INVALID: The data in the operation is invalid parameter or is out
// of range. Example: A USB transfer that failed to complete with TRB Error
constexpr zx_status_t kZxErrIoInvalid = -47;

// ======== Filesystem Errors ========
// ZX_ERR_BAD_PATH: Path name is too long.
constexpr zx_status_t kZxErrBadPath = -50;

// ZX_ERR_NOT_DIR: Object is not a directory or does not support
// directory operations.
// Example: Attempted to open a file as a directory or
// attempted to do directory operations on a file.
constexpr zx_status_t kZxErrNotDir = -51;

// ZX_ERR_NOT_FILE: Object is not a regular file.
constexpr zx_status_t kZxErrNotFile = -52;

// ZX_ERR_FILE_BIG: This operation would cause a file to exceed a
// filesystem-specific size limit
constexpr zx_status_t kZxErrFileBig = -53;

// ZX_ERR_NO_SPACE: Filesystem or device space is exhausted.
constexpr zx_status_t kZxErrNoSpace = -54;

// ZX_ERR_NOT_EMPTY: Directory is not empty.
constexpr zx_status_t kZxErrNotEmpty = -55;

// ======== Flow Control ========
// These are not errors, as such, and will never be returned
// by a syscall or public API.  They exist to allow callbacks
// to request changes in operation.
//
// ZX_ERR_STOP: Do not call again.
// Example: A notification callback will be called on every
// event until it returns something other than ZX_OK.
// This status allows differentiation between "stop due to
// an error" and "stop because the work is done."
constexpr zx_status_t kZxErrStop = -60;

// ZX_ERR_NEXT: Advance to the next item.
// Example: A notification callback will use this response
// to indicate it did not "consume" an item passed to it,
// but by choice, not due to an error condition.
constexpr zx_status_t kZxErrNext = -61;

// ZX_ERR_ASYNC: Ownership of the item has moved to an asynchronous worker.
//
// Unlike ZX_ERR_STOP, which implies that iteration on an object
// should stop, and ZX_ERR_NEXT, which implies that iteration
// should continue to the next item, ZX_ERR_ASYNC implies
// that an asynchronous worker is responsible for continuing iteration.
//
// Example: A notification callback will be called on every
// event, but one event needs to handle some work asynchronously
// before it can continue. ZX_ERR_ASYNC implies the worker is
// responsible for resuming iteration once its work has completed.
constexpr zx_status_t kZxErrAsync = -62;

// ======== Network-related errors ========

// ZX_ERR_PROTOCOL_NOT_SUPPORTED: Specified protocol is not
// supported.
constexpr zx_status_t kZxErrProtocolNotSupported = -70;

// ZX_ERR_ADDRESS_UNREACHABLE: Host is unreachable.
constexpr zx_status_t kZxErrAddressUnreachable = -71;

// ZX_ERR_ADDRESS_IN_USE: Address is being used by someone else.
constexpr zx_status_t kZxErrAddressInUse = -72;

// ZX_ERR_NOT_CONNECTED: Socket is not connected.
constexpr zx_status_t kZxErrNotConnected = -73;

// ZX_ERR_CONNECTION_REFUSED: Remote peer rejected the connection.
constexpr zx_status_t kZxErrConnectionRefused = -74;

// ZX_ERR_CONNECTION_RESET: Connection was reset.
constexpr zx_status_t kZxErrConnectionReset = -75;

// ZX_ERR_CONNECTION_ABORTED: Connection was aborted.
constexpr zx_status_t kZxErrConnectionAborted = -76;

}  // namespace debug_ipc
