// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bindings

import (
	"fidl/system"
)

// InvalidHandle is a handle that will always be invalid.
type InvalidHandle struct {
}

func (h *InvalidHandle) Close() system.MojoResult {
	return system.MOJO_SYSTEM_RESULT_INVALID_ARGUMENT
}

func (h *InvalidHandle) IsValid() bool {
	return false
}

func (h *InvalidHandle) NativeHandle() system.MojoHandle {
	return system.MOJO_HANDLE_INVALID
}

func (h *InvalidHandle) ReleaseNativeHandle() system.MojoHandle {
	return system.MOJO_HANDLE_INVALID
}

func (h *InvalidHandle) ToUntypedHandle() system.UntypedHandle {
	return h
}

func (h *InvalidHandle) Wait(signals system.MojoHandleSignals, deadline system.MojoDeadline) (system.MojoResult, system.MojoHandleSignalsState) {
	return system.MOJO_SYSTEM_RESULT_INVALID_ARGUMENT, system.MojoHandleSignalsState{}
}

func (h *InvalidHandle) ToChannelHandle() system.ChannelHandle {
	return h
}

func (h *InvalidHandle) ToVmoHandle() system.VmoHandle {
	return h
}

func (h *InvalidHandle) ReadData(flags system.MojoReadDataFlags) (system.MojoResult, []byte) {
	return system.MOJO_SYSTEM_RESULT_INVALID_ARGUMENT, nil
}

func (h *InvalidHandle) BeginReadData(flags system.MojoReadDataFlags) (system.MojoResult, []byte) {
	return system.MOJO_SYSTEM_RESULT_INVALID_ARGUMENT, nil
}

func (h *InvalidHandle) EndReadData(numBytesRead int) system.MojoResult {
	return system.MOJO_SYSTEM_RESULT_INVALID_ARGUMENT
}

func (h *InvalidHandle) WriteData(data []byte, flags system.MojoWriteDataFlags) (system.MojoResult, int) {
	return system.MOJO_SYSTEM_RESULT_INVALID_ARGUMENT, 0
}

func (h *InvalidHandle) BeginWriteData(flags system.MojoWriteDataFlags) (system.MojoResult, []byte) {
	return system.MOJO_SYSTEM_RESULT_INVALID_ARGUMENT, nil
}

func (h *InvalidHandle) EndWriteData(numBytesWritten int) system.MojoResult {
	return system.MOJO_SYSTEM_RESULT_INVALID_ARGUMENT
}

func (h *InvalidHandle) ReadMessage(flags system.MojoReadMessageFlags) (system.MojoResult, []byte, []system.UntypedHandle) {
	return system.MOJO_SYSTEM_RESULT_INVALID_ARGUMENT, nil, nil
}

func (h *InvalidHandle) WriteMessage(bytes []byte, handles []system.UntypedHandle, flags system.MojoWriteMessageFlags) system.MojoResult {
	return system.MOJO_SYSTEM_RESULT_INVALID_ARGUMENT
}

func (h *InvalidHandle) DuplicateBufferHandle(opts *system.DuplicateBufferHandleOptions) (system.MojoResult, system.VmoHandle) {
	return system.MOJO_SYSTEM_RESULT_INVALID_ARGUMENT, nil
}

func (h *InvalidHandle) MapBuffer(offset uint64, numBytes int, flags system.MojoMapBufferFlags) (system.MojoResult, []byte) {
	return system.MOJO_SYSTEM_RESULT_INVALID_ARGUMENT, nil
}

func (h *InvalidHandle) UnmapBuffer(buffer []byte) system.MojoResult {
	return system.MOJO_SYSTEM_RESULT_INVALID_ARGUMENT
}

func (h *InvalidHandle) GetBufferInformation() (system.MojoResult, system.MojoBufferInformation) {
	return system.MOJO_SYSTEM_RESULT_INVALID_ARGUMENT, system.MojoBufferInformation{}
}
