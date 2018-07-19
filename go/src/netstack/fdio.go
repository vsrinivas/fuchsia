// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/binary"
	"fmt"
	"log"
	"strings"
	"sync"
	"syscall/zx"
	"syscall/zx/fdio"
	"syscall/zx/mxerror"
	"syscall/zx/zxsocket"
	"syscall/zx/zxwait"
	"time"

	"netstack/dns"

	"app/context"
	"fidl/fuchsia/devicesettings"
	"fidl/fuchsia/net"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/buffer"
	"github.com/google/netstack/tcpip/header"
	"github.com/google/netstack/tcpip/network/ipv4"
	"github.com/google/netstack/tcpip/network/ipv6"
	"github.com/google/netstack/tcpip/stack"
	"github.com/google/netstack/tcpip/transport/tcp"
	"github.com/google/netstack/tcpip/transport/udp"
	"github.com/google/netstack/waiter"
)

const debug = true
const debug2 = false

// TODO: Replace these with a better tracing mechanism (NET-757)
const logListen = false
const logAccept = false

const ZX_SOCKET_HALF_CLOSE = 1
const ZXSIO_SIGNAL_INCOMING = zx.SignalUser0
const ZXSIO_SIGNAL_OUTGOING = zx.SignalUser1
const ZXSIO_SIGNAL_CONNECTED = zx.SignalUser3
const LOCAL_SIGNAL_CLOSING = zx.SignalUser5

const defaultNIC = 2

var (
	ioctlNetcGetNumIfs   = fdio.IoctlNum(fdio.IoctlKindDefault, fdio.IoctlFamilyNetconfig, 1)
	ioctlNetcGetIfInfoAt = fdio.IoctlNum(fdio.IoctlKindDefault, fdio.IoctlFamilyNetconfig, 2)
	ioctlNetcGetNodename = fdio.IoctlNum(fdio.IoctlKindDefault, fdio.IoctlFamilyNetconfig, 8)
)

func socketDispatcher(stk *stack.Stack, ctx *context.Context) (*socketServer, error) {
	d, err := fdio.NewDispatcher(fdio.Handler)
	if err != nil {
		return nil, err
	}
	a := socketServer{
		dispatcher: d,
		stack:      stk,
		dnsClient:  dns.NewClient(stk, 0),
		io:         make(map[cookie]*iostate),
		next:       1,
	}
	ctx.OutgoingService.AddService("net.Netstack", func(c zx.Channel) error {
		h := zx.Handle(c)
		if err := a.dispatcher.AddHandler(h, fdio.ServerHandler(a.fdioHandler), 0); err != nil {
			h.Close()
		}
		if err := h.SignalPeer(0, zx.SignalUser0); err != nil {
			h.Close()
		}
		return nil
	})

	go d.Serve()
	return &a, nil
}

func (s *socketServer) setNetstack(ns *netstack) {
	s.ns = ns
}

type cookie int64

type iostate struct {
	wq *waiter.Queue
	ep tcpip.Endpoint

	netProto   tcpip.NetworkProtocolNumber   // IPv4 or IPv6
	transProto tcpip.TransportProtocolNumber // TCP or UDP

	openProto int // protocol for opOpen

	dataHandle zx.Handle // a zx.Socket, used to communicate with libc

	mu        sync.Mutex
	refs      int
	lastError *tcpip.Error // if not-nil, next error returned via getsockopt

	writeLoopDone     chan struct{}
	controlLoopDone   chan struct{}
	listenLoopClosing chan struct{} // tell the listen loop to close
	listenLoopDone    chan struct{} // report that the listen loops has closed
}

