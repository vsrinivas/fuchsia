// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define MX_OK (0)

// ======= Internal failures =======
// MX_ERR_INTERNAL: The system encountered an otherwise unspecified error
// while performing the operation.
#define MX_ERR_INTERNAL (-1)

// MX_ERR_NOT_SUPPORTED: The operation is not implemented, supported,
// or enabled.
#define MX_ERR_NOT_SUPPORTED (-2)

// MX_ERR_NO_RESOURCES: The system was not able to allocate some resource
// needed for the operation.
#define MX_ERR_NO_RESOURCES (-3)

// MX_ERR_NO_MEMORY: The system was not able to allocate memory needed
// for the operation.
#define MX_ERR_NO_MEMORY (-4)

// MX_ERR_CALL_FAILED: The second phase of mx_channel_call() did not complete
// successfully.
#define MX_ERR_CALL_FAILED (-5)

// MX_ERR_INTERNAL_INTR_RETRY: The system call was interrupted, but should be
// retried.  This should not be seen outside of the VDSO.
#define MX_ERR_INTERNAL_INTR_RETRY (-6)

// ======= Parameter errors =======
// MX_ERR_INVALID_ARGS: an argument is invalid, ex. null pointer
#define MX_ERR_INVALID_ARGS (-10)

// MX_ERR_BAD_HANDLE: A specified handle value does not refer to a handle.
#define MX_ERR_BAD_HANDLE (-11)

// MX_ERR_WRONG_TYPE: The subject of the operation is the wrong type to
// perform the operation.
// Example: Attempting a message_read on a thread handle.
#define MX_ERR_WRONG_TYPE (-12)

// MX_ERR_BAD_SYSCALL: The specified syscall number is invalid.
#define MX_ERR_BAD_SYSCALL (-13)

// MX_ERR_OUT_OF_RANGE: An argument is outside the valid range for this
// operation.
#define MX_ERR_OUT_OF_RANGE (-14)

// MX_ERR_BUFFER_TOO_SMALL: A caller provided buffer is too small for
// this operation.
#define MX_ERR_BUFFER_TOO_SMALL (-15)

// ======= Precondition or state errors =======
// MX_ERR_BAD_STATE: operation failed because the current state of the
// object does not allow it, or a precondition of the operation is
// not satisfied
#define MX_ERR_BAD_STATE (-20)

// MX_ERR_TIMED_OUT: The time limit for the operation elapsed before
// the operation completed.
#define MX_ERR_TIMED_OUT (-21)

// MX_ERR_SHOULD_WAIT: The operation cannot be performed currently but
// potentially could succeed if the caller waits for a prerequisite
// to be satisfied, for example waiting for a handle to be readable
// or writable.
// Example: Attempting to read from a channel that has no
// messages waiting but has an open remote will return MX_ERR_SHOULD_WAIT.
// Attempting to read from a channel that has no messages waiting
// and has a closed remote end will return MX_ERR_PEER_CLOSED.
#define MX_ERR_SHOULD_WAIT (-22)

// MX_ERR_CANCELED: The in-progress operation (e.g. a wait) has been
// canceled.
#define MX_ERR_CANCELED (-23)

// MX_ERR_PEER_CLOSED: The operation failed because the remote end of the
// subject of the operation was closed.
#define MX_ERR_PEER_CLOSED (-24)

// MX_ERR_NOT_FOUND: The requested entity is not found.
#define MX_ERR_NOT_FOUND (-25)

// MX_ERR_ALREADY_EXISTS: An object with the specified identifier
// already exists.
// Example: Attempting to create a file when a file already exists
// with that name.
#define MX_ERR_ALREADY_EXISTS (-26)

// MX_ERR_ALREADY_BOUND: The operation failed because the named entity
// is already owned or controlled by another entity. The operation
// could succeed later if the current owner releases the entity.
#define MX_ERR_ALREADY_BOUND (-27)

// MX_ERR_UNAVAILABLE: The subject of the operation is currently unable
// to perform the operation.
// Note: This is used when there's no direct way for the caller to
// observe when the subject will be able to perform the operation
// and should thus retry.
#define MX_ERR_UNAVAILABLE (-28)


// ======= Permission check errors =======
// MX_ERR_ACCESS_DENIED: The caller did not have permission to perform
// the specified operation.
#define MX_ERR_ACCESS_DENIED (-30)

