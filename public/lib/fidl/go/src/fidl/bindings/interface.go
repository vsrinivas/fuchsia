// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bindings

import (
	"fmt"

	"syscall/zx"
)

// ChannelHandleOwner owns a channel handle, it can only pass it
// invalidating itself or close it.
type ChannelHandleOwner struct {
	handle zx.Handle
}

// PassChannel passes ownership of the underlying channel handle to
// the newly created handle object, invalidating the underlying handle object
// in the process.
func (o *ChannelHandleOwner) PassChannel() zx.Handle {
	if o.handle == zx.HANDLE_INVALID {
		return zx.HANDLE_INVALID
	}
	handle := o.handle
	o.handle = zx.HANDLE_INVALID
	return handle
}

// Close closes the underlying handle.
func (o *ChannelHandleOwner) Close() {
	if o.handle != zx.HANDLE_INVALID {
		o.handle.Close()
	}
}

// NewChannelHandleOwner creates |ChannelHandleOwner| that owns the
// provided channel handle.
func NewChannelHandleOwner(handle zx.Handle) ChannelHandleOwner {
	return ChannelHandleOwner{handle}
}

// InterfaceRequest represents a request from a remote client for an
// implementation of fidl interface over a specified channel. The
// implementor of the interface should remove the channel by calling
// PassChannel() and attach it to the implementation.
type InterfaceRequest struct {
	ChannelHandleOwner
}

// InterfacePointer owns a channel handle with an implementation of fidl
// interface attached to the other end of the channel. The client of the
// interface should remove the channel by calling PassChannel() and
// attach it to the proxy.
type InterfacePointer struct {
	ChannelHandleOwner
}

// CreateChannelForInterface creates a channel with interface request
// on one end and interface pointer on the other end. The interface request
// should be attached to appropriate fidl interface implementation and
// the interface pointer should be attached to fidl interface proxy.
func CreateChannelForFidlInterface() (InterfaceRequest, InterfacePointer) {
	c0, c1, err := zx.NewChannel(0)
	if err != nil {
		panic(fmt.Sprintf("can't create a channel: %v", err))
	}
	return InterfaceRequest{ChannelHandleOwner{c0.Handle}}, InterfacePointer{ChannelHandleOwner{c1.Handle}}
}
