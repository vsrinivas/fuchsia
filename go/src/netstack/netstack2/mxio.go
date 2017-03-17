// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/binary"
	"fmt"
	"log"
	"os"
	"strings"
	"sync"
	"syscall"
	"syscall/mx"
	"syscall/mx/mxerror"
	"syscall/mx/mxio"
	"syscall/mx/mxio/dispatcher"
	"syscall/mx/mxio/rio"

	"github.com/google/netstack/dns"
	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/buffer"
	"github.com/google/netstack/tcpip/network/ipv4"
	"github.com/google/netstack/tcpip/network/ipv6"
	"github.com/google/netstack/tcpip/transport/tcp"
	"github.com/google/netstack/tcpip/transport/udp"
	"github.com/google/netstack/waiter"
)

const debug = true
const debug2 = false

const MX_SOCKET_HALF_CLOSE = 1
const MX_SOCKET_READABLE = mx.SignalObject0
const MX_SOCKET_WRITABLE = mx.SignalObject1
const MX_SOCKET_PEER_CLOSED = mx.SignalObject2
const MXSIO_SIGNAL_INCOMING = mx.SignalUser0
const MXSIO_SIGNAL_CONNECTED = mx.SignalUser3
const MXSIO_SIGNAL_HALFCLOSED = mx.SignalUser4

const defaultNIC = 2

func devmgrConnect() (mx.Handle, error) {
	f, err := os.OpenFile("/dev/socket", O_DIRECTORY|O_RDWR, 0)
	if err != nil {
		log.Printf("could not open /dev/socket: %v", err)
		return 0, err
	}
	defer f.Close()

	c0, c1, err := mx.NewChannel(0)
	if err != nil {
		log.Printf("could not create socket fs channel: %v", err)
		return 0, err
	}
	err = syscall.IoctlSetHandle(int(f.Fd()), mxio.IoctlDevmgrMountFS, c1.Handle)
	if err != nil {
		c0.Close()
		c1.Close()
		return 0, fmt.Errorf("failed to mount /dev/socket: %v", err)
	}
	return c0.Handle, nil
}

func socketDispatcher(stk tcpip.Stack) (*socketServer, error) {
	d, err := dispatcher.New(rio.Handler)
	if err != nil {
		return nil, err
	}
	s := &socketServer{
		dispatcher: d,
		stack:      stk,
		io:         make(map[cookie]*iostate),
		next:       1,
		nicData:    make(map[tcpip.NICID]*nicData),
	}

	h, err := devmgrConnect()
	if err != nil {
		return nil, fmt.Errorf("devmgr: %v", err)
	}

	if err := d.AddHandler(h, rio.ServerHandler(s.mxioHandler), 0); err != nil {
		h.Close()
		return nil, err
	}

	// We're ready to serve
	// TODO(crawshaw): give this signal a name.
	if err := h.SignalPeer(0, mx.SignalUser0); err != nil {
		h.Close()
		return nil, err
	}

	go d.Serve()
	return s, nil
}

func (s *socketServer) getNICData(nicid tcpip.NICID) *nicData {
	if d, ok := s.nicData[nicid]; ok {
		return d
	}

	s.nicData[nicid] = &nicData{
		ready: make(chan struct{}),
	}
	return s.nicData[nicid]
}

func (s *socketServer) setAddr(nicid tcpip.NICID, addr tcpip.Address) {
	s.mu.Lock()
	defer s.mu.Unlock()

	d := s.getNICData(nicid)
	close(d.ready)
	d.addr = addr

	if s.dnsClient == nil {
		// TODO(mpcomplete): Which NIC should we use for DNS requests?
		s.dnsClient = dns.NewClient(s.stack, defaultNIC)
	}
}

type cookie int64

type iostate struct {
	cookie cookie
	wq     *waiter.Queue
	ep     tcpip.Endpoint

	transProto tcpip.TransportProtocolNumber
	// dataHandle is used to communicate with libc.
	// dataHandle is an mx.Socket for TCP, or an mx.Channel for UDP.
	dataHandle mx.Handle
}

