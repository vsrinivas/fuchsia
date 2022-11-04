// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package netstack

import (
	"syscall/zx"

	"fidl/fuchsia/net/stack"

	"gvisor.dev/gvisor/pkg/tcpip"
)

var _ error = (*TcpIpError)(nil)

// TcpIpError wraps a tcpip.Error.
//
// TcpIpError provides an error implementation over tcpip.Error so that error
// wrapping and unwrapping can be used. It also provides utility methods to
// convert it to Netstack FIDL API returns.
type TcpIpError struct {
	Err tcpip.Error
}

func (e *TcpIpError) Error() string {
	return e.Err.String()
}

// WrapTcpIpError wraps a stack error into a type that implements the error
// interface.
func WrapTcpIpError(e tcpip.Error) *TcpIpError {
	return &TcpIpError{Err: e}
}

// ToStackError transforms the internal tcpip.Error into a FIDL
// fuchsia.net.stack/Error.
// Panics if the internal error is nil.
func (e TcpIpError) ToStackError() stack.Error {
	switch e.Err.(type) {
	case nil:
		panic("Attempted to convert nil tcpip.Error to stack.Error")
	case *tcpip.ErrUnknownProtocol:
		return stack.ErrorNotSupported
	case *tcpip.ErrUnknownNICID:
		return stack.ErrorNotFound
	case *tcpip.ErrUnknownDevice:
		return stack.ErrorNotFound
	case *tcpip.ErrUnknownProtocolOption:
		return stack.ErrorInvalidArgs
	case *tcpip.ErrDuplicateNICID:
		return stack.ErrorAlreadyExists
	case *tcpip.ErrDuplicateAddress:
		return stack.ErrorAlreadyExists
	case *tcpip.ErrHostUnreachable:
		return stack.ErrorInternal
	case *tcpip.ErrAlreadyBound:
		return stack.ErrorAlreadyExists
	case *tcpip.ErrInvalidEndpointState:
		return stack.ErrorBadState
	case *tcpip.ErrAlreadyConnecting:
		return stack.ErrorAlreadyExists
	case *tcpip.ErrAlreadyConnected:
		return stack.ErrorAlreadyExists
	case *tcpip.ErrNoPortAvailable:
		return stack.ErrorInternal
	case *tcpip.ErrPortInUse:
		return stack.ErrorInternal
	case *tcpip.ErrBadLocalAddress:
		return stack.ErrorInvalidArgs
	case *tcpip.ErrClosedForSend:
		return stack.ErrorBadState
	case *tcpip.ErrClosedForReceive:
		return stack.ErrorBadState
	case *tcpip.ErrWouldBlock:
		return stack.ErrorInternal
	case *tcpip.ErrConnectionRefused:
		return stack.ErrorInternal
	case *tcpip.ErrTimeout:
		return stack.ErrorTimeOut
	case *tcpip.ErrAborted:
		return stack.ErrorInternal
	case *tcpip.ErrConnectStarted:
		return stack.ErrorInternal
	case *tcpip.ErrDestinationRequired:
		return stack.ErrorInvalidArgs
	case *tcpip.ErrNotSupported:
		return stack.ErrorNotSupported
	case *tcpip.ErrQueueSizeNotSupported:
		return stack.ErrorNotSupported
	case *tcpip.ErrNotConnected:
		return stack.ErrorInternal
	case *tcpip.ErrConnectionReset:
		return stack.ErrorInternal
	case *tcpip.ErrConnectionAborted:
		return stack.ErrorInternal
	case *tcpip.ErrNoSuchFile:
		return stack.ErrorNotFound
	case *tcpip.ErrInvalidOptionValue:
		return stack.ErrorInvalidArgs
	case *tcpip.ErrBadAddress:
		return stack.ErrorInvalidArgs
	case *tcpip.ErrNetworkUnreachable:
		return stack.ErrorInternal
	case *tcpip.ErrMessageTooLong:
		return stack.ErrorInvalidArgs
	case *tcpip.ErrNoBufferSpace:
		return stack.ErrorInternal
	case *tcpip.ErrBroadcastDisabled:
		return stack.ErrorInternal
	case *tcpip.ErrNotPermitted:
		return stack.ErrorInternal
	case *tcpip.ErrAddressFamilyNotSupported:
		return stack.ErrorNotSupported
	default:
		return stack.ErrorInternal
	}
}

