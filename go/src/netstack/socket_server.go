// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/binary"
	"fmt"
	"log"
	"sync"
	"syscall/zx"
	"syscall/zx/fdio"
	"syscall/zx/mxerror"
	"syscall/zx/zxsocket"
	"syscall/zx/zxwait"
	"time"

	"app/context"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/buffer"
	"github.com/google/netstack/tcpip/header"
	"github.com/google/netstack/tcpip/network/ipv4"
	"github.com/google/netstack/tcpip/network/ipv6"
	"github.com/google/netstack/tcpip/stack"
	"github.com/google/netstack/tcpip/transport/ping"
	"github.com/google/netstack/tcpip/transport/tcp"
	"github.com/google/netstack/tcpip/transport/udp"
	"github.com/google/netstack/waiter"
)

// #cgo CFLAGS: -D_GNU_SOURCE
// #cgo CFLAGS: -I${SRCDIR}/../../../../zircon/system/ulib/zxs/include
// #cgo CFLAGS: -I${SRCDIR}/../../../../zircon/third_party/ulib/musl/include/
// #cgo CFLAGS: -I${SRCDIR}/../../../public
// #include <lib/zxs/protocol.h>
// #include <netinet/tcp.h>
// #include <lib/netstack/c/netconfig.h>
import "C"

const debug = false

// TODO: Replace these with a better tracing mechanism (NET-757)
const logListen = false
const logAccept = false

const ZXSIO_SIGNAL_INCOMING = zx.SignalUser0
const ZXSIO_SIGNAL_OUTGOING = zx.SignalUser1
const ZXSIO_SIGNAL_CONNECTED = zx.SignalUser3
const LOCAL_SIGNAL_CLOSING = zx.SignalUser5

func sendSignal(s zx.Socket, sig zx.Signals, peer bool) error {
	var err error
	if peer {
		err = s.Handle().SignalPeer(0, sig)
	} else {
		err = s.Handle().Signal(0, sig)
	}
	switch status := mxerror.Status(err); status {
	case zx.ErrOk:
	case zx.ErrBadHandle, zx.ErrPeerClosed:
		// The peer might have closed the handle.
	default:
		return err
	}
	return nil
}

func signalConnectFailure(s zx.Socket) error {
	return sendSignal(s, ZXSIO_SIGNAL_OUTGOING, true)
}

func signalConnectSuccess(s zx.Socket, outgoing bool) error {
	// CONNECTED should be sent to the peer before it is sent locally.
	// That ensures the peer detects the connection before any data is written by
	// loopStreamRead.
	err := sendSignal(s, ZXSIO_SIGNAL_OUTGOING|ZXSIO_SIGNAL_CONNECTED, true)
	if err != nil {
		return err
	}
	return sendSignal(s, ZXSIO_SIGNAL_CONNECTED, false)
}

func newSocketServer(stk *stack.Stack, ctx *context.Context) (*socketServer, error) {
	a := socketServer{
		stack: stk,
		io:    make(map[cookie]*iostate),
		next:  1,
	}
	return &a, nil
}

func (s *socketServer) setNetstack(ns *Netstack) {
	s.ns = ns
}

type cookie int64

type iostate struct {
	wq *waiter.Queue
	ep tcpip.Endpoint

	netProto   tcpip.NetworkProtocolNumber   // IPv4 or IPv6
	transProto tcpip.TransportProtocolNumber // TCP or UDP

	dataHandle zx.Socket // used to communicate with libc

	mu        sync.Mutex
	refs      int
	lastError *tcpip.Error // if not-nil, next error returned via getsockopt

	loopWriteDone  chan struct{} // report that loop[Stream|Dgram]Write finished
	loopListenDone chan struct{} // report that loopListen finished

	closing chan struct{}
}

// loopStreamWrite connects libc write to the network stack for TCP sockets.
//
// As written, we have two netstack threads per socket.
// That's not so bad for small client work, but even a client OS is
// eventually going to feel the overhead of this.
func (ios *iostate) loopStreamWrite(stk *stack.Stack) {
	// Warm up.
	_, err := zxwait.Wait(zx.Handle(ios.dataHandle),
		zx.SignalSocketReadable|zx.SignalSocketReadDisabled|
			zx.SignalSocketPeerClosed|LOCAL_SIGNAL_CLOSING,
		zx.TimensecInfinite)
	switch mxerror.Status(err) {
	case zx.ErrOk:
		// NOP
	case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
		return
	default:
		log.Printf("loopStreamWrite: warmup failed: %v", err)
	}

	// The client might have written some data into the socket.
	// Always continue to the 'for' loop below and try to read them
	// even if the signals show the client has closed the dataHandle.

	waitEntry, notifyCh := waiter.NewChannelEntry(nil)
	for {
		// TODO: obviously allocating for each read is silly.
		// A quick hack we can do is store these in a ring buffer,
		// as the lifecycle of this buffer.View starts here, and
		// ends in nearby code we control in link.go.
		v := buffer.NewView(2048)
		n, err := ios.dataHandle.Read([]byte(v), 0)
		switch mxerror.Status(err) {
		case zx.ErrOk:
			// Success. Pass the data to the endpoint and loop.
		case zx.ErrBadState:
			// This side of the socket is closed.
			err := ios.ep.Shutdown(tcpip.ShutdownWrite)
			if err != nil {
				log.Printf("loopStreamWrite: ShutdownWrite failed: %v", err)
			}
			return
		case zx.ErrShouldWait:
			obs, err := zxwait.Wait(zx.Handle(ios.dataHandle),
				zx.SignalSocketReadable|zx.SignalSocketReadDisabled|
					zx.SignalSocketPeerClosed|LOCAL_SIGNAL_CLOSING,
				zx.TimensecInfinite)
			switch mxerror.Status(err) {
			case zx.ErrOk:
				// Handle signal below.
			case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
				return
			default:
				log.Printf("loopStreamWrite: wait failed: %v", err)
				return
			}
			switch {
			case obs&zx.SignalSocketReadDisabled != 0:
				// The next Read will return zx.BadState.
				continue
			case obs&zx.SignalSocketReadable != 0:
				continue
			case obs&LOCAL_SIGNAL_CLOSING != 0:
				return
			case obs&zx.SignalSocketPeerClosed != 0:
				return
			}
		case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
			return
		default:
			log.Printf("socket read failed: %v", err) // TODO: communicate this
			continue
		}
		v = v[:n]

		if debug {
			log.Printf("loopStreamWrite: sending packet n=%d, v=%q", n, v)
		}

		if err := func() *tcpip.Error {
			ios.wq.EventRegister(&waitEntry, waiter.EventOut)
			defer ios.wq.EventUnregister(&waitEntry)

			for {
				n, resCh, err := ios.ep.Write(tcpip.SlicePayload(v), tcpip.WriteOptions{})
				if resCh != nil {
					if err != tcpip.ErrNoLinkAddress {
						panic(fmt.Sprintf("err=%v inconsistent with presence of resCh", err))
					}
					panic(fmt.Sprintf("TCP link address resolutions happen on connect; saw %d/%d", n, len(v)))
				}
				if err == tcpip.ErrWouldBlock {
					// Note that Close should not interrupt this wait.
					<-notifyCh
					continue
				}
				if err != nil {
					return err
				}
				v = v[n:]
				if len(v) == 0 {
					return nil
				}
			}
		}(); err != nil {
			log.Printf("loopStreamWrite: got endpoint error: %v (TODO)", err)
			return
		}
	}
}