// loopSocketWrite connects libc write to the network stack for TCP sockets.
//
// As written, we have two netstack threads per socket.
// That's not so bad for small client work, but even a client OS is
// eventually going to feel the overhead of this.
func (ios *iostate) loopSocketWrite(stk *stack.Stack) {
	defer func() { ios.writeLoopDone <- struct{}{} }()

	dataHandle := zx.Socket(ios.dataHandle)

	// Warm up.
	_, err := zxwait.Wait(ios.dataHandle,
		zx.SignalSocketReadable|zx.SignalSocketReadDisabled|
			zx.SignalSocketPeerClosed|LOCAL_SIGNAL_CLOSING,
		zx.TimensecInfinite)
	switch mxerror.Status(err) {
	case zx.ErrOk:
		// NOP
	case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
		return
	default:
		log.Printf("loopSocketWrite: warmup failed: %v", err)
	}

	waitEntry, notifyCh := waiter.NewChannelEntry(nil)
	for {
		// TODO: obviously allocating for each read is silly.
		// A quick hack we can do is store these in a ring buffer,
		// as the lifecycle of this buffer.View starts here, and
		// ends in nearby code we control in link.go.
		v := buffer.NewView(2048)
		n, err := dataHandle.Read([]byte(v), 0)
		switch mxerror.Status(err) {
		case zx.ErrOk:
			// Success. Pass the data to the endpoint and loop.
		case zx.ErrBadState:
			// This side of the socket is closed.
			err := ios.ep.Shutdown(tcpip.ShutdownWrite)
			if err != nil {
				log.Printf("loopSocketWrite: ShutdownWrite failed: %v", err)
			}
			return
		case zx.ErrShouldWait:
			obs, err := zxwait.Wait(ios.dataHandle,
				zx.SignalSocketReadable|zx.SignalSocketReadDisabled|
					zx.SignalSocketPeerClosed|LOCAL_SIGNAL_CLOSING,
				zx.TimensecInfinite)
			switch mxerror.Status(err) {
			case zx.ErrOk:
				// Handle signal below.
			case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
				return
			default:
				log.Printf("loopSocketWrite: wait failed: %v", err)
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

		if debug2 {
			log.Printf("loopSocketWrite: sending packet n=%d, v=%q", n, v[:n])
		}
		ios.wq.EventRegister(&waitEntry, waiter.EventOut)
		for {
			_, err := ios.ep.Write(v[:n], nil)
			if err == tcpip.ErrWouldBlock {
				<-notifyCh
				continue
			}
			break
		}
		ios.wq.EventUnregister(&waitEntry)
		if err != nil {
			log.Printf("loopSocketWrite: got endpoint error: %v (TODO)", err)
			return
		}
	}
}

// loopSocketRead connects libc read to the network stack for TCP sockets.
func (ios *iostate) loopSocketRead(stk *stack.Stack) {
	dataHandle := zx.Socket(ios.dataHandle)

	// Warm up.
	writable := false
	connected := false
	for !(writable && connected) {
		sigs := zx.Signals(zx.SignalSocketWriteDisabled | zx.SignalSocketPeerClosed)
		if !writable {
			sigs |= zx.SignalSocketWritable
		}
		if !connected {
			sigs |= ZXSIO_SIGNAL_CONNECTED
		}
		obs, err := zxwait.Wait(ios.dataHandle, sigs, zx.TimensecInfinite)
		switch mxerror.Status(err) {
		case zx.ErrOk:
			// NOP
		case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
			return
		default:
			log.Printf("loopSocketRead: warmup failed: %v", err)
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
		if obs&zx.SignalSocketWriteDisabled != 0 {
			err := ios.ep.Shutdown(tcpip.ShutdownRead)
			if err != nil {
				log.Printf("loopSocketRead: ShutdownRead failed: %v", err)
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
			v, err = ios.ep.Read(nil)
			if err == nil {
				break
			} else if err == tcpip.ErrWouldBlock || err == tcpip.ErrInvalidEndpointState || err == tcpip.ErrNotConnected {
				if debug2 {
					log.Printf("loopSocketRead read err=%v", err)
				}
				<-notifyCh
				// TODO: get socket closed message from loopSocketWrite
				continue
			} else if err == tcpip.ErrClosedForReceive || err == tcpip.ErrConnectionRefused {
				if err == tcpip.ErrConnectionRefused {
					ios.lastError = err
				}
				_, err := dataHandle.Write(nil, ZX_SOCKET_HALF_CLOSE)
				switch mxerror.Status(err) {
				case zx.ErrOk:
				case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
				default:
					log.Printf("socket read: send ZX_SOCKET_HALF_CLOSE failed: %v", err)
				}
				return
			}
			log.Printf("loopSocketRead got endpoint error: %v (TODO)", err)
			return
		}
		ios.wq.EventUnregister(&waitEntry)
		if debug2 {
			log.Printf("loopSocketRead: got a buffer, len(v)=%d", len(v))
		}

	writeLoop:
		for len(v) > 0 {
			n, err := dataHandle.Write([]byte(v), 0)
			v = v[n:]
			switch mxerror.Status(err) {
			case zx.ErrOk:
				// Success. Loop and keep writing.
			case zx.ErrBadState:
				// This side of the socket is closed.
				err := ios.ep.Shutdown(tcpip.ShutdownRead)
				if err != nil {
					log.Printf("loopSocketRead: ShutdownRead failed: %v", err)
				}
				return
			case zx.ErrShouldWait:
				if debug2 {
					log.Printf("loopSocketRead: got zx.ErrShouldWait")
				}
				obs, err := zxwait.Wait(ios.dataHandle,
					zx.SignalSocketWritable|zx.SignalSocketWriteDisabled|
						zx.SignalSocketPeerClosed,
					zx.TimensecInfinite)
				switch mxerror.Status(err) {
				case zx.ErrOk:
					// Handle signal below.
				case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
					return
				default:
					log.Printf("loopSocketRead: wait failed: %v", err)
					return
				}
				switch {
				case obs&zx.SignalSocketPeerClosed != 0:
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
	dataHandle := zx.Socket(ios.dataHandle)

	waitEntry, notifyCh := waiter.NewChannelEntry(nil)
	for {
		ios.wq.EventRegister(&waitEntry, waiter.EventIn)
		var sender tcpip.FullAddress
		var v buffer.View
		var err *tcpip.Error
		for {
			v, err = ios.ep.Read(&sender)
			if err == nil {
				break
			} else if err == tcpip.ErrWouldBlock {
				<-notifyCh
				continue
			} else if err == tcpip.ErrClosedForReceive {
				if debug2 {
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

		out := make([]byte, c_fdio_socket_msg_hdr_len+len(v))
		writeSocketMsgHdr(out, sender)
		copy(out[c_fdio_socket_msg_hdr_len:], v)

	writeLoop:
		for {
			_, err := dataHandle.Write(out, 0)
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
	defer func() { ios.writeLoopDone <- struct{}{} }()

	dataHandle := zx.Socket(ios.dataHandle)

	waitEntry, notifyCh := waiter.NewChannelEntry(nil)
	for {
		v := buffer.NewView(2048)
		n, err := dataHandle.Read([]byte(v), 0)
		switch mxerror.Status(err) {
		case zx.ErrOk:
			// Success. Pass the data to the endpoint and loop.
		case zx.ErrBadState:
			return // This side of the socket is closed.
		case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
			return
		case zx.ErrShouldWait:
			obs, err := zxwait.Wait(ios.dataHandle, zx.SignalSocketReadable|zx.SignalSocketPeerClosed|LOCAL_SIGNAL_CLOSING, zx.TimensecInfinite)
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
		v = v[:n:n]

		receiver, err := readSocketMsgHdr(v)
		if err != nil {
			// TODO communicate
			log.Printf("loopDgramWrite: bad socket msg header: %v", err)
			continue
		}

		ios.wq.EventRegister(&waitEntry, waiter.EventOut)
		for {
			_, err := ios.ep.Write(v[c_fdio_socket_msg_hdr_len:], receiver)
			if err == tcpip.ErrWouldBlock {
				<-notifyCh
				continue
			}
			break
		}
		ios.wq.EventUnregister(&waitEntry)
		if err != nil {
			log.Printf("loopDgramWrite: got endpoint error: %v (TODO)", err)
			return
		}
	}
}

func (ios *iostate) loopControl(s *socketServer, cookie int64) {
	synthesizeClose := true
	defer func() {
		if synthesizeClose {
			zxsocket.Handler(0, zxsocket.ServerHandler(s.zxsocketHandler), cookie)
		}
		ios.controlLoopDone <- struct{}{}
	}()

	dataHandle := zx.Socket(ios.dataHandle)

	for {
		err := zxsocket.Handler(dataHandle, zxsocket.ServerHandler(s.zxsocketHandler), cookie)
		switch mxerror.Status(err) {
		case zx.ErrOk:
			// Success. Pass the data to the endpoint and loop.
		case zx.ErrBadState:
			return // This side of the socket is closed.
		case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
			return
		case zx.ErrShouldWait:
			obs, err := zxwait.Wait(ios.dataHandle, zx.SignalSocketControlReadable|zx.SignalSocketPeerClosed|LOCAL_SIGNAL_CLOSING, zx.TimensecInfinite)
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
			if err == fdio.ErrDisconnectNoCallback {
				// We received OpClose.
				synthesizeClose = false
				return
			}
			log.Printf("loopControl failed: %v", err) // TODO: communicate this
			continue
		}
	}
}

type iostateResponse func(zx.Handle)

func fdioIostateResponse(h zx.Handle, openProto int) iostateResponse {
	return func(peerS zx.Handle) {
		switch openProto {
		case 2:
			ro := fdio.RioDescription{
				Status: errStatus(nil),
			}
			ro.Info.Tag = fdio.ProtocolSocket
			ro.SetOp(fdio.OpOnOpen)
			ro.Info.Socket().Handle = peerS
			ro.Write(h, 0)
		case 3:
			r := openReply{
				Status: errStatus(nil),
				Handle: peerS,
			}
			r.Write(h)
		}
		h.Close()
	}
}

func (s *socketServer) newIostate(h zx.Handle, netProto tcpip.NetworkProtocolNumber, transProto tcpip.TransportProtocolNumber, wq *waiter.Queue, ep tcpip.Endpoint, isAccept bool, openProto int, responder iostateResponse) (reterr error) {
	var peerS zx.Handle
	ios := &iostate{
		netProto:   netProto,
		transProto: transProto,
		wq:         wq,
		ep:         ep,
		openProto:  openProto,
		refs:       1,
	}
	switch transProto {
	case tcp.ProtocolNumber, udp.ProtocolNumber, ipv4.PingProtocolNumber:
		var t uint32
		if transProto == tcp.ProtocolNumber {
			t = zx.SocketStream
		} else {
			t = zx.SocketDatagram
		}
		t |= zx.SocketHasControl
		if !isAccept {
			t |= zx.SocketHasAccept
		}
		s0, s1, err := zx.NewSocket(t)
		if err != nil {
			return err
		}
		// TODO: Why cast these to Handle? Why not store as Socket?
		ios.dataHandle = zx.Handle(s0)
		peerS = zx.Handle(s1)
		if err != nil {
			ios.dataHandle.Close()
			return err
		}
	default:
		panic(fmt.Sprintf("unknown transport protocol number: %v", transProto))
	}

	s.mu.Lock()
	newCookie := s.next
	s.next++
	s.io[newCookie] = ios
	s.mu.Unlock()

	defer func() {
		if reterr != nil {
			ios.dataHandle.Close()
			peerS.Close()

			s.mu.Lock()
			delete(s.io, newCookie)
			s.mu.Unlock()
		}
	}()

	if isAccept {
		s := zx.Socket(h)
		if err := s.Share(peerS); err != nil {
			return err
		}
		// SignalPeer CONNECTED first, then locally Signal CONNECTED.
		err := ios.dataHandle.SignalPeer(0, ZXSIO_SIGNAL_CONNECTED)
		switch status := mxerror.Status(err); status {
		case zx.ErrOk:
		case zx.ErrPeerClosed:
			// The peer might have closed the handle.
		default:
			log.Printf("signal-peer failed: %v", err)
			return err
		}
		err = ios.dataHandle.Signal(0, ZXSIO_SIGNAL_CONNECTED)
		if err != nil {
			log.Printf("signal failed: %v", err)
			return err
		}
	} else {
		responder(peerS)
	}

	if ep != nil {
		// This must be initialized before starting the control loop below, or it will race with opClose.
		ios.writeLoopDone = make(chan struct{})
	}

	ios.controlLoopDone = make(chan struct{})
	go ios.loopControl(s, int64(newCookie))

	switch transProto {
	case tcp.ProtocolNumber:
		if ep != nil {
			go ios.loopSocketRead(s.stack)
			go ios.loopSocketWrite(s.stack)
		}
	case udp.ProtocolNumber, ipv4.PingProtocolNumber:
		go ios.loopDgramRead(s.stack)
		go ios.loopDgramWrite(s.stack)
	}

	return nil
}

type socketServer struct {
	dispatcher *fdio.Dispatcher
	stack      *stack.Stack
	dnsClient  *dns.Client
	ns         *netstack

	mu   sync.Mutex
	next cookie
	io   map[cookie]*iostate
}

type socketPath struct {
	version  int
	domain   int
	typ      int
	protocol int
}

func parseSocketPath(path string) (socketPath, error) {
	var version, domain, typ, protocol int
	if n, _ := fmt.Sscanf(path, "socket-v%d/%d/%d/%d\x00", &version, &domain, &typ, &protocol); n != 4 {
		return socketPath{}, mxerror.Errorf(zx.ErrInvalidArgs, "socket: bad path %q (n=%d)", path, n)
	}
	return socketPath{version: version, domain: domain, typ: typ, protocol: protocol}, nil
}

func (s *socketServer) opSocket(h zx.Handle, path socketPath, iostateResponder iostateResponse) (err error) {
	var n tcpip.NetworkProtocolNumber
	switch path.domain {
	case AF_INET:
		n = ipv4.ProtocolNumber
	case AF_INET6:
		n = ipv6.ProtocolNumber
	default:
		return mxerror.Errorf(zx.ErrNotSupported, "socket: unknown network protocol: %d", path.domain)
	}

	transProto, err := sockProto(path.typ, path.protocol)
	if err != nil {
		return err
	}

	wq := new(waiter.Queue)
	ep, e := s.stack.NewEndpoint(transProto, n, wq)
	if e != nil {
		if debug {
			log.Printf("socket: new endpoint: %v", e)
		}
		return mxerror.Errorf(zx.ErrInternal, "socket: new endpoint: %v", err)
	}
	if n == ipv6.ProtocolNumber {
		if err := ep.SetSockOpt(tcpip.V6OnlyOption(0)); err != nil {
			log.Printf("socket: setsockopt v6only option failed: %v", err)
		}
	}
	err = s.newIostate(h, n, transProto, wq, ep, false, path.version, iostateResponder)
	if err != nil {
		if debug {
			log.Printf("socket: new iostate: %v", err)
		}
		return err
	}

	return nil
}

func sockProto(typ, protocol int) (t tcpip.TransportProtocolNumber, err error) {
	switch typ {
	case SOCK_STREAM:
		switch protocol {
		case IPPROTO_IP, IPPROTO_TCP:
			return tcp.ProtocolNumber, nil
		default:
			return 0, mxerror.Errorf(zx.ErrNotSupported, "unsupported SOCK_STREAM protocol: %d", protocol)
		}
	case SOCK_DGRAM:
		switch protocol {
		case IPPROTO_IP, IPPROTO_UDP:
			return udp.ProtocolNumber, nil
		case IPPROTO_ICMP:
			return ipv4.PingProtocolNumber, nil
		default:
			return 0, mxerror.Errorf(zx.ErrNotSupported, "unsupported SOCK_DGRAM protocol: %d", protocol)
		}
	}
	return 0, mxerror.Errorf(zx.ErrNotSupported, "unsupported protocol: %d/%d", typ, protocol)
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

func mxNetError(e *tcpip.Error) zx.Status {
	switch e {
	case tcpip.ErrUnknownProtocol:
		return zx.ErrProtocolNotSupported
	case tcpip.ErrDuplicateAddress, tcpip.ErrPortInUse:
		return zx.ErrAddressInUse
	case tcpip.ErrNoRoute:
		return zx.ErrAddressUnreachable
	case tcpip.ErrAlreadyBound:
		// Note that tcpip.ErrAlreadyBound and zx.ErrAlreadyBound correspond to different
		// errors. tcpip.ErrAlreadyBound is returned when attempting to bind socket when
		// it's already bound. zx.ErrAlreadyBound is used to indicate that the local
		// address is already used by someone else.
		return zx.ErrInvalidArgs
	case tcpip.ErrInvalidEndpointState, tcpip.ErrAlreadyConnecting, tcpip.ErrAlreadyConnected:
		return zx.ErrBadState
	case tcpip.ErrNoPortAvailable:
		return zx.ErrNoResources
	case tcpip.ErrUnknownProtocolOption, tcpip.ErrBadLocalAddress, tcpip.ErrDestinationRequired:
		return zx.ErrInvalidArgs
	case tcpip.ErrClosedForSend, tcpip.ErrClosedForReceive, tcpip.ErrConnectionReset:
		return zx.ErrConnectionReset
	case tcpip.ErrWouldBlock:
		return zx.ErrShouldWait
	case tcpip.ErrConnectionRefused:
		return zx.ErrConnectionRefused
	case tcpip.ErrTimeout:
		return zx.ErrTimedOut
	case tcpip.ErrConnectStarted:
		return zx.ErrShouldWait
	case tcpip.ErrNotSupported, tcpip.ErrQueueSizeNotSupported:
		return zx.ErrNotSupported
	case tcpip.ErrNotConnected:
		return zx.ErrNotConnected
	case tcpip.ErrConnectionAborted:
		return zx.ErrConnectionAborted
	}

	log.Printf("%v", e)
	return zx.ErrInternal
}

func (s *socketServer) opGetSockOpt(ios *iostate, msg *zxsocket.Msg) zx.Status {
	var val c_mxrio_sockopt_req_reply
	if err := val.Decode(msg.Data[:msg.Datalen]); err != nil {
		if debug {
			log.Printf("getsockopt: decode argument: %v", err)
		}
		return errStatus(err)
	}
	if ios.ep == nil {
		if debug {
			log.Printf("getsockopt: no socket")
		}
		return zx.ErrBadState
	}
	if opt := val.Unpack(); opt != nil {
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
				errno = uint32(mxNetError(err))
			}
			binary.LittleEndian.PutUint32(val.optval[:], errno)
			val.optlen = c_socklen(4)
		case tcpip.SendBufferSizeOption:
			ios.ep.GetSockOpt(&o)
			binary.LittleEndian.PutUint32(val.optval[:], uint32(o))
			val.optlen = c_socklen(4)
		case tcpip.ReceiveBufferSizeOption:
			ios.ep.GetSockOpt(&o)
			binary.LittleEndian.PutUint32(val.optval[:], uint32(o))
			val.optlen = c_socklen(4)
		case tcpip.ReceiveQueueSizeOption:
			ios.ep.GetSockOpt(&o)
			binary.LittleEndian.PutUint32(val.optval[:], uint32(o))
			val.optlen = c_socklen(4)
		case tcpip.NoDelayOption:
			ios.ep.GetSockOpt(&o)
			binary.LittleEndian.PutUint32(val.optval[:], uint32(o))
			val.optlen = c_socklen(4)
		case tcpip.ReuseAddressOption:
			ios.ep.GetSockOpt(&o)
			binary.LittleEndian.PutUint32(val.optval[:], uint32(o))
			val.optlen = c_socklen(4)
		case tcpip.V6OnlyOption:
			ios.ep.GetSockOpt(&o)
			binary.LittleEndian.PutUint32(val.optval[:], uint32(o))
			val.optlen = c_socklen(4)
		case tcpip.MulticastTTLOption:
			ios.ep.GetSockOpt(&o)
			binary.LittleEndian.PutUint32(val.optval[:], uint32(o))
			val.optlen = c_socklen(4)
		case tcpip.KeepaliveEnabledOption:
			ios.ep.GetSockOpt(&o)
			binary.LittleEndian.PutUint32(val.optval[:], uint32(o))
			val.optlen = c_socklen(4)
		case tcpip.KeepaliveIdleOption:
			ios.ep.GetSockOpt(&o)
			binary.LittleEndian.PutUint32(val.optval[:], uint32(time.Duration(o).Seconds()))
			val.optlen = c_socklen(4)
		case tcpip.KeepaliveIntervalOption:
			ios.ep.GetSockOpt(&o)
			binary.LittleEndian.PutUint32(val.optval[:], uint32(time.Duration(o).Seconds()))
			val.optlen = c_socklen(4)
		case tcpip.KeepaliveCountOption:
			ios.ep.GetSockOpt(&o)
			binary.LittleEndian.PutUint32(val.optval[:], uint32(o))
			val.optlen = c_socklen(4)
		case tcpip.InfoOption:
			ios.ep.GetSockOpt(&o)
			info := c_mxrio_sockopt_tcp_info{
				// Microseconds.
				rtt:    uint32(o.Rtt.Nanoseconds() / 1000),
				rttvar: uint32(o.Rttvar.Nanoseconds() / 1000),
			}
			info.Encode(&val)
		default:
			binary.LittleEndian.PutUint32(val.optval[:], 0)
			val.optlen = c_socklen(4)
		}
	} else {
		val.optlen = 0
	}
	val.Encode(msg)
	return zx.ErrOk
}

func (s *socketServer) opSetSockOpt(ios *iostate, msg *zxsocket.Msg) zx.Status {
	var val c_mxrio_sockopt_req_reply
	if err := val.Decode(msg.Data[:msg.Datalen]); err != nil {
		if debug {
			log.Printf("setsockopt: decode argument: %v", err)
		}
		return errStatus(err)
	}
	if ios.ep == nil {
		if debug {
			log.Printf("setsockopt: no socket")
		}
		return zx.ErrBadState
	}
	if opt := val.Unpack(); opt != nil {
		if err := ios.ep.SetSockOpt(opt); err != nil {
			return mxNetError(err)
		}
	}
	msg.Datalen = 0
	msg.SetOff(0)
	return zx.ErrOk
}

func (s *socketServer) opBind(ios *iostate, msg *zxsocket.Msg) (status zx.Status) {
	addr, err := readSockaddrIn(msg.Data[:msg.Datalen])
	if err != nil {
		if debug {
			log.Printf("bind: bad input: %v", err)
		}
		return errStatus(err)
	}
	if debug2 {
		defer func() {
			log.Printf("bind(%s): %v", *addr, status)
		}()
	}

	if ios.ep == nil {
		if debug {
			log.Printf("bind: no socket")
		}
		return zx.ErrBadState
	}
	if err := ios.ep.Bind(*addr, nil); err != nil {
		return mxNetError(err)
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

func (s *socketServer) buildIfInfos() *c_netc_get_if_info {
	rep := &c_netc_get_if_info{}

	s.ns.mu.Lock()
	defer s.ns.mu.Unlock()
	index := uint32(0)
	for nicid, ifs := range s.ns.ifStates {
		if ifs.nic.Addr == header.IPv4Loopback {
			continue
		}
		rep.info[index].index = uint16(index + 1)
		rep.info[index].flags |= NETC_IFF_UP
		copy(rep.info[index].name[:], ifs.nic.Name)
		writeSockaddrStorage(&rep.info[index].addr, tcpip.FullAddress{NIC: nicid, Addr: ifs.nic.Addr})
		writeSockaddrStorage(&rep.info[index].netmask, tcpip.FullAddress{NIC: nicid, Addr: tcpip.Address(ifs.nic.Netmask)})

		// Long-hand for: broadaddr = ifs.nic.Addr | ^ifs.nic.Netmask
		broadaddr := []byte(ifs.nic.Addr)
		for i := range broadaddr {
			broadaddr[i] |= ^ifs.nic.Netmask[i]
		}
		writeSockaddrStorage(&rep.info[index].broadaddr, tcpip.FullAddress{NIC: nicid, Addr: tcpip.Address(broadaddr)})
		index++
	}
	rep.n_info = index
	return rep
}

// We remember the interface list from the last time ioctlNetcGetNumIfs was called. This avoids
// a race condition if the interface list changes between calls to ioctlNetcGetIfInfoAt.
var lastIfInfo *c_netc_get_if_info

func (s *socketServer) opIoctl(ios *iostate, msg *zxsocket.Msg) zx.Status {
	// TODO: deprecated in favor of FIDL service. Remove.
	switch msg.IoctlOp() {
	case ioctlNetcGetNumIfs:
		lastIfInfo = s.buildIfInfos()
		binary.LittleEndian.PutUint32(msg.Data[:msg.Arg], lastIfInfo.n_info)
		msg.Datalen = 4
		return zx.ErrOk
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
		if requestedIndex >= lastIfInfo.n_info {
			if debug {
				log.Printf("ioctlNetcGetIfInfoAt: index out of range (%d vs %d)", requestedIndex, lastIfInfo.n_info)
			}
			return zx.ErrInvalidArgs
		}
		lastIfInfo.info[requestedIndex].Encode(msg)
		return zx.ErrOk
	case ioctlNetcGetNodename:
		nodename, status, err := ns.deviceSettings.GetString(deviceSettingsManagerNodenameKey)
		if err != nil {
			log.Printf("ioctlNetcGetNodename: error accessing device settings: %s\n", err)
			nodename = defaultNodename // defined in netstack.go
		}
		if status != devicesettings.StatusOk {
			var reportStatus string
			switch status {
			case devicesettings.StatusErrNotSet:
				reportStatus = "key not set"
			case devicesettings.StatusErrInvalidSetting:
				reportStatus = "invalid setting"
			case devicesettings.StatusErrRead:
				reportStatus = "error reading key"
			case devicesettings.StatusErrIncorrectType:
				reportStatus = "value type was incorrect"
			case devicesettings.StatusErrUnknown:
				reportStatus = "unknown"
			}
			log.Println("ioctlNetcGetNodename: falling back to default nodename.")
			log.Printf("ioctlNetcGetNodename: device settings error: %s\n", reportStatus)
			nodename = defaultNodename // defined in netstack.go
		}
		msg.Datalen = uint32(copy(msg.Data[:msg.Arg], nodename))
		msg.Data[msg.Datalen] = 0
		return zx.ErrOk
	}

	if debug {
		log.Printf("opIoctl op=0x%x, datalen=%d", msg.Op(), msg.Datalen)
	}

	return zx.ErrInvalidArgs
}

func fdioSockAddrReply(a tcpip.FullAddress, msg *zxsocket.Msg) zx.Status {
	var err error
	rep := c_mxrio_sockaddr_reply{}
	rep.len, err = writeSockaddrStorage(&rep.addr, a)
	if err != nil {
		return errStatus(err)
	}
	rep.Encode(msg)
	msg.SetOff(0)
	return zx.ErrOk
}

func (s *socketServer) opGetSockName(ios *iostate, msg *zxsocket.Msg) zx.Status {
	a, err := ios.ep.GetLocalAddress()
	if err != nil {
		return mxNetError(err)
	}
	if debug2 {
		log.Printf("getsockname(): %v", a)
	}
	return fdioSockAddrReply(a, msg)
}

func (s *socketServer) opGetPeerName(ios *iostate, msg *zxsocket.Msg) (status zx.Status) {
	if ios.ep == nil {
		return zx.ErrBadState
	}
	a, err := ios.ep.GetRemoteAddress()
	if err != nil {
		return mxNetError(err)
	}
	return fdioSockAddrReply(a, msg)
}

func (s *socketServer) loopListen(ios *iostate, inCh chan struct{}) {
	defer func() { ios.listenLoopDone <- struct{}{} }()

	// When an incoming connection is available, wait for the listening socket to
	// enter a shareable state, then share it with zircon.
	for {
		select {
		case <-inCh:
			// NOP
		case <-ios.listenLoopClosing:
			return
		}
		obs, err := zxwait.Wait(ios.dataHandle,
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
			log.Printf("listen: internal error. Accept returned ErrWouldBlock")
			continue
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

		err = s.newIostate(ios.dataHandle, ios.netProto, ios.transProto, newwq, newep, true, ios.openProto, fdioIostateResponse(ios.dataHandle, ios.openProto))
		if err != nil {
			if debug {
				log.Printf("listen: newIostate failed: %v", err)
			}
			return
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
	if ios.ep == nil {
		if debug {
			log.Printf("listen: no socket")
		}
		return zx.ErrBadState
	}

	inEntry, inCh := waiter.NewChannelEntry(nil)
	ios.wq.EventRegister(&inEntry, waiter.EventIn)
	if err := ios.ep.Listen(int(backlog)); err != nil {
		if debug {
			log.Printf("listen: %v", err)
		}
		return mxNetError(err)
	}

	if logListen {
		addr, err := ios.ep.GetLocalAddress()
		if err == nil {
			log.Printf("TCP listen: (%v, %v)", addr.Addr, addr.Port)
		}
	}

	ios.listenLoopClosing = make(chan struct{})
	ios.listenLoopDone = make(chan struct{})
	go func() {
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
	addr, err := readSockaddrIn(msg.Data[:msg.Datalen])
	if err != nil {
		if debug {
			log.Printf("connect: bad input: %v", err)
		}
		return errStatus(err)
	}
	if debug2 {
		defer func() {
			log.Printf("connect(%s): %v", *addr, status)
		}()
	}

	if addr.Addr == "" {
		// TODO: Not ideal. We should pass an empty addr to the endpoint,
		// and netstack should find the first local interface that it can
		// connect to. Until that exists, we assume localhost.
		switch ios.netProto {
		case ipv4.ProtocolNumber:
			addr.Addr = header.IPv4Loopback
		case ipv6.ProtocolNumber:
			addr.Addr = header.IPv6Loopback
		}
	}

	waitEntry, notifyCh := waiter.NewChannelEntry(nil)
	ios.wq.EventRegister(&waitEntry, waiter.EventOut)
	e := ios.ep.Connect(*addr)

	msg.SetOff(0)
	msg.Datalen = 0

	if e == tcpip.ErrConnectStarted {
		go func() {
			<-notifyCh
			ios.wq.EventUnregister(&waitEntry)
			e = ios.ep.GetSockOpt(tcpip.ErrorOption{})
			if e != nil {
				ios.mu.Lock()
				ios.lastError = e
				ios.mu.Unlock()
				err = ios.dataHandle.SignalPeer(0, ZXSIO_SIGNAL_OUTGOING)
				switch status := mxerror.Status(err); status {
				case zx.ErrOk:
				case zx.ErrPeerClosed:
					// The peer might have closed the handle.
				default:
					log.Printf("connect: signal-peer failed: %v", err)
					// TODO: communicate this to the client
				}
				return
			}
			// SignalPeer CONNECTED first, then locally Signal CONNECTED.
			// This way the peer always detects CONNECTED signal before any data is written by
			// loopSocketRead.
			err := ios.dataHandle.SignalPeer(0, ZXSIO_SIGNAL_OUTGOING|ZXSIO_SIGNAL_CONNECTED)
			switch status := mxerror.Status(err); status {
			case zx.ErrOk:
			case zx.ErrBadHandle, zx.ErrPeerClosed:
				// The socket might have been closed.
				// TODO: consider synchronizing with opClose.
			default:
				log.Printf("connect: signal-peer failed: %v", err)
				// TODO: communicate this to the client
			}
			err = ios.dataHandle.Signal(0, ZXSIO_SIGNAL_CONNECTED)
			switch status := mxerror.Status(err); status {
			case zx.ErrOk:
			case zx.ErrBadHandle, zx.ErrPeerClosed:
				// The socket might have been closed.
				// TODO: consider synchronizing with opClose.
			default:
				log.Printf("connect: signal failed: %v", err)
				// TODO: communicate this to the client
			}
		}()
		return zx.ErrShouldWait
	}
	ios.wq.EventUnregister(&waitEntry)
	if e != nil {
		log.Printf("connect: addr=%v, %v", *addr, err)
		return mxNetError(e)
	}
	if debug2 {
		log.Printf("connect: connected")
	}
	if ios.transProto == tcp.ProtocolNumber {
		// SignalPeer CONNECTED first, then locally Signal CONNECTED.
		err := ios.dataHandle.SignalPeer(0, ZXSIO_SIGNAL_CONNECTED)
		switch status := mxerror.Status(err); status {
		case zx.ErrOk:
		case zx.ErrPeerClosed:
			// The peer might have closed the handle.
		default:
			log.Printf("connect: signal-peer failed: %v", err)
			return status
		}
		err = ios.dataHandle.Signal(0, ZXSIO_SIGNAL_CONNECTED)
		if err != nil {
			log.Printf("connect: signal failed: %v", err)
			return mxerror.Status(err)
		}
	}

	return zx.ErrOk
}

func addrInfoStatusToEAI(status net.AddrInfoStatus) int32 {
	switch status {
	case net.AddrInfoStatusBadFlags:
		return EAI_BADFLAGS;
	case net.AddrInfoStatusNoName:
		return EAI_NONAME;
	case net.AddrInfoStatusAgain:
		return EAI_AGAIN;
	case net.AddrInfoStatusFail:
		return EAI_FAIL;
	case net.AddrInfoStatusNoData:
		return EAI_NONAME;
	case net.AddrInfoStatusBufferOverflow:
		return EAI_OVERFLOW;
	case net.AddrInfoStatusSystemError:
		return EAI_SYSTEM;
	}
	return 0
}

func (s *socketServer) opGetAddrInfo(ios *iostate, msg *zxsocket.Msg) zx.Status {
	var val c_mxrio_gai_req
	if err := val.Decode(msg); err != nil {
		return errStatus(err)
	}
	node, service, hints := val.Unpack()

	nodeP := &node
	if val.node_is_null == 1 {
		nodeP = nil
	}

	serviceP := &service
	if val.service_is_null == 1 {
		serviceP = nil
	}

	h := &net.AddrInfoHints{
		Flags:    hints.ai_flags,
		Family:   hints.ai_family,
		SockType: hints.ai_socktype,
		Protocol: hints.ai_protocol,
	}
	if val.hints_is_null == 1 {
		h = nil
	}

	status, addrInfo := s.GetAddrInfo(nodeP, serviceP, h)
	retval := addrInfoStatusToEAI(status)

	rep := c_mxrio_gai_reply{retval: retval}
	if retval != 0 {
		rep.Encode(msg)
		return zx.ErrOk
	}

	for i := 0; i < len(addrInfo); i++ {
		res := &rep.res[rep.nres]
		res.ai.ai_family = addrInfo[i].Family
		res.ai.ai_socktype = addrInfo[i].SockType
		res.ai.ai_protocol = addrInfo[i].Protocol
		// The 0xdeadbeef constant indicates the other side needs to
		// adjust ai_addr with the value passed below.
		res.ai.ai_addr = 0xdeadbeef
		switch addrInfo[i].Family {
		case AF_INET:
			sockaddr := c_sockaddr_in{sin_family: AF_INET}
			sockaddr.sin_port.setPort(addrInfo[i].Port)
			copy(sockaddr.sin_addr[:], addrInfo[i].Addr.Val[:4])
			writeSockaddrStorage4(&res.addr, &sockaddr)
			res.ai.ai_addrlen = c_socklen(c_sockaddr_in_len)
			rep.nres++
		case AF_INET6:
			sockaddr := c_sockaddr_in6{sin6_family: AF_INET6}
			sockaddr.sin6_port.setPort(addrInfo[i].Port)
			copy(sockaddr.sin6_addr[:], addrInfo[i].Addr.Val[:16])
			writeSockaddrStorage6(&res.addr, &sockaddr)
			res.ai.ai_addrlen = c_socklen(c_sockaddr_in6_len)
			rep.nres++
		}
	}
	rep.Encode(msg)
	return zx.ErrOk
}

func (s *socketServer) GetAddrInfo(node *string, service *string, hints *net.AddrInfoHints) (net.AddrInfoStatus, []*net.AddrInfo) {
	s.mu.Lock()
	dnsClient := s.dnsClient
	s.mu.Unlock()

	if dnsClient == nil {
		log.Println("getaddrinfo called, but no DNS client available.")
		return net.AddrInfoStatusFail, nil
	}

	if hints == nil {
		hints = &net.AddrInfoHints{}
	}
	if hints.SockType == 0 {
		hints.SockType = SOCK_STREAM
	}
	if hints.Protocol == 0 {
		if hints.SockType == SOCK_STREAM {
			hints.Protocol = IPPROTO_TCP
		} else if hints.SockType == SOCK_DGRAM {
			hints.Protocol = IPPROTO_UDP
		}
	}

	t, err := sockProto(int(hints.SockType), int(hints.Protocol))
	if err != nil {
		if debug {
			log.Printf("getaddrinfo: sockProto: %v", err)
		}
		return net.AddrInfoStatusSystemError, nil
	}
	var port uint16
	if service != nil && *service != "" {
		port, err = serviceLookup(*service, t)
		if err != nil {
			if debug {
				log.Printf("getaddrinfo: serviceLookup: %v", err)
			}
			return net.AddrInfoStatusSystemError, nil
		}
	}

	var addrs []tcpip.Address
	if node == nil {
		addrs = append(addrs, "\x00\x00\x00\x00")
	} else {
		addrs, err = dnsClient.LookupIP(*node)
		if err != nil {
			if *node == "localhost" {
				addrs = append(addrs, "\x7f\x00\x00\x01")
			} else {
				addrs = append(addrs, tcpip.Parse(*node))
				if debug2 {
					log.Printf("getaddrinfo: addr=%v, err=%v", addrs, err)
				}
			}
		}
	}

	if len(addrs) == 0 || len(addrs[0]) == 0 {
		return net.AddrInfoStatusNoName, nil
	}

	n := 0
	addrInfo := make([]*net.AddrInfo, n, len(addrs))
	for i := 0; i < len(addrs); i++ {
		ai := &net.AddrInfo{
			Flags:    0,
			SockType: hints.SockType,
			Protocol: hints.Protocol,
			Port:     port,
		}

		switch len(addrs[i]) {
		case 4:
			if hints.Family != AF_UNSPEC && hints.Family != AF_INET {
				continue
			}
			ai.Family = AF_INET
			ai.Addr.Len = 4
			copy(ai.Addr.Val[:4], addrs[i])
		case 16:
			if hints.Family != AF_UNSPEC && hints.Family != AF_INET6 {
				continue
			}
			ai.Family = AF_INET6
			ai.Addr.Len = 16
			copy(ai.Addr.Val[:16], addrs[i])
		default:
			if debug {
				log.Printf("getaddrinfo: len(addr)=%d, wrong size", len(addrs[i]))
			}
			// TODO: failing to resolve is a valid reply. fill out retval
			return net.AddrInfoStatusSystemError, nil
		}

		n++
		addrInfo = addrInfo[:n]
		addrInfo[n-1] = ai
	}

	return net.AddrInfoStatusOk, addrInfo
}

func (s *socketServer) opFcntl(ios *iostate, msg *zxsocket.Msg) zx.Status {
	cmd := uint32(msg.Arg)
	if debug2 {
		log.Printf("fcntl: cmd %v, flags %v", cmd, msg.FcntlFlags())
	}
	switch cmd {
	case fdio.OpFcntlCmdGetFL:
		// Set flags to 0 as O_NONBLOCK is handled on the client side.
		msg.SetFcntlFlags(0)
	case fdio.OpFcntlCmdSetFL:
		// Do nothing.
	default:
		return zx.ErrNotSupported
	}
	msg.Datalen = 0
	return zx.ErrOk
}

func (s *socketServer) opClose(ios *iostate, cookie cookie) zx.Status {
	s.mu.Lock()
	delete(s.io, cookie)
	s.mu.Unlock()

	// Signal that we're about to close. This tells the various message loops to finish
	// processing, and let us know when they're done.
	err := ios.dataHandle.Signal(0, LOCAL_SIGNAL_CLOSING)
	if ios.listenLoopClosing != nil {
		ios.listenLoopClosing <- struct{}{}
	}

	go func() {
		switch mxerror.Status(err) {
		case zx.ErrOk:
			if ios.writeLoopDone != nil {
				<-ios.writeLoopDone
			}
			if ios.controlLoopDone != nil {
				<-ios.controlLoopDone
			}
			if ios.listenLoopDone != nil {
				<-ios.listenLoopDone
			}
		case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
			// Ignore.
		default:
			log.Printf("close: signal failed: %v", err)
		}

		if ios.ep != nil {
			ios.ep.Close()
		}
		ios.dataHandle.Close()
	}()

	return zx.ErrOk
}

func (s *socketServer) fdioHandler(msg *fdio.Msg, rh zx.Handle, cookieVal int64) zx.Status {
	cookie := cookie(cookieVal)
	op := msg.Op()
	if debug2 {
		log.Printf("fdioHandler: op=%v, len=%d, arg=%v, hcount=%d", op, msg.Datalen, msg.Arg, msg.Hcount)
	}

	s.mu.Lock()
	ios := s.io[cookie]
	s.mu.Unlock()
	if ios == nil {
		if op == fdio.OpOpen {
			// iostate has not been allocated if the open op is for "none" and "socket",
			// continue to the switch below.
			// TODO: return an error if the open op is for "accept".
		} else if op == fdio.OpClose && (rh == 0 || cookie == 0) {
			// There are two special cases we can simply return here:
			// 1. [rh == 0] the close op was synthesized by Dispatcher (because
			//    the peer channel was closed).
			// 2. [rh != 0 and cookie == 0] the close op was for the open handle
			//    of netstack node in the namespace (which is not a socket).
			return zx.ErrOk
		} else {
			log.Printf("fdioHandler: request (op:%v) dropped because of the state mismatch", op)
			return zx.ErrBadState
		}
	}

	switch op {
	case fdio.OpOpen:
		path := string(msg.Data[:msg.Datalen])
		openProto := 3
		var err error
		switch {
		case strings.HasPrefix(path, "none-v2"):
			openProto := 2
			err = s.newIostate(msg.Handle[0], ipv4.ProtocolNumber, tcp.ProtocolNumber, nil, nil, false, openProto, fdioIostateResponse(msg.Handle[0], openProto))
		case strings.HasPrefix(path, "none-v3"): // ZXRIO_SOCKET_DIR_NONE

			err = s.newIostate(msg.Handle[0], ipv4.ProtocolNumber, tcp.ProtocolNumber, nil, nil, false, openProto, fdioIostateResponse(msg.Handle[0], openProto))
		case strings.HasPrefix(path, "socket-v"):
			var sp socketPath
			if sp, err = parseSocketPath(path); err == nil {
				openProto = sp.version
				err = s.opSocket(msg.Handle[0], sp, fdioIostateResponse(msg.Handle[0], openProto))
			}
		default:
			if debug2 {
				log.Printf("open: unknown path=%q", path)
			}
			err = mxerror.Errorf(zx.ErrNotSupported, "open: unknown path=%q", path)
		}

		if err != nil {
			switch openProto {
			case 2:
				ro := fdio.RioDescription{
					Status: errStatus(err),
				}
				ro.SetOp(fdio.OpOnOpen)
				ro.Write(msg.Handle[0], 0)
			case 3:
				r := openReply{
					Status: errStatus(err),
				}
				r.Write(msg.Handle[0])
			}
			msg.Handle[0].Close()
		}
		return fdio.ErrIndirect.Status
	case fdio.OpClose:
		return s.opClose(ios, cookie)
	default:
		log.Printf("fdioHandler: unknown socket op: %v", op)
		return zx.ErrNotSupported
	}
	return zx.ErrBadState
	// TODO do_halfclose
}

func (s *socketServer) zxsocketHandler(msg *zxsocket.Msg, rh zx.Socket, cookieVal int64) zx.Status {
	cookie := cookie(cookieVal)
	op := msg.Op()
	if debug2 {
		log.Printf("zxsocketHandler: op=%v, len=%d, arg=%v, hcount=%d", op, msg.Datalen, msg.Arg, msg.Hcount)
	}

	s.mu.Lock()
	ios := s.io[cookie]
	s.mu.Unlock()
	if ios == nil {
		if op == fdio.OpClose && rh == 0 {
			// The close op was synthesized by Dispatcher (because the peer channel was closed).
			return zx.ErrOk
		}
		log.Printf("zxsioHandler: request (op:%v) dropped because of the state mismatch", op)
		return zx.ErrBadState
	}

	switch op {
	case fdio.OpConnect:
		return s.opConnect(ios, msg) // do_connect
	case fdio.OpClose:
		return s.opClose(ios, cookie)
	case fdio.OpRead:
		if debug {
			log.Printf("unexpected opRead")
		}
	case fdio.OpWrite:
		if debug {
			log.Printf("unexpected opWrite")
		}
	case fdio.OpWriteAt:
	case fdio.OpSeek:
		return zx.ErrOk
	case fdio.OpStat:
	case fdio.OpTruncate:
	case fdio.OpSync:
	case fdio.OpSetAttr:
	case fdio.OpBind:
		return s.opBind(ios, msg)
	case fdio.OpListen:
		return s.opListen(ios, msg)
	case fdio.OpIoctl:
		return s.opIoctl(ios, msg)
	case fdio.OpGetAddrInfo:
		return s.opGetAddrInfo(ios, msg)
	case fdio.OpGetSockname:
		return s.opGetSockName(ios, msg)
	case fdio.OpGetPeerName:
		return s.opGetPeerName(ios, msg)
	case fdio.OpGetSockOpt:
		return s.opGetSockOpt(ios, msg)
	case fdio.OpSetSockOpt:
		return s.opSetSockOpt(ios, msg)
	case fdio.OpFcntl:
		return s.opFcntl(ios, msg)
	default:
		log.Printf("zxsocketHandler: unknown socket op: %v", op)
		return zx.ErrNotSupported
	}
	return zx.ErrBadState
	// TODO do_halfclose
}

type openReply struct {
	Status zx.Status
	Handle zx.Handle
}

func (r *openReply) Write(h zx.Handle) {
	data := make([]byte, 4)
	binary.LittleEndian.PutUint32(data, uint32(r.Status))
	handles := []zx.Handle{r.Handle}
	c := zx.Channel(h)
	c.Write(data, handles, 0)
}

func init() {
	// The marshalling in openReply.Write assumes that
	// zx.Status is a 32bit integer.
	if binary.Size(zx.Status(0)) != 4 {
		panic("zx.Status is not 4 bytes")
	}
}