// ToZxStatus transforms the internal tcpip.Error into a zx.Status.
func (e TcpIpError) ToZxStatus() zx.Status {
	switch e.Err.(type) {
	case nil:
		return zx.ErrOk
	case *tcpip.ErrUnknownProtocol:
		return zx.ErrInvalidArgs
	case *tcpip.ErrUnknownNICID:
		return zx.ErrNotFound
	case *tcpip.ErrUnknownDevice:
		return zx.ErrNotFound
	case *tcpip.ErrUnknownProtocolOption:
		return zx.ErrInvalidArgs
	case *tcpip.ErrDuplicateNICID:
		return zx.ErrAlreadyExists
	case *tcpip.ErrDuplicateAddress:
		return zx.ErrAlreadyExists
	case *tcpip.ErrHostUnreachable:
		return zx.ErrAddressUnreachable
	case *tcpip.ErrAlreadyBound:
		return zx.ErrAlreadyBound
	case *tcpip.ErrInvalidEndpointState:
		return zx.ErrBadState
	case *tcpip.ErrAlreadyConnecting:
		return zx.ErrAlreadyBound
	case *tcpip.ErrAlreadyConnected:
		return zx.ErrAlreadyBound
	case *tcpip.ErrNoPortAvailable:
		return zx.ErrNoResources
	case *tcpip.ErrPortInUse:
		return zx.ErrAddressInUse
	case *tcpip.ErrBadLocalAddress:
		return zx.ErrInvalidArgs
	case *tcpip.ErrClosedForSend:
		return zx.ErrBadState
	case *tcpip.ErrClosedForReceive:
		return zx.ErrBadState
	case *tcpip.ErrWouldBlock:
		return zx.ErrShouldWait
	case *tcpip.ErrConnectionRefused:
		return zx.ErrConnectionRefused
	case *tcpip.ErrTimeout:
		return zx.ErrTimedOut
	case *tcpip.ErrAborted:
		return zx.ErrConnectionAborted
	case *tcpip.ErrConnectStarted:
		return zx.ErrInternal
	case *tcpip.ErrDestinationRequired:
		return zx.ErrInvalidArgs
	case *tcpip.ErrNotSupported:
		return zx.ErrNotSupported
	case *tcpip.ErrQueueSizeNotSupported:
		return zx.ErrNotSupported
	case *tcpip.ErrNotConnected:
		return zx.ErrNotConnected
	case *tcpip.ErrConnectionReset:
		return zx.ErrConnectionReset
	case *tcpip.ErrConnectionAborted:
		return zx.ErrConnectionReset
	case *tcpip.ErrNoSuchFile:
		return zx.ErrNotFound
	case *tcpip.ErrInvalidOptionValue:
		return zx.ErrInvalidArgs
	case *tcpip.ErrBadAddress:
		return zx.ErrInvalidArgs
	case *tcpip.ErrNetworkUnreachable:
		return zx.ErrAddressUnreachable
	case *tcpip.ErrMessageTooLong:
		return zx.ErrInvalidArgs
	case *tcpip.ErrNoBufferSpace:
		return zx.ErrNoMemory
	case *tcpip.ErrBroadcastDisabled:
		return zx.ErrInternal
	case *tcpip.ErrNotPermitted:
		return zx.ErrAccessDenied
	case *tcpip.ErrAddressFamilyNotSupported:
		return zx.ErrNotSupported
	default:
		return zx.ErrInternal
	}
}