// loopStreamRead connects libc read to the network stack for TCP sockets.
func (ios *iostate) loopStreamRead(stk *stack.Stack) {
	// Warm up.
	writable := false
	connected := false
	for !(writable && connected) {
		sigs := zx.Signals(zx.SignalSocketWriteDisabled | zx.SignalSocketPeerClosed | LOCAL_SIGNAL_CLOSING)
		if !writable {
			sigs |= zx.SignalSocketWritable
		}
		if !connected {
			sigs |= ZXSIO_SIGNAL_CONNECTED
		}
		obs, err := zxwait.Wait(zx.Handle(ios.dataHandle), sigs, zx.TimensecInfinite)
		switch mxerror.Status(err) {
		case zx.ErrOk:
			// NOP
		case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
			return
		default:
			log.Printf("loopStreamRead: warmup failed: %v", err)
		}
		if obs&zx.SignalSocketWritable != 0 {
			writable = true
		}
		if obs&ZXSIO_SIGNAL_CONNECTED != 0 {
			connected = true
		}
		if obs&zx.SignalSocketPeerClosed != 0 {
			return
		}
		if obs&LOCAL_SIGNAL_CLOSING != 0 {
			return
		}
		if obs&zx.SignalSocketWriteDisabled != 0 {
			err := ios.ep.Shutdown(tcpip.ShutdownRead)
			if err != nil {
				log.Printf("loopStreamRead: ShutdownRead failed: %v", err)
			}
			return
		}
	}

	waitEntry, notifyCh := waiter.NewChannelEntry(nil)
	for {
		ios.wq.EventRegister(&waitEntry, waiter.EventIn)
		var v buffer.View
		var err *tcpip.Error
		for {
			v, _, err = ios.ep.Read(nil)
			if err == nil {
				break
			} else if err == tcpip.ErrWouldBlock || err == tcpip.ErrInvalidEndpointState || err == tcpip.ErrNotConnected {
				if debug {
					log.Printf("loopStreamRead read err=%v", err)
				}
				select {
				case <-notifyCh:
					continue
				case <-ios.closing:
					// TODO: write a unit test that exercises this.
					return
				}
			} else if err == tcpip.ErrClosedForReceive || err == tcpip.ErrConnectionRefused {
				if err == tcpip.ErrConnectionRefused {
					ios.lastError = err
				}
				err := ios.dataHandle.Shutdown(zx.SocketShutdownWrite)
				switch mxerror.Status(err) {
				case zx.ErrOk:
				case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
				default:
					log.Printf("socket read: shutdown failed: %v", err)
				}
				return
			}
			log.Printf("loopStreamRead got endpoint error: %v (TODO)", err)
			return
		}
		ios.wq.EventUnregister(&waitEntry)
		if debug {
			log.Printf("loopStreamRead: got a buffer, len(v)=%d", len(v))
		}

	writeLoop:
		for len(v) > 0 {
			n, err := ios.dataHandle.Write([]byte(v), 0)
			v = v[n:]
			switch mxerror.Status(err) {
			case zx.ErrOk:
				// Success. Loop and keep writing.
			case zx.ErrBadState:
				// This side of the socket is closed.
				err := ios.ep.Shutdown(tcpip.ShutdownRead)
				if err != nil {
					log.Printf("loopStreamRead: ShutdownRead failed: %v", err)
				}
				return
			case zx.ErrShouldWait:
				if debug {
					log.Printf("loopStreamRead: got zx.ErrShouldWait")
				}
				obs, err := zxwait.Wait(zx.Handle(ios.dataHandle),
					zx.SignalSocketWritable|zx.SignalSocketWriteDisabled|
						zx.SignalSocketPeerClosed|LOCAL_SIGNAL_CLOSING,
					zx.TimensecInfinite)
				switch mxerror.Status(err) {
				case zx.ErrOk:
					// Handle signal below.
				case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
					return
				default:
					log.Printf("loopStreamRead: wait failed: %v", err)
					return
				}
				switch {
				case obs&zx.SignalSocketPeerClosed != 0:
					return
				case obs&LOCAL_SIGNAL_CLOSING != 0:
					return
				case obs&zx.SignalSocketWriteDisabled != 0:
					// The next Write will return zx.ErrBadState.
					continue
				case obs&zx.SignalSocketWritable != 0:
					continue
				}
			case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
				return
			default:
				log.Printf("socket write failed: %v", err) // TODO: communicate this
				break writeLoop
			}
		}
	}
}

