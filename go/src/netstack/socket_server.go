// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"log"
	"syscall/zx"
	"syscall/zx/fdio"
	"syscall/zx/mxerror"
	"syscall/zx/zxsocket"
	"syscall/zx/zxwait"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/header"
	"github.com/google/netstack/tcpip/network/ipv4"
	"github.com/google/netstack/tcpip/network/ipv6"
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

const (
	ZXSIO_SIGNAL_INCOMING  = zx.SignalUser0
	ZXSIO_SIGNAL_OUTGOING  = zx.SignalUser1
	ZXSIO_SIGNAL_CONNECTED = zx.SignalUser3
	LOCAL_SIGNAL_CLOSING   = zx.SignalUser5
)

type iostate struct {
	wq *waiter.Queue
	ep tcpip.Endpoint

	ns *Netstack

	netProto   tcpip.NetworkProtocolNumber   // IPv4 or IPv6
	transProto tcpip.TransportProtocolNumber // TCP or UDP

	dataHandle zx.Socket // used to communicate with libc

	loopWriteDone  chan struct{} // report that loopWrite finished
	loopListenDone chan struct{} // report that loopListen finished

	closing chan struct{}
}

// loopWrite connects libc write to the network stack.
func (ios *iostate) loopWrite() error {
	const sigs = zx.SignalSocketReadable | zx.SignalSocketReadDisabled |
		zx.SignalSocketPeerClosed | LOCAL_SIGNAL_CLOSING

	waitEntry, notifyCh := waiter.NewChannelEntry(nil)
	defer ios.wq.EventUnregister(&waitEntry)
	for {
		// TODO: obviously allocating for each read is silly.
		// A quick hack we can do is store these in a ring buffer,
		// as the lifecycle of this buffer.View starts here, and
		// ends in nearby code we control in link.go.
		v := make([]byte, 0, 2048)
		switch n, err := ios.dataHandle.Read(v[:cap(v)], 0); mxerror.Status(err) {
		case zx.ErrOk:
			// Success. Pass the data to the endpoint and loop.
			v = v[:n]
		case zx.ErrBadState:
			// This side of the socket is closed.
			if err := ios.ep.Shutdown(tcpip.ShutdownWrite); err != nil && err != tcpip.ErrNotConnected {
				return fmt.Errorf("Endpoint.Shutdown(ShutdownWrite): %s", err)
			}
			return nil
		case zx.ErrShouldWait:
			switch obs, err := zxwait.Wait(zx.Handle(ios.dataHandle), sigs, zx.TimensecInfinite); mxerror.Status(err) {
			case zx.ErrOk:
				switch {
				case obs&zx.SignalSocketReadDisabled != 0:
				// The next Read will return zx.BadState.
				case obs&zx.SignalSocketReadable != 0:
					// The client might have written some data into the socket.
					// Always continue to the 'for' loop below and try to read them
					// even if the signals show the client has closed the dataHandle.
					continue
				case obs&zx.SignalSocketPeerClosed != 0:
					return nil
				case obs&LOCAL_SIGNAL_CLOSING != 0:
					return nil
				}
			case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
				return nil
			default:
				return err
			}
		case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
			return nil
		default:
			return err
		}

		if debug {
			log.Printf("%p: loopWrite: sending packet n=%d, v=%q", ios, len(v), v)
		}

		var opts tcpip.WriteOptions
		if ios.transProto != tcp.ProtocolNumber {
			var fdioSocketMsg C.struct_fdio_socket_msg
			if err := fdioSocketMsg.Unmarshal(v[:C.FDIO_SOCKET_MSG_HEADER_SIZE]); err != nil {
				return err
			}
			if fdioSocketMsg.addrlen != 0 {
				addr, err := fdioSocketMsg.addr.Decode()
				if err != nil {
					return err
				}
				opts.To = &addr
			}
			v = v[C.FDIO_SOCKET_MSG_HEADER_SIZE:]
		}

		ios.wq.EventRegister(&waitEntry, waiter.EventOut)
		for {
			n, resCh, err := ios.ep.Write(tcpip.SlicePayload(v), opts)
			if resCh != nil {
				if err != tcpip.ErrNoLinkAddress {
					panic(fmt.Sprintf("err=%v inconsistent with presence of resCh", err))
				}
				if ios.transProto == tcp.ProtocolNumber {
					panic(fmt.Sprintf("TCP link address resolutions happen on connect; saw %d/%d", n, len(v)))
				}
				<-resCh
				continue
			}
			if err == tcpip.ErrWouldBlock {
				switch ios.transProto {
				case tcp.ProtocolNumber:
				default:
					panic(fmt.Sprintf("UDP writes are nonblocking; saw %d/%d", n, len(v)))
				}
				// Note that Close should not interrupt this wait.
				<-notifyCh
				continue
			}
			if err != nil {
				optsStr := "..."
				if to := opts.To; to != nil {
					optsStr = fmt.Sprintf("%+v", *to)
				}
				return fmt.Errorf("Endpoint.Write(%s): %s", optsStr, err)
			}
			if ios.transProto != tcp.ProtocolNumber {
				if int(n) < len(v) {
					panic(fmt.Sprintf("UDP disallows short writes; saw: %d/%d", n, len(v)))
				}
			}
			v = v[n:]
			if len(v) == 0 {
				break
			}
		}
		ios.wq.EventUnregister(&waitEntry)
	}
}

