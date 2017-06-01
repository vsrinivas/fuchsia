// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package system

// This interface wraps the "raw" mojo system entry points. This has no
// dependencies on other types in this package so it can be implemented
// by code that doesn't depend on this package.
type MojoSystem interface {
	// Shared buffer
	CreateVmo(flags uint32, numBytes uint64) (result uint32, handle uint32)
	DuplicateBufferHandle(handle uint32, flags uint32) (result uint32, dupHandle uint32)
	GetBufferInformation(handle uint32) (result uint32, flags uint32, numBytes uint64)
	// After a successful MapBuffer call, the caller must pass the same slice value to UnmapBuffer to release
	// the underlying memory segment.
	MapBuffer(handle uint32, offset, numBytes uint64, flags uint32) (result uint32, buf []byte)
	UnmapBuffer(buf []byte) (result uint32)

	// Time
	GetTimeTicksNow() (timestamp uint64)

	// Close a handle
	Close(handle uint32) (result uint32)

	// Waiting
	Wait(handle uint32, signals uint32, deadline uint64) (result uint32, satisfiedSignals, satisfiableSignals uint32)
	WaitMany(handles []uint32, signals []uint32, deadline uint64) (result uint32, index int, satisfiedSignals, satisfiableSignals []uint32)

	// Message pipe
	CreateChannel(flags uint32) (result uint32, handle0, handle1 uint32)
	WriteMessage(handle uint32, bytes []byte, handles []uint32, flags uint32) (result uint32)
	ReadMessage(handle uint32, flags uint32) (result uint32, buf []byte, handles []uint32)
}

var sysImpl MojoSystem