// loopDgramRead connects libc read to the network stack for UDP messages.
func (ios *iostate) loopDgramRead(stk *stack.Stack) {
	waitEntry, notifyCh := waiter.NewChannelEntry(nil)
	for {
		ios.wq.EventRegister(&waitEntry, waiter.EventIn)
		var sender tcpip.FullAddress
		var v buffer.View
		var err *tcpip.Error
		for {
			v, _, err = ios.ep.Read(&sender)
			if err == nil {
				break
			} else if err == tcpip.ErrWouldBlock {
				select {
				case <-notifyCh:
					continue
				case <-ios.closing:
					return
				}
			} else if err == tcpip.ErrClosedForReceive {
				if debug {
					log.Printf("TODO loopDgramRead closed")
				}
				// TODO _, err := ios.dataHandle.Write(nil, ZX_SOCKET_HALF_CLOSE)
				return
			}
			// TODO communicate to user
			log.Printf("loopDgramRead got endpoint error: %v (TODO)", err)
			return
		}
		ios.wq.EventUnregister(&waitEntry)

		out := make([]byte, C.FDIO_SOCKET_MSG_HEADER_SIZE+len(v))
		if err := func() error {
			var fdioSocketMsg C.struct_fdio_socket_msg
			n, err := fdioSocketMsg.addr.Encode(sender)
			if err != nil {
				return err
			}
			fdioSocketMsg.addrlen = C.socklen_t(n)
			if _, err := fdioSocketMsg.MarshalTo(out[:C.FDIO_SOCKET_MSG_HEADER_SIZE]); err != nil {
				return err
			}
			return nil
		}(); err != nil {
			// TODO communicate to user
			log.Printf("writeSocketMsgHdr failed: %v (TODO)", err)
		}
		if n := copy(out[C.FDIO_SOCKET_MSG_HEADER_SIZE:], v); n < len(v) {
			panic(fmt.Sprintf("copied %d/%d bytes", n, len(v)))
		}

	writeLoop:
		for {
			_, err := ios.dataHandle.Write(out, 0)
			switch mxerror.Status(err) {
			case zx.ErrOk:
				break writeLoop
			case zx.ErrBadState:
				return // This side of the socket is closed.
			case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
				return
			default:
				log.Printf("socket write failed: %v", err) // TODO: communicate this
				break writeLoop
			}
		}
	}
}

// loopDgramWrite connects libc write to the network stack for UDP messages.
func (ios *iostate) loopDgramWrite(stk *stack.Stack) {
	for {
		v := buffer.NewView(2048)
		n, err := ios.dataHandle.Read([]byte(v), 0)
		switch mxerror.Status(err) {
		case zx.ErrOk:
			// Success. Pass the data to the endpoint and loop.
		case zx.ErrBadState:
			return // This side of the socket is closed.
		case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
			return
		case zx.ErrShouldWait:
			obs, err := zxwait.Wait(zx.Handle(ios.dataHandle),
				zx.SignalSocketReadable|zx.SignalSocketPeerClosed|LOCAL_SIGNAL_CLOSING,
				zx.TimensecInfinite)
			switch mxerror.Status(err) {
			case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
				return
			case zx.ErrOk:
				switch {
				case obs&zx.SignalChannelReadable != 0:
					continue
				case obs&LOCAL_SIGNAL_CLOSING != 0:
					return
				case obs&zx.SignalSocketPeerClosed != 0:
					return
				}
			default:
				log.Printf("loopDgramWrite wait failed: %v", err)
				return
			}
		default:
			log.Printf("loopDgramWrite failed: %v", err) // TODO: communicate this
			continue
		}
		v = v[:n]

		receiver, err := func() (*tcpip.FullAddress, error) {
			var fdioSocketMsg C.struct_fdio_socket_msg
			if err := fdioSocketMsg.Unmarshal(v[:C.FDIO_SOCKET_MSG_HEADER_SIZE]); err != nil {
				return nil, err
			}
			if fdioSocketMsg.addrlen == 0 {
				return nil, nil
			}
			addr, err := fdioSocketMsg.addr.Decode()
			if err != nil {
				return nil, err
			}
			return &addr, nil
		}()
		if err != nil {
			// TODO communicate
			log.Printf("loopDgramWrite: bad socket msg header: %v", err)
			continue
		}
		v = v[C.FDIO_SOCKET_MSG_HEADER_SIZE:]

		if err := func() *tcpip.Error {
			for {
				n, resCh, err := ios.ep.Write(tcpip.SlicePayload(v), tcpip.WriteOptions{To: receiver})
				if resCh != nil {
					if err != tcpip.ErrNoLinkAddress {
						panic(fmt.Sprintf("err=%v inconsistent with presence of resCh", err))
					}
					<-resCh
					continue
				}
				if err == tcpip.ErrWouldBlock {
					panic(fmt.Sprintf("UDP writes are nonblocking; saw %d/%d", n, len(v)))
				}
				if err != nil {
					return err
				}
				if int(n) < len(v) {
					panic(fmt.Sprintf("UDP disallowes short writes; saw: %d/%d", n, len(v)))
				}
				return nil
			}
		}(); err != nil {
			log.Printf("loopDgramWrite: got endpoint error: %v (TODO)", err)
			return
		}
	}
}