// loopRead connects libc read to the network stack.
func (ios *iostate) loopRead() error {
	const sigs = zx.SignalSocketWritable | zx.SignalSocketWriteDisabled |
		zx.SignalSocketPeerClosed | LOCAL_SIGNAL_CLOSING

	inEntry, inCh := waiter.NewChannelEntry(nil)
	defer ios.wq.EventUnregister(&inEntry)

	outEntry, outCh := waiter.NewChannelEntry(nil)
	connected := ios.transProto != tcp.ProtocolNumber
	if !connected {
		ios.wq.EventRegister(&outEntry, waiter.EventOut)
		defer ios.wq.EventUnregister(&outEntry)
	}

	var sender tcpip.FullAddress
	for {
		var v []byte

		ios.wq.EventRegister(&inEntry, waiter.EventIn)
		for {
			var err *tcpip.Error
			v, _, err = ios.ep.Read(&sender)
			if err == tcpip.ErrClosedForReceive {
				return ios.dataHandle.Shutdown(zx.SocketShutdownWrite)
			}
			var notifyCh <-chan struct{}
			if err == tcpip.ErrInvalidEndpointState {
				if connected {
					panic(fmt.Sprintf("connected endpoint returned %s", err))
				}
				notifyCh = outCh
			} else if !connected {
				var signals zx.Signals = ZXSIO_SIGNAL_OUTGOING
				switch err {
				case nil, tcpip.ErrWouldBlock:
					connected = true
					ios.wq.EventUnregister(&outEntry)

					signals |= ZXSIO_SIGNAL_CONNECTED
				default:
					notifyCh = outCh
				}

				switch err := ios.dataHandle.Handle().SignalPeer(0, signals); mxerror.Status(err) {
				case zx.ErrOk, zx.ErrBadHandle:
				default:
					log.Printf("%p: SignalPeer(0, %b): %s", ios, signals, err)
				}
			}
			switch err {
			case tcpip.ErrWouldBlock:
				notifyCh = inCh
			}
			if notifyCh != nil {
				select {
				case <-notifyCh:
					continue
				case <-ios.closing:
					// TODO: write a unit test that exercises this.
					return nil
				}
			}
			if err != nil {
				return fmt.Errorf("Endpoint.Read(): %s", err)
			}
			break
		}
		ios.wq.EventUnregister(&inEntry)
		if debug {
			log.Printf("%p: loopRead: received packet n=%d, v=%q", ios, len(v), v)
		}

		if ios.transProto != tcp.ProtocolNumber {
			out := make([]byte, C.FDIO_SOCKET_MSG_HEADER_SIZE+len(v))
			if err := func() error {
				var fdioSocketMsg C.struct_fdio_socket_msg
				fdioSocketMsg.addrlen = C.socklen_t(fdioSocketMsg.addr.Encode(ios.netProto, sender))
				if _, err := fdioSocketMsg.MarshalTo(out[:C.FDIO_SOCKET_MSG_HEADER_SIZE]); err != nil {
					return err
				}
				return nil
			}(); err != nil {
				return err
			}
			if n := copy(out[C.FDIO_SOCKET_MSG_HEADER_SIZE:], v); n < len(v) {
				panic(fmt.Sprintf("copied %d/%d bytes", n, len(v)))
			}
			v = out
		}

	writeLoop:
		for {
			switch n, err := ios.dataHandle.Write(v, 0); mxerror.Status(err) {
			case zx.ErrOk:
				if ios.transProto != tcp.ProtocolNumber {
					if n < len(v) {
						panic(fmt.Sprintf("UDP disallows short writes; saw: %d/%d", n, len(v)))
					}
				}
				v = v[n:]
				if len(v) == 0 {
					break writeLoop
				}
			case zx.ErrBadState:
				// This side of the socket is closed.
				if err := ios.ep.Shutdown(tcpip.ShutdownRead); err != nil {
					return fmt.Errorf("Endpoint.Shutdown(ShutdownRead): %s", err)
				}
				return nil
			case zx.ErrShouldWait:
				switch obs, err := zxwait.Wait(zx.Handle(ios.dataHandle), sigs, zx.TimensecInfinite); mxerror.Status(err) {
				case zx.ErrOk:
					switch {
					case obs&zx.SignalSocketWriteDisabled != 0:
					// The next Write will return zx.BadState.
					case obs&zx.SignalSocketWritable != 0:
						continue
					case obs&zx.SignalSocketPeerClosed != 0:
						return nil
					case obs&LOCAL_SIGNAL_CLOSING != 0:
						return nil
					}
				case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
					return nil
				default:
					return err
				}
			case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
				return nil
			default:
				return err
			}
		}
	}
}

