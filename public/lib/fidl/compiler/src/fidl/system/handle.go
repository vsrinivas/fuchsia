// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package system

// Handle is a generic handle for mojo objects.
type Handle interface {
	// IsValid returns whether the handle is valid. A handle is valid until it
	// has been explicitly closed or sent through a message pipe.
	IsValid() bool

	// ToUntypedHandle converts this handle into an UntypedHandle, invalidating
	// this handle.
	ToUntypedHandle() UntypedHandle
}

// UntypedHandle is a a mojo handle of unknown type. This handle can be typed by
// using one of its methods, which will return a handle of the requested type
// and invalidate this object. No validation is made when the conversion
// operation is called.
type UntypedHandle interface {
	Handle

	// ToChannelHandle returns the underlying handle as a ChannelHandle
	// and invalidates this UntypedHandle representation.
	ToChannelHandle() ChannelHandle

	// ToVmoHandle returns the underlying handle as a
	// VmoHandle and invalidates this UntypedHandle representation.
	ToVmoHandle() VmoHandle
}

type untypedHandleImpl struct {
}

func (h *untypedHandleImpl) ToChannelHandle() ChannelHandle {
	return &channel{}
}

func (h *untypedHandleImpl) ToVmoHandle() VmoHandle {
	return &sharedBuffer{}
}

// ChannelHandle is a handle for a bidirectional communication channel for
// framed data (i.e., messages). Messages can contain plain data and/or Mojo
// handles.
type ChannelHandle interface {
}

type channel struct {
}

// VmoHandle is a handle for a buffer that can be shared between
// applications.
type VmoHandle interface {
}

type sharedBuffer struct {
}