func (ios *iostate) loopControl(s *socketServer, cookie int64) {
	synthesizeClose := true
	defer func() {
		if synthesizeClose {
			switch err := zxsocket.Handler(0, zxsocket.ServerHandler(s.zxsocketHandler), cookie); mxerror.Status(err) {
			case zx.ErrOk:
			default:
				log.Printf("synethsize close failed: %v", err)
			}
		}

		if err := ios.dataHandle.Close(); err != nil {
			log.Printf("dataHandle.Close() failed: %v", err)
		}
	}()

	for {
		switch err := zxsocket.Handler(ios.dataHandle, zxsocket.ServerHandler(s.zxsocketHandler), cookie); mxerror.Status(err) {
		case zx.ErrOk:
			// Success. Pass the data to the endpoint and loop.
		case zx.ErrBadState:
			return // This side of the socket is closed.
		case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
			return
		case zx.ErrShouldWait:
			obs, err := zxwait.Wait(zx.Handle(ios.dataHandle),
				zx.SignalSocketControlReadable|zx.SignalSocketPeerClosed|LOCAL_SIGNAL_CLOSING,
				zx.TimensecInfinite)
			switch mxerror.Status(err) {
			case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
				return
			case zx.ErrOk:
				switch {
				case obs&zx.SignalSocketControlReadable != 0:
					continue
				case obs&LOCAL_SIGNAL_CLOSING != 0:
					return
				case obs&zx.SignalSocketPeerClosed != 0:
					return
				}
			default:
				log.Printf("loopControl wait failed: %v", err)
				return
			}
		default:
			if err == zxsocket.ErrDisconnectNoCallback {
				// We received OpClose.
				synthesizeClose = false
				return
			}
			log.Printf("loopControl failed: %v", err) // TODO: communicate this
			continue
		}
	}
}

func (s *socketServer) newIostate(netProto tcpip.NetworkProtocolNumber, transProto tcpip.TransportProtocolNumber, wq *waiter.Queue, ep tcpip.Endpoint, isAccept bool) (zx.Socket, zx.Socket, error) {
	var t uint32
	switch transProto {
	case tcp.ProtocolNumber:
		t = zx.SocketStream
	case udp.ProtocolNumber:
		t = zx.SocketDatagram
	case ping.ProtocolNumber4, ping.ProtocolNumber6:
	default:
		panic(fmt.Sprintf("unknown transport protocol number: %v", transProto))
	}
	t |= zx.SocketHasControl
	if !isAccept {
		t |= zx.SocketHasAccept
	}
	localS, peerS, err := zx.NewSocket(t)
	if err != nil {
		return zx.Socket(zx.HandleInvalid), zx.Socket(zx.HandleInvalid), err
	}
	ios := &iostate{
		netProto:      netProto,
		transProto:    transProto,
		wq:            wq,
		ep:            ep,
		refs:          1,
		dataHandle:    localS,
		loopWriteDone: make(chan struct{}),
		closing:       make(chan struct{}),
	}

	s.mu.Lock()
	newCookie := s.next
	s.next++
	s.io[newCookie] = ios
	s.mu.Unlock()

	go ios.loopControl(s, int64(newCookie))
	go func() {
		defer close(ios.loopWriteDone)

		switch transProto {
		case tcp.ProtocolNumber:
			go ios.loopStreamRead(s.stack)
			ios.loopStreamWrite(s.stack)
		case udp.ProtocolNumber, ping.ProtocolNumber4:
			go ios.loopDgramRead(s.stack)
			ios.loopDgramWrite(s.stack)
		}
	}()

	return localS, peerS, nil
}

type socketServer struct {
	stack *stack.Stack
	ns    *Netstack

	mu   sync.Mutex
	next cookie
	io   map[cookie]*iostate
}

func (s *socketServer) opSocket(netProto tcpip.NetworkProtocolNumber, transProto tcpip.TransportProtocolNumber) (zx.Socket, error) {
	wq := new(waiter.Queue)
	ep, e := s.stack.NewEndpoint(transProto, netProto, wq)
	if e != nil {
		if debug {
			log.Printf("socket: new endpoint: %v", e)
		}
		return zx.Socket(zx.HandleInvalid), mxerror.Errorf(zx.ErrInternal, "socket: new endpoint: %v", e)
	}
	if netProto == ipv6.ProtocolNumber {
		if err := ep.SetSockOpt(tcpip.V6OnlyOption(0)); err != nil {
			log.Printf("socket: setsockopt v6only option failed: %v", err)
		}
	}
	_, peerS, err := s.newIostate(netProto, transProto, wq, ep, false)
	if err != nil {
		if debug {
			log.Printf("socket: new iostate: %v", err)
		}
		return zx.Socket(zx.HandleInvalid), err
	}

	return peerS, nil
}

func errStatus(err error) zx.Status {
	if err == nil {
		return zx.ErrOk
	}
	if s, ok := err.(zx.Error); ok {
		return s.Status
	}

	log.Printf("%v", err)
	return zx.ErrInternal
}

func zxNetError(e *tcpip.Error) zx.Status {
	switch e {
	case tcpip.ErrUnknownProtocol:
		return zx.ErrProtocolNotSupported
	case tcpip.ErrDuplicateAddress,
		tcpip.ErrPortInUse:
		return zx.ErrAddressInUse
	case tcpip.ErrNoRoute,
		tcpip.ErrNetworkUnreachable,
		tcpip.ErrNoLinkAddress:
		return zx.ErrAddressUnreachable
	case tcpip.ErrAlreadyBound:
		// Note that tcpip.ErrAlreadyBound and zx.ErrAlreadyBound correspond to different
		// errors. tcpip.ErrAlreadyBound is returned when attempting to bind socket when
		// it's already bound. zx.ErrAlreadyBound is used to indicate that the local
		// address is already used by someone else.
		return zx.ErrInvalidArgs
	case tcpip.ErrInvalidEndpointState,
		tcpip.ErrAlreadyConnecting,
		tcpip.ErrAlreadyConnected:
		return zx.ErrBadState
	case tcpip.ErrNoPortAvailable,
		tcpip.ErrNoBufferSpace:
		return zx.ErrNoResources
	case tcpip.ErrUnknownProtocolOption,
		tcpip.ErrBadLocalAddress,
		tcpip.ErrDestinationRequired,
		tcpip.ErrBadAddress,
		tcpip.ErrInvalidOptionValue,
		tcpip.ErrDuplicateNICID,
		tcpip.ErrBadLinkEndpoint:
		return zx.ErrInvalidArgs
	case tcpip.ErrClosedForSend,
		tcpip.ErrClosedForReceive,
		tcpip.ErrConnectionReset:
		return zx.ErrConnectionReset
	case tcpip.ErrWouldBlock:
		return zx.ErrShouldWait
	case tcpip.ErrConnectionRefused:
		return zx.ErrConnectionRefused
	case tcpip.ErrTimeout:
		return zx.ErrTimedOut
	case tcpip.ErrConnectStarted:
		return zx.ErrShouldWait
	case tcpip.ErrNotSupported,
		tcpip.ErrQueueSizeNotSupported:
		return zx.ErrNotSupported
	case tcpip.ErrNotConnected:
		return zx.ErrNotConnected
	case tcpip.ErrConnectionAborted:
		return zx.ErrConnectionAborted

	case tcpip.ErrUnknownNICID,
		tcpip.ErrNoSuchFile:
		return zx.ErrNotFound
	case tcpip.ErrAborted:
		return zx.ErrCanceled
	case tcpip.ErrMessageTooLong:
		return zx.ErrOutOfRange
	default:
		log.Printf("Mapping unknown netstack error to zx.ErrInternal: %v", e)
		return zx.ErrInternal
	}
}