// loopSocketWrite connects libc write to the network stack for TCP sockets.
//
// TODO: replace WaitOne with a method that parks goroutines when waiting
// for a signal on an mx.Socket.
//
// As written, we have two netstack threads per socket.
// That's not so bad for small client work, but even a client OS is
// eventually going to feel the overhead of this.
func (ios *iostate) loopSocketWrite(stk tcpip.Stack) {
	dataHandle := mx.Socket(ios.dataHandle)

	// Warm up.
	_, err := dataHandle.WaitOne(MX_SOCKET_READABLE|MX_SOCKET_PEER_CLOSED, mx.TimensecInfinite)
	if err != nil {
		log.Printf("loopSocketWrite: warmup failed: %v", err)
		return
	}

	waitEntry, notifyCh := waiter.NewChannelEntry(nil)
	for {
		// TODO: obviously allocating for each read is silly.
		// A quick hack we can do is store these in a ring buffer,
		// as the lifecycle of this buffer.View starts here, and
		// ends in nearby code we control in link.go.
		v := buffer.NewView(2048)
		n, err := dataHandle.Read([]byte(v), 0)
		if mxerror.Status(err) == mx.ErrShouldWait {
			obs, err := dataHandle.WaitOne(MX_SOCKET_READABLE|MX_SOCKET_PEER_CLOSED, mx.TimensecInfinite)
			if err != nil {
				log.Printf("loopSocketWrite: wait failed: %v", ios.cookie, err)
				return
			}
			switch {
			case obs&MX_SOCKET_PEER_CLOSED != 0:
				if debug2 {
					log.Printf("loopSocketWrite: MX_SOCKET_PEER_CLOSED on socket: %x", obs)
				}
				return
			case obs&MX_SOCKET_READABLE != 0:
				continue
			}
		} else if mxerror.Status(err) == mx.ErrRemoteClosed {
			return
		} else if err != nil {
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

func (ios *iostate) loopRead(stk tcpip.Stack) {
	switch ios.transProto {
	case tcp.ProtocolNumber:
		go ios.loopSocketRead(stk)
	case udp.ProtocolNumber:
		go ios.loopDgramRead(stk)
	}
}

// loopSocketRead connects libc read to the network stack for TCP sockets.
func (ios *iostate) loopSocketRead(stk tcpip.Stack) {
	dataHandle := mx.Socket(ios.dataHandle)

	// Warm up.
	obs, err := dataHandle.WaitOne(MX_SOCKET_WRITABLE|MX_SOCKET_PEER_CLOSED, mx.TimensecInfinite)
	if err != nil {
		if debug {
			log.Printf("loopSocketRead: warmup failed: %v", err)
		}
		return
	}
	switch {
	case obs&MX_SOCKET_PEER_CLOSED != 0:
		return
	}

	waitEntry, notifyCh := waiter.NewChannelEntry(nil)
	for {
		ios.wq.EventRegister(&waitEntry, waiter.EventIn)
		var v buffer.View
		var err error
		for {
			v, err = ios.ep.Read(nil)
			if err == nil {
				break
			} else if err == tcpip.ErrWouldBlock {
				if debug2 {
					log.Printf("loopSocketRead got tcpip.ErrWouldBlock")
				}
				<-notifyCh
				// TODO: get socket closed message from loopSocketWrite
				continue
			} else if err == tcpip.ErrClosedForReceive {
				_, err := dataHandle.Write(nil, MX_SOCKET_HALF_CLOSE)
				if err != nil && mxerror.Status(err) != mx.ErrRemoteClosed {
					log.Printf("socket read: send MX_SOCKET_HALF_CLOSE failed: %v", err)
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

		for {
			_, err = dataHandle.Write([]byte(v), 0)
			if err == nil {
				break
			} else if mxerror.Status(err) == mx.ErrShouldWait {
				if debug2 {
					log.Printf("loopSocketRead: gto mx.ErrShouldWait")
				}
				obs, err := dataHandle.WaitOne(
					MX_SOCKET_WRITABLE|MX_SOCKET_PEER_CLOSED,
					mx.TimensecInfinite,
				)
				if err != nil {
					log.Printf("loopSocketRead: wait failed: %v", ios.cookie, err)
					return
				}
				switch {
				case obs&MX_SOCKET_PEER_CLOSED != 0:
					return
				case obs&MX_SOCKET_WRITABLE != 0:
					continue
				}
			} else if mxerror.Status(err) == mx.ErrRemoteClosed {
				log.Printf("loopSocketRead: got ErrRemoteClosed")
				return
			}
			log.Printf("socket write failed: %v", err) // TODO: communicate this
			break
		}
	}
}

func (ios *iostate) loopShutdown() {
	obs, err := ios.dataHandle.WaitOne(MX_SOCKET_PEER_CLOSED|MXSIO_SIGNAL_HALFCLOSED, mx.TimensecInfinite)
	if err != nil {
		return
	}
	switch {
	case obs&MX_SOCKET_PEER_CLOSED != 0:
		ios.ep.Close()
	case obs&MXSIO_SIGNAL_HALFCLOSED != 0:
		if err := ios.ep.Shutdown(tcpip.ShutdownRead | tcpip.ShutdownWrite); err != nil {
			if debug {
				log.Printf("shutdown failed: %v", err)
			}
		}
	}
}

// loopDgramRead connects libc read to the network stack for UDP messages.
func (ios *iostate) loopDgramRead(stk tcpip.Stack) {
	dataHandle := mx.Channel{ios.dataHandle}

	waitEntry, notifyCh := waiter.NewChannelEntry(nil)
	for {
		ios.wq.EventRegister(&waitEntry, waiter.EventIn)
		var sender tcpip.FullAddress
		var v buffer.View
		var err error
		for {
			v, err = ios.ep.Read(&sender)
			if err == nil {
				break
			} else if err == tcpip.ErrWouldBlock {
				<-notifyCh
				continue
			} else if err == tcpip.ErrClosedForReceive {
				log.Printf("TODO loopDgramRead closed")
				// TODO _, err := ios.dataHandle.Write(nil, MX_SOCKET_HALF_CLOSE)
				return
			}
			// TODO communicate to user
			log.Printf("loopDgramRead got endpoint error: %v (TODO)", err)
			return
		}
		ios.wq.EventUnregister(&waitEntry)

		out := make([]byte, c_mxio_socket_msg_hdr_len+len(v))
		writeSocketMsgHdr(out, sender)
		copy(out[c_mxio_socket_msg_hdr_len:], v)

		for {
			err = dataHandle.Write(out, nil, 0)
			if err == nil {
				break
			} else if mxerror.Status(err) == mx.ErrRemoteClosed {
				if debug2 {
					log.Printf("loopDgramRead: got ErrRemoteClosed")
				}
				return
			}
			log.Printf("socket write failed: %v", err) // TODO: communicate this
			break
		}
	}
}

// loopDgramWrite connects libc write to the network stack for UDP messages.
func (ios *iostate) loopDgramWrite(stk tcpip.Stack) {
	dataHandle := mx.Channel{ios.dataHandle}

	waitEntry, notifyCh := waiter.NewChannelEntry(nil)
	for {
		v := buffer.NewView(2048)
		n, _, err := dataHandle.Read([]byte(v), nil, 0)
		if mxerror.Status(err) == mx.ErrShouldWait {
			obs, err := dataHandle.Handle.WaitOne(mx.SignalChannelReadable|mx.SignalChannelPeerClosed, mx.TimensecInfinite)
			if err != nil {
				return
			}
			switch {
			case obs&mx.SignalChannelPeerClosed != 0:
				return
			case obs&MX_SOCKET_READABLE != 0:
				continue
			}
		} else if mxerror.Status(err) == mx.ErrRemoteClosed {
			return
		} else if err != nil {
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
			_, err := ios.ep.Write(v[c_mxio_socket_msg_hdr_len:], receiver)
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

func (s *socketServer) newIostate(transProto tcpip.TransportProtocolNumber, wq *waiter.Queue, ep tcpip.Endpoint) (ios *iostate, peerH, peerS mx.Handle, reterr error) {
	h0, h1, err := mx.NewChannel(0)
	if err != nil {
		return nil, 0, 0, err
	}
	defer func() {
		if reterr != nil {
			h0.Close()
			h1.Close()
		}
	}()

	ios = &iostate{
		transProto: transProto,
		wq:         wq,
		ep:         ep,
	}
	switch transProto {
	case tcp.ProtocolNumber:
		s0, s1, err := mx.NewSocket(0)
		if err != nil {
			return nil, 0, 0, err
		}
		ios.dataHandle = mx.Handle(s0)
		peerS = mx.Handle(s1)
	case udp.ProtocolNumber:
		c0, c1, err := mx.NewChannel(0)
		if err != nil {
			return nil, 0, 0, err
		}
		ios.dataHandle = c0.Handle
		peerS = c1.Handle
	default:
		panic(fmt.Sprintf("unknown transport protocol number: %v", transProto))
	}
	defer func() {
		if reterr != nil {
			ios.dataHandle.Close()
			peerS.Close()
		}
	}()

	s.mu.Lock()
	ios.cookie = s.next
	s.next++
	s.io[ios.cookie] = ios
	s.mu.Unlock()

	if err := s.dispatcher.AddHandler(h0.Handle, rio.ServerHandler(s.mxioHandler), int64(ios.cookie)); err != nil {
		s.mu.Lock()
		delete(s.io, ios.cookie)
		s.mu.Unlock()

		return nil, 0, 0, err
	}

	switch transProto {
	case tcp.ProtocolNumber:
		if ep != nil {
			go ios.loopShutdown()
			go ios.loopSocketWrite(s.stack)
		}
	case udp.ProtocolNumber:
		go ios.loopShutdown()
		go ios.loopDgramWrite(s.stack)
	}

	return ios, h1.Handle, peerS, nil
}

type socketServer struct {
	dispatcher *dispatcher.Dispatcher
	stack      tcpip.Stack
	dnsClient  *dns.Client

	mu      sync.Mutex
	next    cookie
	nicData map[tcpip.NICID]*nicData
	io      map[cookie]*iostate
}

type nicData struct {
	ready chan struct{}
	addr  tcpip.Address
}

func (s *socketServer) opSocket(ios *iostate, msg *rio.Msg, path string) (peerH, peerS mx.Handle, err error) {
	var domain, typ, protocol int
	if n, _ := fmt.Sscanf(path, "socket/%d/%d/%d\x00", &domain, &typ, &protocol); n != 3 {
		return 0, 0, mxerror.Errorf(mx.ErrInvalidArgs, "socket: bad path %q (n=%d)", path, n)
	}

	var n tcpip.NetworkProtocolNumber
	switch domain {
	case AF_INET:
		n = ipv4.ProtocolNumber
	case AF_INET6:
		n = ipv6.ProtocolNumber
	default:
		return 0, 0, mxerror.Errorf(mx.ErrNotSupported, "socket: unknown network protocol: %d", domain)
	}

	transProto, err := sockProto(typ, protocol)
	if err != nil {
		return 0, 0, err
	}

	wq := new(waiter.Queue)
	ep, err := s.stack.NewEndpoint(transProto, n, wq)
	if err != nil {
		if debug {
			log.Printf("socket: new endpoint: %v", err)
		}
		return 0, 0, err
	}
	if n == ipv6.ProtocolNumber {
		if err := ep.SetSockOpt(tcpip.V6OnlyOption(0)); err != nil {
			log.Printf("socket: setsockopt v6only option failed: %v", err)
		}
	}
	ios, peerH, peerS, err = s.newIostate(transProto, wq, ep)
	if err != nil {
		if debug {
			log.Printf("socket: new iostate: %v", err)
		}
		return 0, 0, err
	}

	return peerH, peerS, nil
}

func sockProto(typ, protocol int) (t tcpip.TransportProtocolNumber, err error) {
	switch typ {
	case SOCK_STREAM:
		switch protocol {
		case IPPROTO_IP, IPPROTO_TCP:
			return tcp.ProtocolNumber, nil
		default:
			return 0, mxerror.Errorf(mx.ErrNotSupported, "unsupported SOCK_STREAM protocol: %d", protocol)
		}
	case SOCK_DGRAM:
		switch protocol {
		case IPPROTO_IP, IPPROTO_UDP:
			return udp.ProtocolNumber, nil
		default:
			return 0, mxerror.Errorf(mx.ErrNotSupported, "unsupported SOCK_DGRAM protocol: %d", protocol)
		}
	}
	return 0, mxerror.Errorf(mx.ErrNotSupported, "unsupported protocol: %d/%d", typ, protocol)
}

var errShouldWait = mx.Error{Status: mx.ErrShouldWait, Text: "netstack"}

func (s *socketServer) opAccept(ios *iostate, msg *rio.Msg, path string) (peerH, peerS mx.Handle, err error) {
	if ios.ep == nil {
		return 0, 0, mxerror.Errorf(mx.ErrBadState, "accept: no socket")
	}
	newep, newwq, err := ios.ep.Accept()
	if err == tcpip.ErrWouldBlock {
		return 0, 0, errShouldWait
	}
	if ios.ep.Readiness(waiter.EventIn) == 0 {
		// If we just accepted the only queued incoming connection,
		// clear the signal so the mxio client knows no incoming
		// connection is available.
		if err := mx.Handle(ios.dataHandle).SignalPeer(MXSIO_SIGNAL_INCOMING, 0); err != nil {
			log.Printf("accept: clearing MXSIO_SIGNAL_INCOMING: %v", err)
		}
	}
	if err != nil {
		if debug {
			log.Printf("accept: %v", err)
		}
		return 0, 0, err
	}
	go ios.loopRead(s.stack)
	_, peerH, peerS, err = s.newIostate(ios.transProto, newwq, newep)
	return peerH, peerS, err
}

func errStatus(err error) mx.Status {
	if err == nil {
		return mx.ErrOk
	}
	if s, ok := err.(mx.Error); ok {
		return s.Status
	}
	log.Printf("%v", err)
	return mx.ErrInternal
}

func (s *socketServer) opGetSockOpt(ios *iostate, msg *rio.Msg) mx.Status {
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
		return mx.ErrBadState
	}
	if opt := val.Unpack(); opt != nil {
		ios.ep.GetSockOpt(opt)
	}
	msg.Datalen = 0
	msg.SetOff(0)
	return mx.ErrOk
}

func (s *socketServer) opSetSockOpt(ios *iostate, msg *rio.Msg) mx.Status {
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
		return mx.ErrBadState
	}
	if opt := val.Unpack(); opt != nil {
		ios.ep.SetSockOpt(opt)
	}
	msg.Datalen = 0
	msg.SetOff(0)
	return mx.ErrOk
}

func (s *socketServer) opBind(ios *iostate, msg *rio.Msg) mx.Status {
	addr, err := readSockaddrIn(msg.Data[:msg.Datalen])
	if err != nil {
		if debug {
			log.Printf("bind: bad input: %v", err)
		}
		return errStatus(err)
	}

	if ios.ep == nil {
		if debug {
			log.Printf("bind: no socket")
		}
		return mx.ErrBadState
	}
	if debug2 {
		log.Printf("bind(%s)", *addr)
	}
	if err := ios.ep.Bind(*addr, nil); err != nil {
		return errStatus(err)
	}
	msg.Datalen = 0
	msg.SetOff(0)
	return mx.ErrOk
}

func (s *socketServer) opIoctl(ios *iostate, msg *rio.Msg) mx.Status {
	if debug {
		log.Printf("opIoctl TODO op=0x%x, datalen=%d", msg.Op(), msg.Datalen)
	}
	return mx.ErrInvalidArgs
}

func mxioSockAddrReply(a tcpip.FullAddress, msg *rio.Msg) mx.Status {
	var err error
	rep := c_mxrio_sockaddr_reply{}
	rep.len, err = writeSockaddrStorage(&rep.addr, a)
	if err != nil {
		return errStatus(err)
	}
	rep.Encode(msg)
	msg.SetOff(0)
	return mx.ErrOk
}

func (s *socketServer) opGetSockName(ios *iostate, msg *rio.Msg) mx.Status {
	a, err := ios.ep.GetLocalAddress()
	if err != nil {
		return errStatus(err)
	}
	if debug2 {
		log.Printf("getsockname(): %v", a)
	}
	return mxioSockAddrReply(a, msg)
}

func (s *socketServer) opGetPeerName(ios *iostate, msg *rio.Msg) (status mx.Status) {
	if ios.ep == nil {
		return mx.ErrBadState
	}
	a, err := ios.ep.GetRemoteAddress()
	if err != nil {
		return errStatus(err)
	}
	return mxioSockAddrReply(a, msg)
}

func (s *socketServer) opListen(ios *iostate, msg *rio.Msg) (status mx.Status) {
	d := msg.Data[:msg.Datalen]
	if len(d) != 4 {
		if debug {
			log.Printf("listen: bad input length %d", len(d))
		}
		return mx.ErrInvalidArgs
	}
	backlog := binary.LittleEndian.Uint32(d)
	if ios.ep == nil {
		if debug {
			log.Printf("listen: no socket")
		}
		return mx.ErrBadState
	}
	if err := ios.ep.Listen(int(backlog)); err != nil {
		if debug {
			log.Printf("listen: %v", err)
		}
		return errStatus(err)
	}
	go func() {
		// When an incoming connection is queued up (that is,
		// calling accept would return a new connection),
		// signal the mxio socket that it exists. This allows
		// the socket API client to implement a blocking accept.
		inEntry, inCh := waiter.NewChannelEntry(nil)
		ios.wq.EventRegister(&inEntry, waiter.EventIn)
		defer ios.wq.EventUnregister(&inEntry)
		for range inCh {
			if err := mx.Handle(ios.dataHandle).SignalPeer(0, MXSIO_SIGNAL_INCOMING); err != nil {
				if mxerror.Status(err) == mx.ErrRemoteClosed {
					return
				}
				log.Printf("socket signal MXSIO_SIGNAL_INCOMING: %v", err)
			}
		}
	}()

	msg.Datalen = 0
	msg.SetOff(0)
	return mx.ErrOk
}

func (s *socketServer) opConnect(ios *iostate, msg *rio.Msg) mx.Status {
	if msg.Datalen == 0 {
		if ios.transProto == udp.ProtocolNumber {
			// connect() can be called with no address to
			// disassociate UDP sockets.
			ios.ep.Shutdown(tcpip.ShutdownRead)
			return mx.ErrOk
		}
		return mx.ErrInvalidArgs
	}
	addr, err := readSockaddrIn(msg.Data[:msg.Datalen])
	if err != nil {
		if debug {
			log.Printf("connect bad input: %v", err)
		}
		return errStatus(err)
	}

	waitEntry, notifyCh := waiter.NewChannelEntry(nil)
	ios.wq.EventRegister(&waitEntry, waiter.EventOut)
	if debug2 {
		log.Printf("connect(%s)", *addr)
	}
	err = ios.ep.Connect(*addr)
	if err == tcpip.ErrConnectStarted {
		<-notifyCh
		err = ios.ep.GetSockOpt(tcpip.ErrorOption{})
	}
	ios.wq.EventUnregister(&waitEntry)
	if err != nil {
		log.Printf("connect: addr=%s, %v", *addr, err)
		return errStatus(err)
	}
	if debug2 {
		log.Printf("connect: connected")
	}
	if ios.transProto == tcp.ProtocolNumber {
		if err := mx.Handle(ios.dataHandle).SignalPeer(0, MXSIO_SIGNAL_CONNECTED); err != nil {
			log.Printf("connect: signal failed: %v", err)
		}
	}
	go ios.loopRead(s.stack)

	msg.SetOff(0)
	msg.Datalen = 0
	return mx.ErrOk
}

func (s *socketServer) opGetAddrInfo(ios *iostate, msg *rio.Msg) mx.Status {
	var val c_mxrio_gai_req
	if err := val.Decode(msg); err != nil {
		return errStatus(err)
	}
	node, service, hints := val.Unpack()

	if debug {
		log.Printf("getaddrinfo node=%q, service=%q", node, service)
	}

	// TODO(mpcomplete): Which NIC should we use for DNS requests?
	s.mu.Lock()
	nicData := s.getNICData(defaultNIC)
	select {
	case <-nicData.ready:
	default:
		s.mu.Unlock()

		const EAI_FAIL = -4
		rep := c_mxrio_gai_reply{retval: EAI_FAIL}
		rep.Encode(msg)
		return mx.ErrOk
	}
	dnsClient := s.dnsClient
	s.mu.Unlock()

	if hints.ai_socktype == 0 {
		hints.ai_socktype = SOCK_STREAM
	}
	if hints.ai_protocol == 0 {
		hints.ai_protocol = IPPROTO_TCP
	}
	t, err := sockProto(int(hints.ai_socktype), int(hints.ai_protocol))
	if err != nil {
		return errStatus(err)
	}
	var port uint16
	if service != "" {
		port, err = serviceLookup(service, t)
		if err != nil {
			log.Printf("getAddrInfo: %v", err)
			return mx.ErrNotSupported
		}
	}

	var addr tcpip.Address
	dnsLookupIPs, err := dnsClient.LookupIP(node)
	if err != nil {
		if node == "localhost" {
			addr = "\x7f\x00\x00\x01"
		} else {
			addr = tcpip.Parse(node)
		}
	} else {
		for _, ip := range dnsLookupIPs {
			if ip4 := ip.To4(); ip4 != "" {
				addr = ip4
			} else {
				addr = ip
			}
			break
		}
	}
	if debug2 {
		log.Printf("getaddrinfo: addr=%v", addr)
	}

	// TODO: error if hints.ai_family does not match address size

	rep := c_mxrio_gai_reply{}
	rep.res[0].ai.ai_socktype = hints.ai_socktype
	rep.res[0].ai.ai_protocol = hints.ai_protocol
	// The 0xdeadbeef constant indicates the other side needs to
	// adjust ai_addr with the value passed below.
	rep.res[0].ai.ai_addr = 0xdeadbeef
	switch len(addr) {
	case 0, 4:
		rep.res[0].ai.ai_family = AF_INET
		rep.res[0].ai.ai_addrlen = c_socklen(c_sockaddr_in_len)
		sockaddr := c_sockaddr_in{sin_family: AF_INET}
		sockaddr.sin_port.setPort(port)
		copy(sockaddr.sin_addr[:], addr)
		writeSockaddrStorage4(&rep.res[0].addr, &sockaddr)
	case 16:
		rep.res[0].ai.ai_family = AF_INET6
		rep.res[0].ai.ai_addrlen = c_socklen(c_sockaddr_in6_len)
		sockaddr := c_sockaddr_in6{sin6_family: AF_INET6}
		sockaddr.sin6_port.setPort(port)
		copy(sockaddr.sin6_addr[:], addr)
		writeSockaddrStorage6(&rep.res[0].addr, &sockaddr)
	default:
		if debug {
			log.Printf("getaddrinfo: len(addr)=%d, wrong size", len(addr))
		}
		// TODO: failing to resolve is a valid reply. fill out retval
		return mx.ErrBadState
	}
	rep.nres = 1
	rep.Encode(msg)
	return mx.ErrOk
}

func (s *socketServer) mxioHandler(msg *rio.Msg, rh mx.Handle, cookieVal int64) mx.Status {
	cookie := cookie(cookieVal)
	op := msg.Op()
	if debug2 {
		log.Printf("socketServer.mxio: op=%v, len=%d, arg=%v, hcount=%d", op, msg.Datalen, msg.Arg, msg.Hcount)
	}

	if rh <= 0 {
		if debug2 {
			log.Printf("socketServer.mxio invalid rh (op=%v)", op) // DEBUG
		}
		return mx.ErrInvalidArgs
	}
	s.mu.Lock()
	ios := s.io[cookie]
	s.mu.Unlock()

	switch op {
	case rio.OpOpen:
		path := string(msg.Data[:msg.Datalen])
		var err error
		var peerH, peerS mx.Handle
		switch {
		case strings.HasPrefix(path, "none"): // MXRIO_SOCKET_DIR_NONE
			_, peerH, peerS, err = s.newIostate(tcp.ProtocolNumber, nil, nil)
		case strings.HasPrefix(path, "socket/"): // MXRIO_SOCKET_DIR_SOCKET
			peerH, peerS, err = s.opSocket(ios, msg, path)
		case strings.HasPrefix(path, "accept"): // MXRIO_SOCKET_DIR_ACCEPT
			peerH, peerS, err = s.opAccept(ios, msg, path)
		default:
			if debug2 {
				log.Printf("open: unknown path=%q", path)
			}
			return mx.ErrNotSupported
		}
		ro := rio.RioObject{
			RioObjectHeader: rio.RioObjectHeader{
				Status: errStatus(err),
				Type:   uint32(mxio.ProtocolSocket),
			},
			Esize: 0,
		}
		if peerH != 0 && peerS != 0 {
			ro.Handle[0] = peerH
			ro.Handle[1] = peerS
			ro.Hcount = 2
		}
		ro.Write(msg.Handle[0], 0)
		msg.Handle[0].Close()
		return dispatcher.ErrIndirect.Status

	case rio.OpConnect:
		return s.opConnect(ios, msg) // do_connect
	case rio.OpClose:
		if ios.ep != nil {
			ios.ep.Close()
		}
		s.mu.Lock()
		delete(s.io, cookie)
		s.mu.Unlock()
		return mx.ErrOk
	case rio.OpRead:
		if debug {
			log.Printf("unexpected opRead")
		}
	case rio.OpWrite:
		if debug {
			log.Printf("unexpected opWrite")
		}
	case rio.OpWriteAt:
	case rio.OpSeek:
		return mx.ErrOk
	case rio.OpStat:
	case rio.OpTruncate:
	case rio.OpSync:
	case rio.OpSetAttr:
	case rio.OpBind:
		return s.opBind(ios, msg)
	case rio.OpListen:
		return s.opListen(ios, msg)
	case rio.OpIoctl:
		return s.opIoctl(ios, msg)
	case rio.OpGetAddrInfo:
		return s.opGetAddrInfo(ios, msg)
	case rio.OpGetSockname:
		return s.opGetSockName(ios, msg)
	case rio.OpGetPeerName:
		return s.opGetPeerName(ios, msg)
	case rio.OpGetSockOpt:
		return s.opGetSockOpt(ios, msg)
	case rio.OpSetSockOpt:
		return s.opSetSockOpt(ios, msg)
	default:
		log.Printf("unknown socket op: %v", op)
		return mx.ErrNotSupported
	}
	return mx.ErrBadState
	// TODO do_halfclose
}
