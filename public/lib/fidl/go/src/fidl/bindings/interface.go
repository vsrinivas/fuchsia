// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bindings

import (
	"fidl/system"
	"fmt"
)

// ChannelHandleOwner owns a message pipe handle, it can only pass it
// invalidating itself or close it.
type ChannelHandleOwner struct {
	handle system.ChannelHandle
}

// PassChannel passes ownership of the underlying message pipe handle to
// the newly created handle object, invalidating the underlying handle object
// in the process.
func (o *ChannelHandleOwner) PassChannel() system.ChannelHandle {
	if o.handle == nil {
		return &InvalidHandle{}
	}
	return o.handle.ToUntypedHandle().ToChannelHandle()
}

// Close closes the underlying handle.
func (o *ChannelHandleOwner) Close() {
	if o.handle != nil {
		o.handle.Close()
	}
}

// NewChannelHandleOwner creates |ChannelHandleOwner| that owns the
// provided message pipe handle.
func NewChannelHandleOwner(handle system.ChannelHandle) ChannelHandleOwner {
	return ChannelHandleOwner{handle}
}

// InterfaceRequest represents a request from a remote client for an
// implementation of mojo interface over a specified message pipe. The
// implementor of the interface should remove the message pipe by calling
// PassChannel() and attach it to the implementation.
type InterfaceRequest struct {
	ChannelHandleOwner
}

// InterfacePointer owns a message pipe handle with an implementation of mojo
// interface attached to the other end of the message pipe. The client of the
// interface should remove the message pipe by calling PassChannel() and
// attach it to the proxy.
type InterfacePointer struct {
	ChannelHandleOwner
}

// CreateChannelForInterface creates a message pipe with interface request
// on one end and interface pointer on the other end. The interface request
// should be attached to appropriate mojo interface implementation and
// the interface pointer should be attached to mojo interface proxy.
func CreateChannelForMojoInterface() (InterfaceRequest, InterfacePointer) {
	r, h0, h1 := system.GetCore().CreateChannel(nil)
	if r != system.MOJO_RESULT_OK {
		panic(fmt.Sprintf("can't create a message pipe: %v", r))
	}
	return InterfaceRequest{ChannelHandleOwner{h0}}, InterfacePointer{ChannelHandleOwner{h1}}
}