func (s *socketServer) opGetSockOpt(ios *iostate, msg *zxsocket.Msg) zx.Status {
	var val C.struct_zxrio_sockopt_req_reply
	if err := val.Unmarshal(msg.Data[:msg.Datalen]); err != nil {
		if debug {
			log.Printf("getsockopt: decode argument: %v", err)
		}
		return errStatus(err)
	}
	opt, err := val.Unpack()
	if err != nil {
		return errStatus(err)
	}
	switch o := opt.(type) {
	case tcpip.ErrorOption:
		ios.mu.Lock()
		err := ios.lastError
		ios.lastError = nil
		ios.mu.Unlock()

		if err == nil {
			err = ios.ep.GetSockOpt(o)
		}

		errno := uint32(0)
		if err != nil {
			// TODO: should this be a unix errno?
			errno = uint32(zxNetError(err))
		}
		binary.LittleEndian.PutUint32(val.opt(), errno)
		val.optlen = 4
	case tcpip.SendBufferSizeOption:
		if err := ios.ep.GetSockOpt(&o); err != nil {
			return zxNetError(err)
		}
		binary.LittleEndian.PutUint32(val.opt(), uint32(o))
		val.optlen = 4
	case tcpip.ReceiveBufferSizeOption:
		if err := ios.ep.GetSockOpt(&o); err != nil {
			return zxNetError(err)
		}
		binary.LittleEndian.PutUint32(val.opt(), uint32(o))
		val.optlen = 4
	case tcpip.ReceiveQueueSizeOption:
		if err := ios.ep.GetSockOpt(&o); err != nil {
			return zxNetError(err)
		}
		binary.LittleEndian.PutUint32(val.opt(), uint32(o))
		val.optlen = 4
	case tcpip.DelayOption:
		if err := ios.ep.GetSockOpt(&o); err != nil {
			return zxNetError(err)
		}
		// Socket option is TCP_NODELAY, so we need to invert the delay flag.
		if o != 0 {
			o = 0
		} else {
			o = 1
		}
		binary.LittleEndian.PutUint32(val.opt(), uint32(o))
		val.optlen = 4
	case tcpip.ReuseAddressOption:
		if err := ios.ep.GetSockOpt(&o); err != nil {
			return zxNetError(err)
		}
		binary.LittleEndian.PutUint32(val.opt(), uint32(o))
		val.optlen = 4
	case tcpip.ReusePortOption:
		if err := ios.ep.GetSockOpt(&o); err != nil {
			return zxNetError(err)
		}
		binary.LittleEndian.PutUint32(val.opt(), uint32(o))
		val.optlen = 4
	case tcpip.V6OnlyOption:
		if err := ios.ep.GetSockOpt(&o); err != nil {
			return zxNetError(err)
		}
		binary.LittleEndian.PutUint32(val.opt(), uint32(o))
		val.optlen = 4
	case tcpip.MulticastTTLOption:
		if err := ios.ep.GetSockOpt(&o); err != nil {
			return zxNetError(err)
		}
		binary.LittleEndian.PutUint32(val.opt(), uint32(o))
		val.optlen = 4
	case tcpip.KeepaliveEnabledOption:
		if err := ios.ep.GetSockOpt(&o); err != nil {
			return zxNetError(err)
		}
		binary.LittleEndian.PutUint32(val.opt(), uint32(o))
		val.optlen = 4
	case tcpip.KeepaliveIdleOption:
		if err := ios.ep.GetSockOpt(&o); err != nil {
			return zxNetError(err)
		}
		binary.LittleEndian.PutUint32(val.opt(), uint32(time.Duration(o).Seconds()))
		val.optlen = 4
	case tcpip.KeepaliveIntervalOption:
		if err := ios.ep.GetSockOpt(&o); err != nil {
			return zxNetError(err)
		}
		binary.LittleEndian.PutUint32(val.opt(), uint32(time.Duration(o).Seconds()))
		val.optlen = 4
	case tcpip.KeepaliveCountOption:
		if err := ios.ep.GetSockOpt(&o); err != nil {
			return zxNetError(err)
		}
		binary.LittleEndian.PutUint32(val.opt(), uint32(o))
		val.optlen = 4
	case tcpip.TCPInfoOption:
		if err := ios.ep.GetSockOpt(&o); err != nil {
			return zxNetError(err)
		}
		info := C.struct_tcp_info{
			// Microseconds.
			tcpi_rtt:    C.uint(o.RTT.Nanoseconds() / 1000),
			tcpi_rttvar: C.uint(o.RTTVar.Nanoseconds() / 1000),
		}
		n, err := info.MarshalTo(val.opt())
		if err != nil {
			return errStatus(err)
		}
		val.optlen = C.socklen_t(n)
	default:
		b := val.opt()
		for i := range b {
			b[i] = 0
		}

		if opt != nil {
			val.optlen = 4
		} else {
			val.optlen = 0
		}

		log.Printf("unsupported getsockopt(%d, %d)", val.level, val.optname)
	}
	n, err := val.MarshalTo(msg.Data[:])
	if err != nil {
		return errStatus(err)
	}
	msg.Datalen = uint32(n)
	return zx.ErrOk
}

