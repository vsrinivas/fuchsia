// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package system

import "math"

// Go equivalent definitions of the various system types defined in Mojo.
//
type MojoTimeTicks int64
type MojoHandle uint32
type MojoResult uint32
type MojoDeadline uint64
type MojoHandleSignals uint32
type MojoWriteMessageFlags uint32
type MojoReadMessageFlags uint32
type MojoWriteDataFlags uint32
type MojoReadDataFlags uint32
type MojoCreateChannelOptionsFlags uint32
type MojoCreateVmoOptionsFlags uint32
type MojoDuplicateBufferHandleOptionsFlags uint32
type MojoMapBufferFlags uint32
type MojoBufferInformationFlags uint32

const (
	MOJO_DEADLINE_INDEFINITE MojoDeadline = math.MaxUint64
	MOJO_HANDLE_INVALID      MojoHandle   = 0
	// TODO(vtl): Find a way of supporting the new, more flexible/extensible
	// MojoResult (see mojo/public/c/include/result.h and .../system/result.h).
	MOJO_RESULT_OK                         MojoResult = 0x0000
	MOJO_SYSTEM_RESULT_CANCELLED           MojoResult = 0x0001
	MOJO_SYSTEM_RESULT_UNKNOWN             MojoResult = 0x0002
	MOJO_SYSTEM_RESULT_INVALID_ARGUMENT    MojoResult = 0x0003
	MOJO_SYSTEM_RESULT_DEADLINE_EXCEEDED   MojoResult = 0x0004
	MOJO_SYSTEM_RESULT_NOT_FOUND           MojoResult = 0x0005
	MOJO_SYSTEM_RESULT_ALREADY_EXISTS      MojoResult = 0x0006
	MOJO_SYSTEM_RESULT_PERMISSION_DENIED   MojoResult = 0x0007
	MOJO_SYSTEM_RESULT_RESOURCE_EXHAUSTED  MojoResult = 0x0008
	MOJO_SYSTEM_RESULT_FAILED_PRECONDITION MojoResult = 0x0009
	MOJO_SYSTEM_RESULT_BUSY                MojoResult = 0x0019
	MOJO_SYSTEM_RESULT_ABORTED             MojoResult = 0x000a
	MOJO_SYSTEM_RESULT_OUT_OF_RANGE        MojoResult = 0x000b
	MOJO_SYSTEM_RESULT_UNIMPLEMENTED       MojoResult = 0x000c
	MOJO_SYSTEM_RESULT_INTERNAL            MojoResult = 0x000d
	MOJO_SYSTEM_RESULT_UNAVAILABLE         MojoResult = 0x000e
	MOJO_SYSTEM_RESULT_SHOULD_WAIT         MojoResult = 0x001e
	MOJO_SYSTEM_RESULT_DATA_LOSS           MojoResult = 0x000f

	MOJO_HANDLE_SIGNAL_NONE        MojoHandleSignals = 0
	MOJO_HANDLE_SIGNAL_READABLE    MojoHandleSignals = 1 << 0
	MOJO_HANDLE_SIGNAL_WRITABLE    MojoHandleSignals = 1 << 1
	MOJO_HANDLE_SIGNAL_PEER_CLOSED MojoHandleSignals = 1 << 2

	MOJO_WRITE_MESSAGE_FLAG_NONE       MojoWriteMessageFlags = 0
	MOJO_READ_MESSAGE_FLAG_NONE        MojoReadMessageFlags  = 0
	MOJO_READ_MESSAGE_FLAG_MAY_DISCARD MojoReadMessageFlags  = 1 << 0

	MOJO_READ_DATA_FLAG_NONE         MojoReadDataFlags  = 0
	MOJO_READ_DATA_FLAG_ALL_OR_NONE  MojoReadDataFlags  = 1 << 0
	MOJO_READ_DATA_FLAG_DISCARD      MojoReadDataFlags  = 1 << 1
	MOJO_READ_DATA_FLAG_QUERY        MojoReadDataFlags  = 1 << 2
	MOJO_READ_DATA_FLAG_PEEK         MojoReadDataFlags  = 1 << 3
	MOJO_WRITE_DATA_FLAG_NONE        MojoWriteDataFlags = 0
	MOJO_WRITE_DATA_FLAG_ALL_OR_NONE MojoWriteDataFlags = 1 << 0

	MOJO_CREATE_MESSAGE_PIPE_OPTIONS_FLAG_NONE MojoCreateChannelOptionsFlags = 0

	MOJO_CREATE_SHARED_BUFFER_OPTIONS_FLAG_NONE    MojoCreateVmoOptionsFlags             = 0
	MOJO_DUPLICATE_BUFFER_HANDLE_OPTIONS_FLAG_NONE MojoDuplicateBufferHandleOptionsFlags = 0
	MOJO_MAP_BUFFER_FLAG_NONE                      MojoMapBufferFlags                    = 0
	MOJO_BUFFER_INFORMATION_FLAG_NONE              MojoBufferInformationFlags            = 0
)

// IsReadable returns true iff the |MOJO_HANDLE_SIGNAL_READABLE| bit is set.
func (m MojoHandleSignals) IsReadable() bool {
	return (m & MOJO_HANDLE_SIGNAL_READABLE) != 0
}

// IsWritable returns true iff the |MOJO_HANDLE_SIGNAL_WRITABLE| bit is set.
func (m MojoHandleSignals) IsWritable() bool {
	return (m & MOJO_HANDLE_SIGNAL_WRITABLE) != 0
}

// IsClosed returns true iff the |MOJO_HANDLE_SIGNAL_PEER_CLOSED| bit is set.
func (m MojoHandleSignals) IsClosed() bool {
	return (m & MOJO_HANDLE_SIGNAL_PEER_CLOSED) != 0
}

// MojoHandleSignalsState is a struct returned by wait functions to indicate
// the signaling state of handles.
type MojoHandleSignalsState struct {
	// Signals that were satisfied at some time before the call returned.
	SatisfiedSignals MojoHandleSignals
	// Signals that are possible to satisfy. For example, if the return value
	// was |MOJO_SYSTEM_RESULT_FAILED_PRECONDITION|, you can use this field to
	// determine which, if any, of the signals can still be satisfied.
	SatisfiableSignals MojoHandleSignals
}

// ChannelOptions is used to specify creation parameters for a message pipe.
type ChannelOptions struct {
	Flags MojoCreateChannelOptionsFlags
}

// VmoOptions is used to specify creation parameters for a
// shared buffer.
type VmoOptions struct {
	Flags MojoCreateVmoOptionsFlags
}

// DuplicateBufferHandleOptions is used to specify parameters in
// duplicating access to a shared buffer.
type DuplicateBufferHandleOptions struct {
	Flags MojoDuplicateBufferHandleOptionsFlags
}

// MojoBufferInformation is returned by the GetBufferInformation system
// call.
type MojoBufferInformation struct {
	// Possible values:
	//   MOJO_BUFFER_INFORMATION_FLAG_NONE
	Flags MojoBufferInformationFlags

	// The size of this shared buffer, in bytes.
	NumBytes uint64
}