// ======= Input-output errors =======
// MX_ERR_IO: Otherwise unspecified error occurred during I/O.
#define MX_ERR_IO (-40)

// MX_ERR_REFUSED: The entity the I/O operation is being performed on
// rejected the operation.
// Example: an I2C device NAK'ing a transaction or a disk controller
// rejecting an invalid command, or a stalled USB endpoint.
#define MX_ERR_IO_REFUSED (-41)

// MX_ERR_IO_DATA_INTEGRITY: The data in the operation failed an integrity
// check and is possibly corrupted.
// Example: CRC or Parity error.
#define MX_ERR_IO_DATA_INTEGRITY (-42)

// MX_ERR_IO_DATA_LOSS: The data in the operation is currently unavailable
// and may be permanently lost.
// Example: A disk block is irrecoverably damaged.
#define MX_ERR_IO_DATA_LOSS (-43)

// MX_ERR_IO_NOT_PRESENT: The device is no longer available (has been
// unplugged from the system, powered down, or the driver has been
// unloaded)
#define MX_ERR_IO_NOT_PRESENT (-44)

// MX_ERR_IO_OVERRUN: More data was received from the device than expected.
// Example: a USB "babble" error due to a device sending more data than
// the host queued to receive.
#define MX_ERR_IO_OVERRUN (-45)

// MX_ERR_IO_MISSED_DEADLINE: An operation did not complete within the required timeframe.
// Example: A USB isochronous transfer that failed to complete due to an overrun or underrun.
#define MX_ERR_IO_MISSED_DEADLINE (-46)

// ======== Filesystem Errors ========
// MX_ERR_BAD_PATH: Path name is too long.
#define MX_ERR_BAD_PATH (-50)

// MX_ERR_NOT_DIR: Object is not a directory or does not support
// directory operations.
// Example: Attempted to open a file as a directory or
// attempted to do directory operations on a file.
#define MX_ERR_NOT_DIR (-51)

// MX_ERR_NOT_FILE: Object is not a regular file.
#define MX_ERR_NOT_FILE (-52)

// MX_ERR_FILE_BIG: This operation would cause a file to exceed a
// filesystem-specific size limit
#define MX_ERR_FILE_BIG (-53)

// MX_ERR_NO_SPACE: Filesystem or device space is exhausted.
#define MX_ERR_NO_SPACE (-54)

// MX_ERR_NOT_EMPTY: Directory is not empty.
#define MX_ERR_NOT_EMPTY (-55)

// ======== Flow Control ========
// These are not errors, as such, and will never be returned
// by a syscall or public API.  They exist to allow callbacks
// to request changes in operation.
//
// MX_ERR_STOP: Do not call again.
// Example: A notification callback will be called on every
// event until it returns something other than MX_OK.
// This status allows differentiation between "stop due to
// an error" and "stop because the work is done."
#define MX_ERR_STOP (-60)

// MX_ERR_NEXT: Advance to the next item.
// Example: A notification callback will use this response
// to indicate it did not "consume" an item passed to it,
// but by choice, not due to an error condition.
#define MX_ERR_NEXT (-61)

// ======== Network-related errors ========

// MX_ERR_PROTOCOL_NOT_SUPPORTED: Specified protocol is not
// supported.
#define MX_ERR_PROTOCOL_NOT_SUPPORTED (-70)

// MX_ERR_ADDRESS_UNREACHABLE: Host is unreachable.
#define MX_ERR_ADDRESS_UNREACHABLE (-71)

// MX_ERR_ADDRESS_IN_USE: Address is being used by someone else.
#define MX_ERR_ADDRESS_IN_USE (-72)

// MX_ERR_NOT_CONNECTED: Socket is not connected.
#define MX_ERR_NOT_CONNECTED (-73)

// MX_ERR_CONNECTION_REFUSED: Remote peer rejected the connection.
#define MX_ERR_CONNECTION_REFUSED (-74)

// MX_ERR_CONNECTION_RESET: Connection was reset.
#define MX_ERR_CONNECTION_RESET (-75)

// MX_ERR_CONNECTION_ABORTED: Connection was aborted.
#define MX_ERR_CONNECTION_ABORTED (-76)