func (s *socketServer) opSetSockOpt(ios *iostate, msg *zxsocket.Msg) zx.Status {
	var val C.struct_zxrio_sockopt_req_reply
	if err := val.Unmarshal(msg.Data[:msg.Datalen]); err != nil {
		if debug {
			log.Printf("setsockopt: decode argument: %v", err)
		}
		return errStatus(err)
	}
	opt, err := val.Unpack()
	if err != nil {
		return errStatus(err)
	}
	if opt != nil {
		if err := ios.ep.SetSockOpt(opt); err != nil {
			return zxNetError(err)
		}
	} else {
		log.Printf("unsupported setsockopt: level=%d name=%d)", val.level, val.optname)
	}
	msg.Datalen = 0
	msg.SetOff(0)
	return zx.ErrOk
}

func (s *socketServer) opBind(ios *iostate, msg *zxsocket.Msg) (status zx.Status) {
	// TODO(tamird): are we really sending raw sockaddr_storage here? why aren't we using
	// zxrio_sockaddr_reply? come to think of it, why does zxrio_sockaddr_reply exist?
	addr, err := func() (tcpip.FullAddress, error) {
		var sockaddrStorage C.struct_sockaddr_storage
		if err := sockaddrStorage.Unmarshal(msg.Data[:msg.Datalen]); err != nil {
			return tcpip.FullAddress{}, err
		}
		return sockaddrStorage.Decode()
	}()
	if err != nil {
		if debug {
			log.Printf("bind: bad input: %v", err)
		}
		return errStatus(err)
	}
	if debug {
		defer func() {
			log.Printf("bind(%+v): %v", addr, status)
		}()
	}

	if err := ios.ep.Bind(addr, nil); err != nil {
		return zxNetError(err)
	}

	if logListen {
		if ios.transProto == udp.ProtocolNumber {
			log.Printf("UDP bind (%v, %v)", addr.Addr, addr.Port)
		}
	}

	msg.Datalen = 0
	msg.SetOff(0)
	return zx.ErrOk
}

func (s *socketServer) buildIfInfos() *C.netc_get_if_info_t {
	rep := &C.netc_get_if_info_t{}

	s.ns.mu.Lock()
	defer s.ns.mu.Unlock()
	var index C.uint
	for nicid, ifs := range s.ns.ifStates {
		if ifs.nic.Addr == ipv4Loopback {
			continue
		}
		name := ifs.nic.Name
		// leave one byte for the null terminator.
		if l := len(rep.info[index].name) - 1; len(name) > l {
			name = name[:l]
		}
		// memcpy with a cast to appease the type checker.
		for i := range name {
			rep.info[index].name[i] = C.char(name[i])
		}
		rep.info[index].index = C.ushort(index + 1)
		rep.info[index].flags |= C.NETC_IFF_UP
		if _, err := rep.info[index].addr.Encode(tcpip.FullAddress{NIC: nicid, Addr: ifs.nic.Addr}); err != nil {
			log.Printf("encoding addr failed: %v", err)
		}
		if _, err := rep.info[index].netmask.Encode(tcpip.FullAddress{NIC: nicid, Addr: tcpip.Address(ifs.nic.Netmask)}); err != nil {
			log.Printf("encoding netmask failed: %v", err)
		}

		// Long-hand for: broadaddr = ifs.nic.Addr | ^ifs.nic.Netmask
		broadaddr := []byte(ifs.nic.Addr)
		for i := range broadaddr {
			broadaddr[i] |= ^ifs.nic.Netmask[i]
		}
		if _, err := rep.info[index].broadaddr.Encode(tcpip.FullAddress{NIC: nicid, Addr: tcpip.Address(broadaddr)}); err != nil {
			log.Printf("encoding broadaddr failed: %v", err)
		}
		index++
	}
	rep.n_info = index
	return rep
}

var (
	ioctlNetcGetNumIfs   = fdio.IoctlNum(fdio.IoctlKindDefault, fdio.IoctlFamilyNetconfig, 1)
	ioctlNetcGetIfInfoAt = fdio.IoctlNum(fdio.IoctlKindDefault, fdio.IoctlFamilyNetconfig, 2)
	ioctlNetcGetNodename = fdio.IoctlNum(fdio.IoctlKindDefault, fdio.IoctlFamilyNetconfig, 8)
)

// We remember the interface list from the last time ioctlNetcGetNumIfs was called. This avoids
// a race condition if the interface list changes between calls to ioctlNetcGetIfInfoAt.
var lastIfInfo *C.netc_get_if_info_t

