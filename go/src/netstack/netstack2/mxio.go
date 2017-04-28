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
	"github.com/google/netstack/tcpip/header"
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
		dnsClient:  dns.NewClient(stk, 0),
		io:         make(map[cookie]*iostate),
		next:       1,
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

func (s *socketServer) setNetstack(ns *netstack) {
	s.ns = ns
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
	switch mxerror.Status(err) {
	case mx.ErrOk:
		// NOP
	case mx.ErrBadHandle, mx.ErrHandleClosed, mx.ErrRemoteClosed:
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
		case mx.ErrOk:
			// NOP
		case mx.ErrShouldWait:
			obs, err := dataHandle.WaitOne(MX_SOCKET_READABLE|MX_SOCKET_PEER_CLOSED, mx.TimensecInfinite)
			switch mxerror.Status(err) {
			case mx.ErrOk:
				// NOP
			case mx.ErrBadHandle, mx.ErrHandleClosed, mx.ErrRemoteClosed:
				return
			default:
				log.Printf("loopSocketWrite: wait failed: %v", err)
				return
			}
			switch {
			case obs&MX_SOCKET_PEER_CLOSED != 0:
				return
			case obs&MX_SOCKET_READABLE != 0:
				continue
			}
		case mx.ErrBadHandle, mx.ErrHandleClosed, mx.ErrRemoteClosed:
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
	switch mxerror.Status(err) {
	case mx.ErrOk:
		// NOP
	case mx.ErrBadHandle, mx.ErrHandleClosed, mx.ErrRemoteClosed:
		return
	default:
		log.Printf("loopSocketWrite: warmup failed: %v", err)
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
				switch mxerror.Status(err) {
				case mx.ErrOk:
				case mx.ErrBadHandle, mx.ErrHandleClosed, mx.ErrRemoteClosed:
				default:
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

	writeLoop:
		for len(v) > 0 {
			n, err := dataHandle.Write([]byte(v), 0)
			v = v[n:]
			switch mxerror.Status(err) {
			case mx.ErrOk:
				// NOP
			case mx.ErrShouldWait:
				if debug2 {
					log.Printf("loopSocketRead: gto mx.ErrShouldWait")
				}
				obs, err := dataHandle.WaitOne(
					MX_SOCKET_WRITABLE|MX_SOCKET_PEER_CLOSED,
					mx.TimensecInfinite,
				)
				switch mxerror.Status(err) {
				case mx.ErrOk:
					// NOP
				case mx.ErrBadHandle, mx.ErrHandleClosed, mx.ErrRemoteClosed:
					return
				default:
					log.Printf("loopSocketRead: wait failed: %v", err)
					return
				}
				switch {
				case obs&MX_SOCKET_PEER_CLOSED != 0:
					return
				case obs&MX_SOCKET_WRITABLE != 0:
					continue
				}
			case mx.ErrBadHandle, mx.ErrHandleClosed, mx.ErrRemoteClosed:
				return
			default:
				log.Printf("socket write failed: %v", err) // TODO: communicate this
				break writeLoop
			}
		}
	}
}

func (ios *iostate) loopShutdown() {
	defer ios.ep.Close()
	for {
		obs, err := ios.dataHandle.WaitOne(MX_SOCKET_PEER_CLOSED|MXSIO_SIGNAL_HALFCLOSED, mx.TimensecInfinite)
		switch mxerror.Status(err) {
		case mx.ErrOk:
			// NOP
		case mx.ErrBadHandle, mx.ErrHandleClosed, mx.ErrRemoteClosed:
			return
		default:
			log.Printf("shutdown wait failed: %v", err)
			return
		}
		switch {
		case obs&MX_SOCKET_PEER_CLOSED != 0:
			return
		case obs&MXSIO_SIGNAL_HALFCLOSED != 0:
			err := ios.ep.Shutdown(tcpip.ShutdownRead | tcpip.ShutdownWrite)
			if debug2 && err != nil {
				log.Printf("shutdown: %v", err) // typically ignored
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

	writeLoop:
		for {
			err = dataHandle.Write(out, nil, 0)
			switch mxerror.Status(err) {
			case mx.ErrBadHandle, mx.ErrHandleClosed, mx.ErrRemoteClosed:
				return
			case mx.ErrOk:
				break writeLoop
			default:
				log.Printf("socket write failed: %v", err) // TODO: communicate this
				break writeLoop
			}
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
		switch mxerror.Status(err) {
		case mx.ErrOk:
			// NOP
		case mx.ErrBadHandle, mx.ErrHandleClosed, mx.ErrRemoteClosed:
			return
		case mx.ErrShouldWait:
			obs, err := dataHandle.Handle.WaitOne(mx.SignalChannelReadable|mx.SignalChannelPeerClosed, mx.TimensecInfinite)
			switch mxerror.Status(err) {
			case mx.ErrBadHandle, mx.ErrHandleClosed, mx.ErrRemoteClosed:
				return
			case mx.ErrOk:
				switch {
				case obs&mx.SignalChannelPeerClosed != 0:
					return
				case obs&MX_SOCKET_READABLE != 0:
					continue
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

func (s *socketServer) newIostate(h mx.Handle, transProto tcpip.TransportProtocolNumber, wq *waiter.Queue, ep tcpip.Endpoint) (ios *iostate, reterr error) {
	ios = &iostate{
		transProto: transProto,
		wq:         wq,
		ep:         ep,
	}
	var peerS mx.Handle
	switch transProto {
	case tcp.ProtocolNumber:
		s0, s1, err := mx.NewSocket(0)
		if err != nil {
			return nil, err
		}
		ios.dataHandle = mx.Handle(s0)
		peerS = mx.Handle(s1)
	case udp.ProtocolNumber:
		c0, c1, err := mx.NewChannel(0)
		if err != nil {
			return nil, err
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

	// Before we add a dispatcher for this iostate, respond to the client describing what
	// kind of object this is.
	ro := rio.RioObject{
		RioObjectHeader: rio.RioObjectHeader{
			Status: errStatus(nil),
			Type:   uint32(mxio.ProtocolSocket),
		},
		Esize: 0,
	}
	if peerS != 0 {
		ro.Handle[0] = peerS
		ro.Hcount = 1
	}
	ro.Write(h, 0)

	if err := s.dispatcher.AddHandler(h, rio.ServerHandler(s.mxioHandler), int64(ios.cookie)); err != nil {
		s.mu.Lock()
		delete(s.io, ios.cookie)
		s.mu.Unlock()
		h.Close()
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

	return ios, nil
}

type socketServer struct {
	dispatcher *dispatcher.Dispatcher
	stack      tcpip.Stack
	dnsClient  *dns.Client
	ns         *netstack

	mu   sync.Mutex
	next cookie
	io   map[cookie]*iostate
}

func (s *socketServer) opSocket(h mx.Handle, ios *iostate, msg *rio.Msg, path string) (err error) {
	var domain, typ, protocol int
	if n, _ := fmt.Sscanf(path, "socket/%d/%d/%d\x00", &domain, &typ, &protocol); n != 3 {
		return mxerror.Errorf(mx.ErrInvalidArgs, "socket: bad path %q (n=%d)", path, n)
	}

	var n tcpip.NetworkProtocolNumber
	switch domain {
	case AF_INET:
		n = ipv4.ProtocolNumber
	case AF_INET6:
		n = ipv6.ProtocolNumber
	default:
		return mxerror.Errorf(mx.ErrNotSupported, "socket: unknown network protocol: %d", domain)
	}

	transProto, err := sockProto(typ, protocol)
	if err != nil {
		return err
	}

	wq := new(waiter.Queue)
	ep, err := s.stack.NewEndpoint(transProto, n, wq)
	if err != nil {
		if debug {
			log.Printf("socket: new endpoint: %v", err)
		}
		return err
	}
	if n == ipv6.ProtocolNumber {
		if err := ep.SetSockOpt(tcpip.V6OnlyOption(0)); err != nil {
			log.Printf("socket: setsockopt v6only option failed: %v", err)
		}
	}
	ios, err = s.newIostate(h, transProto, wq, ep)
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

func (s *socketServer) opAccept(h mx.Handle, ios *iostate, msg *rio.Msg, path string) (err error) {
	if ios.ep == nil {
		return mxerror.Errorf(mx.ErrBadState, "accept: no socket")
	}
	newep, newwq, err := ios.ep.Accept()
	if err == tcpip.ErrWouldBlock {
		return errShouldWait
	}
	if ios.ep.Readiness(waiter.EventIn) == 0 {
		// If we just accepted the only queued incoming connection,
		// clear the signal so the mxio client knows no incoming
		// connection is available.
		err := mx.Handle(ios.dataHandle).SignalPeer(MXSIO_SIGNAL_INCOMING, 0)
		switch mxerror.Status(err) {
		case mx.ErrOk:
			// NOP
		case mx.ErrBadHandle, mx.ErrHandleClosed, mx.ErrRemoteClosed:
			// Ignore the closure of the origin endpoint here,
			// as we have accepted a new endpoint and it can be
			// valid.
		default:
			log.Printf("accept: clearing MXSIO_SIGNAL_INCOMING: %v", err)
		}
	}
	if err != nil {
		if debug {
			log.Printf("accept: %v", err)
		}
		return err
	}

	newios, err := s.newIostate(h, ios.transProto, newwq, newep)
	go newios.loopRead(s.stack)
	return err
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
		err := ios.ep.GetSockOpt(opt)
		switch o := opt.(type) {
		case tcpip.ErrorOption:
			errno := uint32(0)
			if err != nil {
				errno = uint32(errStatus(err)) // TODO: convert from err?
			}
			binary.LittleEndian.PutUint32(val.optval[:], errno)
			val.optlen = c_socklen(4)
		case *tcpip.SendBufferSizeOption:
			binary.LittleEndian.PutUint32(val.optval[:], uint32(*o))
			val.optlen = c_socklen(4)
		case *tcpip.ReceiveBufferSizeOption:
			binary.LittleEndian.PutUint32(val.optval[:], uint32(*o))
			val.optlen = c_socklen(4)
		case *tcpip.ReceiveQueueSizeOption:
			binary.LittleEndian.PutUint32(val.optval[:], uint32(*o))
			val.optlen = c_socklen(4)
		case *tcpip.NoDelayOption:
			binary.LittleEndian.PutUint32(val.optval[:], uint32(*o))
			val.optlen = c_socklen(4)
		case *tcpip.ReuseAddressOption:
			binary.LittleEndian.PutUint32(val.optval[:], uint32(*o))
			val.optlen = c_socklen(4)
		case *tcpip.V6OnlyOption:
			binary.LittleEndian.PutUint32(val.optval[:], uint32(*o))
			val.optlen = c_socklen(4)
		default:
			binary.LittleEndian.PutUint32(val.optval[:], 0)
			val.optlen = c_socklen(4)
		}
	} else {
		val.optlen = 0
	}
	val.Encode(msg)
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
		log.Printf("opIoctl op=0x%x, datalen=%d", msg.Op(), msg.Datalen)
	}

	switch msg.Op() {
	case IOCTL_NETC_GET_IF_INFO:
		rep := c_netc_get_if_info{}

		s.ns.mu.Lock()
		defer s.ns.mu.Unlock()
		index := uint32(0)
		for nicid, netif := range s.ns.netifs {
			if netif.addr == header.IPv4Loopback {
				continue
			}
			rep.info[index].index = uint16(index)
			rep.info[index].flags |= NETC_IFF_UP
			copy(rep.info[index].name[:], []byte(fmt.Sprintf("en%d", nicid)))
			writeSockaddrStorage(&rep.info[index].addr, tcpip.FullAddress{NIC: nicid, Addr: netif.addr})
			writeSockaddrStorage(&rep.info[index].netmask, tcpip.FullAddress{NIC: nicid, Addr: tcpip.Address(netif.netmask)})

			// Long-hand for: broadaddr = netif.addr | ^netif.netmask
			broadaddr := []byte(netif.addr)
			for i := range broadaddr {
				broadaddr[i] |= ^netif.netmask[i]
			}
			writeSockaddrStorage(&rep.info[index].broadaddr, tcpip.FullAddress{NIC: nicid, Addr: tcpip.Address(broadaddr)})
			index++
		}
		rep.n_info = index
		rep.Encode(msg)
		return mx.ErrOk
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
			err := mx.Handle(ios.dataHandle).SignalPeer(0, MXSIO_SIGNAL_INCOMING)
			switch mxerror.Status(err) {
			case mx.ErrOk:
				continue
			case mx.ErrBadHandle, mx.ErrHandleClosed, mx.ErrRemoteClosed:
				return
			default:
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
		err := mx.Handle(ios.dataHandle).SignalPeer(0, MXSIO_SIGNAL_CONNECTED)
		switch status := mxerror.Status(err); status {
		case mx.ErrOk:
			// NOP
		case mx.ErrBadHandle, mx.ErrHandleClosed, mx.ErrRemoteClosed:
			return status
		default:
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

	if debug2 {
		log.Printf("getaddrinfo node=%q, service=%q", node, service)
	}

	s.mu.Lock()
	dnsClient := s.dnsClient
	s.mu.Unlock()

	if dnsClient == nil {
		log.Println("getaddrinfo called, but no DNS client available.")
		rep := c_mxrio_gai_reply{retval: EAI_FAIL}
		rep.Encode(msg)
		return mx.ErrOk
	}

	if hints.ai_socktype == 0 {
		hints.ai_socktype = SOCK_STREAM
	}
	if hints.ai_protocol == 0 {
		if hints.ai_socktype == SOCK_STREAM {
			hints.ai_protocol = IPPROTO_TCP
		} else if hints.ai_socktype == SOCK_DGRAM {
			hints.ai_protocol = IPPROTO_UDP
		}
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

	if val.node_is_null == 1 {
		rep := c_mxrio_gai_reply{}
		rep.res[0].ai.ai_family = AF_INET
		rep.res[0].ai.ai_addrlen = c_socklen(c_sockaddr_in_len)
		sockaddr := c_sockaddr_in{sin_family: AF_INET}
		sockaddr.sin_port.setPort(port)
		writeSockaddrStorage4(&rep.res[0].addr, &sockaddr)
		rep.nres = 1
		rep.Encode(msg)
		return mx.ErrOk
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
	case 0:
		rep := c_mxrio_gai_reply{retval: EAI_NONAME}
		rep.Encode(msg)
		return mx.ErrOk
	case 4:
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
		switch {
		case strings.HasPrefix(path, "none"): // MXRIO_SOCKET_DIR_NONE
			_, err = s.newIostate(msg.Handle[0], tcp.ProtocolNumber, nil, nil)
		case strings.HasPrefix(path, "socket/"): // MXRIO_SOCKET_DIR_SOCKET
			err = s.opSocket(msg.Handle[0], ios, msg, path)
		case strings.HasPrefix(path, "accept"): // MXRIO_SOCKET_DIR_ACCEPT
			err = s.opAccept(msg.Handle[0], ios, msg, path)
		default:
			if debug2 {
				log.Printf("open: unknown path=%q", path)
			}
			err = mxerror.Errorf(mx.ErrNotSupported, "open: unknown path=%q", path)
		}

		if err != nil {
			ro := rio.RioObject{
				RioObjectHeader: rio.RioObjectHeader{
					Status: errStatus(err),
					Type:   uint32(mxio.ProtocolSocket),
				},
				Esize: 0,
			}
			ro.Write(msg.Handle[0], 0)
			msg.Handle[0].Close()
		}
		return dispatcher.ErrIndirect.Status

	case rio.OpConnect:
		return s.opConnect(ios, msg) // do_connect
	case rio.OpClose:
		if ios.ep != nil {
			ios.dataHandle.Close() // loopShutdown closes ep
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