func (ios *iostate) loopControl() {
	synthesizeClose := true
	defer func() {
		if synthesizeClose {
			switch err := zxsocket.Handler(0, zxsocket.ServerHandler(ios.zxsocketHandler), 0); mxerror.Status(err) {
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
		switch err := zxsocket.Handler(ios.dataHandle, zxsocket.ServerHandler(ios.zxsocketHandler), 0); mxerror.Status(err) {
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

func newIostate(ns *Netstack, netProto tcpip.NetworkProtocolNumber, transProto tcpip.TransportProtocolNumber, wq *waiter.Queue, ep tcpip.Endpoint, isAccept bool) (zx.Socket, error) {
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
		return zx.Socket(zx.HandleInvalid), err
	}
	ios := &iostate{
		netProto:      netProto,
		transProto:    transProto,
		wq:            wq,
		ep:            ep,
		ns:            ns,
		dataHandle:    localS,
		loopWriteDone: make(chan struct{}),
		closing:       make(chan struct{}),
	}

	go ios.loopControl()
	go func() {
		if err := ios.loopRead(); err != nil {
			log.Printf("%p: loopRead: %s", ios, err)
		}
	}()
	go func() {
		defer close(ios.loopWriteDone)

		if err := ios.loopWrite(); err != nil {
			log.Printf("%p: loopWrite: %s", ios, err)
		}
	}()

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

func (ios *iostate) opGetSockOpt(msg *zxsocket.Msg) zx.Status {
	var reqReply C.struct_zxrio_sockopt_req_reply
	if err := reqReply.Unmarshal(msg.Data[:msg.Datalen]); err != nil {
		if debug {
			log.Printf("getsockopt: decode argument: %v", err)
		}
		return errStatus(err)
	}
	val, err := GetSockOpt(ios.ep, ios.transProto, int16(reqReply.level), int16(reqReply.optname))
	if err != nil {
		return zxNetError(err)
	}
	buf := bytes.NewBuffer(reqReply.opt()[:0])
	if err := binary.Write(buf, binary.LittleEndian, val); err != nil {
		panic(err)
	}
	b := buf.Bytes()
	n := copy(reqReply.opt(), b)
	if _, ok := val.(C.struct_tcp_info); ok {
		// TODO(tamird): why are we encoding 144 bytes into a 128 byte buffer?
		n += 16
	}
	if n < len(b) {
		panic(fmt.Sprintf("short %T: %d/%d", val, n, len(b)))
	} else {
		reqReply.optlen = C.socklen_t(n)
	}
	{
		n, err := reqReply.MarshalTo(msg.Data[:])
		if err != nil {
			return errStatus(err)
		}
		msg.Datalen = uint32(n)
	}
	return zx.ErrOk
}

func (ios *iostate) opSetSockOpt(msg *zxsocket.Msg) zx.Status {
	var reqReply C.struct_zxrio_sockopt_req_reply
	if err := reqReply.Unmarshal(msg.Data[:msg.Datalen]); err != nil {
		if debug {
			log.Printf("setsockopt: decode argument: %v", err)
		}
		return errStatus(err)
	}
	if err := SetSockOpt(ios.ep, int16(reqReply.level), int16(reqReply.optname), reqReply.opt()[:reqReply.optlen]); err != nil {
		return zxNetError(err)
	}
	msg.Datalen = 0
	msg.SetOff(0)
	return zx.ErrOk
}

func (ios *iostate) opBind(msg *zxsocket.Msg) (status zx.Status) {
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

func (ios *iostate) buildIfInfos() *C.netc_get_if_info_t {
	rep := &C.netc_get_if_info_t{}

	ios.ns.mu.Lock()
	defer ios.ns.mu.Unlock()
	var index C.uint
	for nicid, ifs := range ios.ns.ifStates {
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
		rep.info[index].addr.Encode(ipv4.ProtocolNumber, tcpip.FullAddress{NIC: nicid, Addr: ifs.nic.Addr})
		rep.info[index].netmask.Encode(ipv4.ProtocolNumber, tcpip.FullAddress{NIC: nicid, Addr: tcpip.Address(ifs.nic.Netmask)})

		// Long-hand for: broadaddr = ifs.nic.Addr | ^ifs.nic.Netmask
		broadaddr := []byte(ifs.nic.Addr)
		for i := range broadaddr {
			broadaddr[i] |= ^ifs.nic.Netmask[i]
		}
		rep.info[index].broadaddr.Encode(ipv4.ProtocolNumber, tcpip.FullAddress{NIC: nicid, Addr: tcpip.Address(broadaddr)})
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

func (ios *iostate) opIoctl(msg *zxsocket.Msg) zx.Status {
	switch msg.IoctlOp() {
	// TODO(ZX-766): remove when dart/runtime/bin/socket_base_fuchsia.cc uses getifaddrs().
	case ioctlNetcGetNumIfs:
		lastIfInfo = ios.buildIfInfos()
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
		nodename := ios.ns.getNodeName()
		msg.Datalen = uint32(copy(msg.Data[:msg.Arg], nodename))
		msg.Data[msg.Datalen] = 0
		return zx.ErrOk
	}

	if debug {
		log.Printf("opIoctl op=0x%x, datalen=%d", msg.Op(), msg.Datalen)
	}

	return zx.ErrInvalidArgs
}

func fdioSockAddrReply(netProto tcpip.NetworkProtocolNumber, addr tcpip.FullAddress, msg *zxsocket.Msg) zx.Status {
	var rep C.struct_zxrio_sockaddr_reply
	rep.len = C.socklen_t(rep.addr.Encode(netProto, addr))
	n, err := rep.MarshalTo(msg.Data[:])
	if err != nil {
		return errStatus(err)
	}
	msg.Datalen = uint32(n)
	msg.SetOff(0)
	return zx.ErrOk
}

func (ios *iostate) opGetSockName(msg *zxsocket.Msg) zx.Status {
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
	return fdioSockAddrReply(ios.netProto, a, msg)
}

func (ios *iostate) opGetPeerName(msg *zxsocket.Msg) (status zx.Status) {
	a, err := ios.ep.GetRemoteAddress()
	if err != nil {
		return zxNetError(err)
	}
	return fdioSockAddrReply(ios.netProto, a, msg)
}

func (ios *iostate) loopListen(inCh chan struct{}) error {
	// When an incoming connection is available, wait for the listening socket to
	// enter a shareable state, then share it with the client.
	for {
		select {
		case <-inCh:
			// NOP
		case <-ios.closing:
			return nil
		}
		// We got incoming connections.
		// Note that we don't know how many connections pending (the waiter channel won't
		// queue more than one notification) so we'll need to call Accept repeatedly until
		// it returns tcpip.ErrWouldBlock.
		for {
			switch obs, err := zxwait.Wait(
				zx.Handle(ios.dataHandle),
				zx.SignalSocketShare|zx.SignalSocketPeerClosed|LOCAL_SIGNAL_CLOSING,
				zx.TimensecInfinite,
			); mxerror.Status(err) {
			case zx.ErrOk:
				switch {
				case obs&zx.SignalSocketShare != 0:
					// NOP
				case obs&LOCAL_SIGNAL_CLOSING != 0:
					return nil
				case obs&zx.SignalSocketPeerClosed != 0:
					return nil
				}
			case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
				return nil
			default:
				return err
			}

			ep, wq, err := ios.ep.Accept()
			if err == tcpip.ErrWouldBlock {
				// No more pending connections.
				break
			}
			if err != nil {
				return fmt.Errorf("Endpoint.Accept(): %s", err)
			}

			if logAccept {
				localAddr, err := ep.GetLocalAddress()
				if err != nil {
					panic(err)
				}
				remoteAddr, err := ep.GetRemoteAddress()
				if err != nil {
					panic(err)
				}
				log.Printf("%p: TCP accept: local=%+v, remote=%+v", ios, localAddr, remoteAddr)
			}

			{
				peerS, err := newIostate(ios.ns, ios.netProto, ios.transProto, wq, ep, true)
				if err != nil {
					return err
				}

				if err := ios.dataHandle.Share(zx.Handle(peerS)); err != nil {
					return err
				}
			}
		}
	}
}

func (ios *iostate) opListen(msg *zxsocket.Msg) (status zx.Status) {
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
		if err := ios.loopListen(inCh); err != nil {
			log.Printf("%p: loopListen: %s", ios, err)
		}
		ios.wq.EventUnregister(&inEntry)
	}()

	msg.Datalen = 0
	msg.SetOff(0)
	return zx.ErrOk
}

func (ios *iostate) opConnect(msg *zxsocket.Msg) (status zx.Status) {
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

	if len(addr.Addr) == 0 {
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

	msg.SetOff(0)
	msg.Datalen = 0

	if err := ios.ep.Connect(addr); err != nil {
		if err == tcpip.ErrConnectStarted {
			switch err := ios.dataHandle.Handle().SignalPeer(ZXSIO_SIGNAL_OUTGOING, 0); mxerror.Status(err) {
			case zx.ErrOk, zx.ErrBadHandle:
			default:
				log.Printf("%p: SignalPeer(%b, 0): %s", ios, ZXSIO_SIGNAL_OUTGOING, err)
			}
		}
		if debug {
			log.Printf("connect: addr=%v, %s", addr, err)
		}
		return zxNetError(err)
	}
	if debug {
		log.Printf("connect: connected")
	}
	return zx.ErrOk
}

func (ios *iostate) opClose(cookie int64) zx.Status {
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

func (ios *iostate) zxsocketHandler(msg *zxsocket.Msg, rh zx.Socket, cookie int64) zx.Status {
	op := msg.Op()
	if debug {
		log.Printf("zxsocketHandler: op=%v, len=%d, arg=%v, hcount=%d", op, msg.Datalen, msg.Arg, msg.Hcount)
	}

	switch op {
	case zxsocket.OpConnect:
		return ios.opConnect(msg)
	case zxsocket.OpClose:
		return ios.opClose(cookie)
	case zxsocket.OpBind:
		return ios.opBind(msg)
	case zxsocket.OpListen:
		return ios.opListen(msg)
	case zxsocket.OpIoctl:
		return ios.opIoctl(msg)
	case zxsocket.OpGetSockname:
		return ios.opGetSockName(msg)
	case zxsocket.OpGetPeerName:
		return ios.opGetPeerName(msg)
	case zxsocket.OpGetSockOpt:
		return ios.opGetSockOpt(msg)
	case zxsocket.OpSetSockOpt:
		return ios.opSetSockOpt(msg)
	default:
		log.Printf("zxsocketHandler: unknown socket op: %v", op)
		return zx.ErrNotSupported
	}
	// TODO do_halfclose
}