func (s *socketServer) opIoctl(ios *iostate, msg *zxsocket.Msg) zx.Status {
	switch msg.IoctlOp() {
	// TODO(ZX-766): remove when dart/runtime/bin/socket_base_fuchsia.cc uses getifaddrs().
	case ioctlNetcGetNumIfs:
		lastIfInfo = s.buildIfInfos()
		binary.LittleEndian.PutUint32(msg.Data[:msg.Arg], uint32(lastIfInfo.n_info))
		msg.Datalen = 4
		return zx.ErrOk
	// TODO(ZX-766): remove when dart/runtime/bin/socket_base_fuchsia.cc uses getifaddrs().
	case ioctlNetcGetIfInfoAt:
		if lastIfInfo == nil {
			if debug {
				log.Printf("ioctlNetcGetIfInfoAt: called before ioctlNetcGetNumIfs")
			}
			return zx.ErrBadState
		}
		d := msg.Data[:msg.Datalen]
		if len(d) != 4 {
			if debug {
				log.Printf("ioctlNetcGetIfInfoAt: bad input length %d", len(d))
			}
			return zx.ErrInvalidArgs
		}
		requestedIndex := binary.LittleEndian.Uint32(d)
		if requestedIndex >= uint32(lastIfInfo.n_info) {
			if debug {
				log.Printf("ioctlNetcGetIfInfoAt: index out of range (%d vs %d)", requestedIndex, lastIfInfo.n_info)
			}
			return zx.ErrInvalidArgs
		}
		n, err := lastIfInfo.info[requestedIndex].MarshalTo(msg.Data[:])
		if err != nil {
			if debug {
				log.Printf("ioctlNetcGetIfInfoAt: %v", err)
			}
			return zx.ErrInternal
		}
		msg.Datalen = uint32(n)
		return zx.ErrOk
	case ioctlNetcGetNodename:
		nodename := s.ns.getNodeName()
		msg.Datalen = uint32(copy(msg.Data[:msg.Arg], nodename))
		msg.Data[msg.Datalen] = 0
		return zx.ErrOk
	}

	if debug {
		log.Printf("opIoctl op=0x%x, datalen=%d", msg.Op(), msg.Datalen)
	}

	return zx.ErrInvalidArgs
}

func fdioSockAddrReply(addr tcpip.FullAddress, msg *zxsocket.Msg) zx.Status {
	var rep C.struct_zxrio_sockaddr_reply
	{
		n, err := rep.addr.Encode(addr)
		if err != nil {
			return errStatus(err)
		}
		rep.len = C.socklen_t(n)
	}
	{
		n, err := rep.MarshalTo(msg.Data[:])
		if err != nil {
			return errStatus(err)
		}
		msg.Datalen = uint32(n)
	}
	msg.SetOff(0)
	return zx.ErrOk
}

func (s *socketServer) opGetSockName(ios *iostate, msg *zxsocket.Msg) zx.Status {
	a, err := ios.ep.GetLocalAddress()
	if err != nil {
		return zxNetError(err)
	}
	if len(a.Addr) == 0 {
		switch ios.netProto {
		case ipv4.ProtocolNumber:
			a.Addr = header.IPv4Any
		case ipv6.ProtocolNumber:
			a.Addr = header.IPv6Any
		}
	}

	if debug {
		log.Printf("getsockname(): %+v", a)
	}
	return fdioSockAddrReply(a, msg)
}

func (s *socketServer) opGetPeerName(ios *iostate, msg *zxsocket.Msg) (status zx.Status) {
	a, err := ios.ep.GetRemoteAddress()
	if err != nil {
		return zxNetError(err)
	}
	return fdioSockAddrReply(a, msg)
}

func (s *socketServer) loopListen(ios *iostate, inCh chan struct{}) {
	// When an incoming connection is available, wait for the listening socket to
	// enter a shareable state, then share it with the client.
	for {
		select {
		case <-inCh:
			// NOP
		case <-ios.closing:
			return
		}
		// We got incoming connections.
		// Note that we don't know how many connections pending (the waiter channel won't
		// queue more than one notification) so we'll need to call Accept repeatedly until
		// it returns tcpip.ErrWouldBlock.
		for {
			obs, err := zxwait.Wait(zx.Handle(ios.dataHandle),
				zx.SignalSocketShare|zx.SignalSocketPeerClosed|LOCAL_SIGNAL_CLOSING,
				zx.TimensecInfinite)
			switch mxerror.Status(err) {
			case zx.ErrOk:
				switch {
				case obs&zx.SignalSocketShare != 0:
					// NOP
				case obs&LOCAL_SIGNAL_CLOSING != 0:
					return
				case obs&zx.SignalSocketPeerClosed != 0:
					return
				}
			case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
				return
			default:
				log.Printf("listen: wait failed: %v", err)
			}

			newep, newwq, e := ios.ep.Accept()
			if e == tcpip.ErrWouldBlock {
				// No more pending connections.
				break
			}
			if e != nil {
				if debug {
					log.Printf("listen: accept failed: %v", e)
				}
				return
			}

			if logAccept {
				localAddr, err := newep.GetLocalAddress()
				remoteAddr, err2 := newep.GetRemoteAddress()
				if err == nil && err2 == nil {
					log.Printf("TCP accept: local(%v, %v), remote(%v, %v)", localAddr.Addr, localAddr.Port, remoteAddr.Addr, remoteAddr.Port)
				}
			}

			localS, peerS, err := s.newIostate(ios.netProto, ios.transProto, newwq, newep, true)
			if err != nil {
				if debug {
					log.Printf("listen: newIostate failed: %v", err)
				}
				return
			}

			if err := ios.dataHandle.Share(zx.Handle(peerS)); err != nil {
				log.Printf("listen: Share failed: %v", err)
				return
			}
			if err := signalConnectSuccess(localS, false); err != nil {
				log.Printf("listen: signalConnectSuccess failed: %v", err)
				return
			}
		}
	}
}

func (s *socketServer) opListen(ios *iostate, msg *zxsocket.Msg) (status zx.Status) {
	d := msg.Data[:msg.Datalen]
	if len(d) != 4 {
		if debug {
			log.Printf("listen: bad input length %d", len(d))
		}
		return zx.ErrInvalidArgs
	}
	backlog := binary.LittleEndian.Uint32(d)

	inEntry, inCh := waiter.NewChannelEntry(nil)
	ios.wq.EventRegister(&inEntry, waiter.EventIn)
	if err := ios.ep.Listen(int(backlog)); err != nil {
		if debug {
			log.Printf("listen: %v", err)
		}
		return zxNetError(err)
	}

	if logListen {
		addr, err := ios.ep.GetLocalAddress()
		if err == nil {
			log.Printf("TCP listen: (%v, %v)", addr.Addr, addr.Port)
		}
	}

	ios.loopListenDone = make(chan struct{})
	go func() {
		defer close(ios.loopListenDone)
		s.loopListen(ios, inCh)
		ios.wq.EventUnregister(&inEntry)
	}()

	msg.Datalen = 0
	msg.SetOff(0)
	return zx.ErrOk
}

func (s *socketServer) opConnect(ios *iostate, msg *zxsocket.Msg) (status zx.Status) {
	if msg.Datalen == 0 {
		if ios.transProto == udp.ProtocolNumber {
			// connect() can be called with no address to
			// disassociate UDP sockets.
			ios.ep.Shutdown(tcpip.ShutdownRead)
			return zx.ErrOk
		}
		if debug {
			log.Printf("connect: no input")
		}
		return zx.ErrInvalidArgs
	}
	// TODO(tamird): are we really sending raw sockaddr_storage here? why aren't we using
	// zxrio_sockaddr_reply? come to think of it, why does zxrio_sockaddr_reply exist?
	addr, err := func() (tcpip.FullAddress, error) {
		var sockaddrStorage C.struct_sockaddr_storage
		if err := sockaddrStorage.Unmarshal(msg.Data[:msg.Datalen]); err != nil {
			return tcpip.FullAddress{}, err
		}
		return sockaddrStorage.Decode()
	}()
	if err != nil {
		if debug {
			log.Printf("connect: bad input: %v", err)
		}
		return errStatus(err)
	}
	if debug {
		defer func() {
			log.Printf("connect(%+v): %v", addr, status)
		}()
	}

	if addr.Addr == "" {
		// TODO: Not ideal. We should pass an empty addr to the endpoint,
		// and netstack should find the first local interface that it can
		// connect to. Until that exists, we assume localhost.
		switch ios.netProto {
		case ipv4.ProtocolNumber:
			addr.Addr = ipv4Loopback
		case ipv6.ProtocolNumber:
			addr.Addr = ipv6Loopback
		}
	}

	waitEntry, notifyCh := waiter.NewChannelEntry(nil)
	ios.wq.EventRegister(&waitEntry, waiter.EventOut)
	e := ios.ep.Connect(addr)

	msg.SetOff(0)
	msg.Datalen = 0

	if e == tcpip.ErrConnectStarted {
		go func() {
			select {
			case <-notifyCh:
			case <-ios.closing:
				ios.wq.EventUnregister(&waitEntry)
				return
			}
			ios.wq.EventUnregister(&waitEntry)
			e = ios.ep.GetSockOpt(tcpip.ErrorOption{})
			if e != nil {
				ios.mu.Lock()
				ios.lastError = e
				ios.mu.Unlock()
				if err := signalConnectFailure(ios.dataHandle); err != nil {
					log.Printf("connect: signalConnectFailure failed: %v", err)
				}
				return
			}
			if err := signalConnectSuccess(ios.dataHandle, true); err != nil {
				log.Printf("connect: signalConnectSuccess failed: %v", err)
			}
		}()
		return zx.ErrShouldWait
	}
	ios.wq.EventUnregister(&waitEntry)
	if e != nil {
		if debug {
			log.Printf("connect: addr=%v, %v", addr, e)
		}
		return zxNetError(e)
	}
	if debug {
		log.Printf("connect: connected")
	}
	if ios.transProto == tcp.ProtocolNumber {
		if err := signalConnectSuccess(ios.dataHandle, true); err != nil {
			return errStatus(err)
		}
	}

	return zx.ErrOk
}

func (s *socketServer) opClose(ios *iostate, cookie cookie) zx.Status {
	s.mu.Lock()
	delete(s.io, cookie)
	s.mu.Unlock()

	// Signal that we're about to close. This tells the various message loops to finish
	// processing, and let us know when they're done.
	err := mxerror.Status(ios.dataHandle.Handle().Signal(0, LOCAL_SIGNAL_CLOSING))
	close(ios.closing)
	for _, c := range []<-chan struct{}{
		ios.loopWriteDone,
		ios.loopListenDone,
	} {
		if c != nil {
			<-c
		}
	}

	ios.ep.Close()

	return err
}

func (s *socketServer) zxsocketHandler(msg *zxsocket.Msg, rh zx.Socket, cookieVal int64) zx.Status {
	cookie := cookie(cookieVal)
	op := msg.Op()
	if debug {
		log.Printf("zxsocketHandler: op=%v, len=%d, arg=%v, hcount=%d", op, msg.Datalen, msg.Arg, msg.Hcount)
	}

	s.mu.Lock()
	ios := s.io[cookie]
	s.mu.Unlock()
	if ios == nil {
		if op == zxsocket.OpClose && rh == 0 {
			// The close op was synthesized by Dispatcher (because the peer channel was closed).
			return zx.ErrOk
		}
		log.Printf("zxsioHandler: request (op:%v) dropped because of the state mismatch", op)
		return zx.ErrBadState
	}

	switch op {
	case zxsocket.OpConnect:
		return s.opConnect(ios, msg)
	case zxsocket.OpClose:
		return s.opClose(ios, cookie)
	case zxsocket.OpBind:
		return s.opBind(ios, msg)
	case zxsocket.OpListen:
		return s.opListen(ios, msg)
	case zxsocket.OpIoctl:
		return s.opIoctl(ios, msg)
	case zxsocket.OpGetSockname:
		return s.opGetSockName(ios, msg)
	case zxsocket.OpGetPeerName:
		return s.opGetPeerName(ios, msg)
	case zxsocket.OpGetSockOpt:
		return s.opGetSockOpt(ios, msg)
	case zxsocket.OpSetSockOpt:
		return s.opSetSockOpt(ios, msg)
	default:
		log.Printf("zxsocketHandler: unknown socket op: %v", op)
		return zx.ErrNotSupported
	}
	// TODO do_halfclose
}
