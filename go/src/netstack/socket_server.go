// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"encoding/binary"
	"fmt"
	"log"
	"reflect"
	"runtime"
	"sync"
	"syscall/zx"
	"syscall/zx/fdio"
	"syscall/zx/fidl"
	"syscall/zx/mxerror"
	"syscall/zx/zxsocket"
	"syscall/zx/zxwait"

	"fidl/fuchsia/net"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/header"
	"github.com/google/netstack/tcpip/network/ipv4"
	"github.com/google/netstack/tcpip/network/ipv6"
	"github.com/google/netstack/tcpip/transport/tcp"
	"github.com/google/netstack/tcpip/transport/udp"
	"github.com/google/netstack/waiter"
)

// #cgo CFLAGS: -D_GNU_SOURCE
// #cgo CFLAGS: -I${SRCDIR}/../../../../zircon/system/ulib/zxs/include
// #cgo CFLAGS: -I${SRCDIR}/../../../../zircon/third_party/ulib/musl/include
// #cgo CFLAGS: -I${SRCDIR}/../../../public
// #include <errno.h>
// #include <fcntl.h>
// #include <lib/netstack/c/netconfig.h>
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

	dataHandle         zx.Socket // used to communicate with libc
	incomingAssertedMu sync.Mutex

	loopWriteDone chan struct{} // report that loopWrite finished

	closing chan struct{}
}

type legacyIostate struct {
	*iostate

	loopListenDone chan struct{} // report that loopListen finished
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
				if ios.transProto != tcp.ProtocolNumber {
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
func (ios *legacyIostate) loopRead() error {
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
				case zx.ErrOk, zx.ErrPeerClosed:
				default:
					panic(err)
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

		if ios.transProto != tcp.ProtocolNumber {
			out := make([]byte, C.FDIO_SOCKET_MSG_HEADER_SIZE+len(v))
			var fdioSocketMsg C.struct_fdio_socket_msg
			fdioSocketMsg.addrlen = C.socklen_t(fdioSocketMsg.addr.Encode(ios.netProto, sender))
			if _, err := fdioSocketMsg.MarshalTo(out[:C.FDIO_SOCKET_MSG_HEADER_SIZE]); err != nil {
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

func (ios *legacyIostate) loopControl() {
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
			if err == tcpip.ErrInvalidEndpointState {
				if connected {
					panic(fmt.Sprintf("connected endpoint returned %s", err))
				}
				select {
				case <-ios.closing:
					return nil
				case <-inCh:
					// We got an incoming connection; we must be a listening socket.
					ios.wq.EventUnregister(&outEntry)

					ios.incomingAssertedMu.Lock()
					switch err := ios.dataHandle.Handle().SignalPeer(0, ZXSIO_SIGNAL_INCOMING); mxerror.Status(err) {
					case zx.ErrOk, zx.ErrPeerClosed:
					default:
						panic(err)
					}
					ios.incomingAssertedMu.Unlock()
					continue
				case <-outCh:
					// We became connected; the next Read will reflect this.
					continue
				}
			} else if !connected {
				var signals zx.Signals = ZXSIO_SIGNAL_OUTGOING
				switch err {
				case nil, tcpip.ErrWouldBlock:
					connected = true
					ios.wq.EventUnregister(&outEntry)

					signals |= ZXSIO_SIGNAL_CONNECTED
				}

				switch err := ios.dataHandle.Handle().SignalPeer(0, signals); mxerror.Status(err) {
				case zx.ErrOk, zx.ErrBadHandle:
				default:
					panic(err)
				}
			}
			switch err {
			case nil:
			case tcpip.ErrConnectionRefused:
				// Linux allows sockets with connection errors to be reused. If the
				// client calls connect() again (and the underlying Endpoint correctly
				// permits the attempt), we need to wait for an outbound event again.
				select {
				case <-outCh:
					continue
				case <-ios.closing:
					return nil
				}
			case tcpip.ErrWouldBlock:
				select {
				case <-inCh:
					continue
				case <-ios.closing:
					return nil
				}
			default:
				return fmt.Errorf("Endpoint.Read(): %s", err)
			}
			break
		}
		ios.wq.EventUnregister(&inEntry)

		if ios.transProto != tcp.ProtocolNumber {
			out := make([]byte, C.FDIO_SOCKET_MSG_HEADER_SIZE+len(v))
			var fdioSocketMsg C.struct_fdio_socket_msg
			fdioSocketMsg.addrlen = C.socklen_t(fdioSocketMsg.addr.Encode(ios.netProto, sender))
			if _, err := fdioSocketMsg.MarshalTo(out[:C.FDIO_SOCKET_MSG_HEADER_SIZE]); err != nil {
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

func (ios *iostate) loopControl() error {
	synthesizeClose := true
	defer func() {
		if synthesizeClose {
			if code, err := ios.Close(); err != nil {
				log.Printf("synethsize close failed: %v", err)
			} else if code != 0 {
				log.Printf("synethsize close failed: %d", code)

				if err := ios.dataHandle.Close(); err != nil {
					log.Printf("dataHandle.Close() failed: %v", err)
				}
			}
		}
	}()

	stub := net.SocketControlStub{Impl: ios}
	var respb [zx.ChannelMaxMessageBytes]byte
	for {
		switch err := func() error {
			nb, err := ios.dataHandle.Read(respb[:], zx.SocketControl)
			if err != nil {
				return err
			}

			msg := respb[:nb]
			var header fidl.MessageHeader
			if err := fidl.UnmarshalHeader(msg, &header); err != nil {
				return err
			}

			p, err := stub.Dispatch(header.Ordinal, msg[fidl.MessageHeaderSize:], nil)
			if err != nil {
				return err
			}
			cnb, _, err := fidl.MarshalMessage(&header, p, respb[:], nil)
			if err != nil {
				return err
			}
			respb := respb[:cnb]
			for len(respb) > 0 {
				if n, err := ios.dataHandle.Write(respb, zx.SocketControl); err != nil {
					return err
				} else {
					respb = respb[n:]
				}
			}
			return nil
		}(); mxerror.Status(err) {
		case zx.ErrOk:
		case zx.ErrBadState:
			return nil // This side of the socket is closed.
		case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
			return nil
		case zx.ErrShouldWait:
			obs, err := zxwait.Wait(zx.Handle(ios.dataHandle),
				zx.SignalSocketControlReadable|zx.SignalSocketPeerClosed|LOCAL_SIGNAL_CLOSING,
				zx.TimensecInfinite)
			switch mxerror.Status(err) {
			case zx.ErrBadHandle, zx.ErrCanceled, zx.ErrPeerClosed:
				return nil
			case zx.ErrOk:
				switch {
				case obs&zx.SignalSocketControlReadable != 0:
					continue
				case obs&LOCAL_SIGNAL_CLOSING != 0:
					synthesizeClose = false
					return nil
				case obs&zx.SignalSocketPeerClosed != 0:
					return nil
				}
			default:
				return err
			}
		default:
			return err
		}
	}
}

func newIostate(ns *Netstack, netProto tcpip.NetworkProtocolNumber, transProto tcpip.TransportProtocolNumber, wq *waiter.Queue, ep tcpip.Endpoint, isAccept bool, legacy bool) zx.Socket {
	var t uint32 = zx.SocketDatagram
	if transProto == tcp.ProtocolNumber {
		t = zx.SocketStream
	}
	t |= zx.SocketHasControl
	if !isAccept {
		t |= zx.SocketHasAccept
	}
	localS, peerS, err := zx.NewSocket(t)
	if err != nil {
		panic(err)
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

	if legacy {
		ios := legacyIostate{
			iostate: ios,
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
	} else {
		go func() {
			if err := ios.loopControl(); err != nil {
				log.Printf("%p: loopControl: %s", ios, err)
			}
		}()
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
	}

	return peerS
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

func (ios *legacyIostate) opGetSockOpt(msg *zxsocket.Msg) zx.Status {
	var reqReply C.struct_zxrio_sockopt_req_reply
	if err := reqReply.Unmarshal(msg.Data[:msg.Datalen]); err != nil {
		if debug {
			log.Printf("getsockopt: decode argument: %v", err)
		}
		return errStatus(err)
	}
	val, err := GetSockOpt(ios.ep, ios.transProto, int16(reqReply.level), int16(reqReply.optname), true)
	if err != nil {
		return zxNetError(err)
	}
	n := copyAsBytes(reqReply.opt(), val)
	if _, ok := val.(C.struct_tcp_info); ok {
		// TODO(tamird): why are we encoding 144 bytes into a 128 byte buffer?
		n += 16
	}
	if size := reflect.TypeOf(val).Size(); n < int(size) {
		panic(fmt.Sprintf("short %T: %d/%d", val, n, size))
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

func (ios *legacyIostate) opSetSockOpt(msg *zxsocket.Msg) zx.Status {
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

func (ios *legacyIostate) opBind(msg *zxsocket.Msg) (status zx.Status) {
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
	for nicid, ifs := range ios.ns.mu.ifStates {
		ifs.mu.Lock()
		if ifs.mu.nic.Addr == ipv4Loopback {

			continue
		}
		name := ifs.mu.nic.Name
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
		rep.info[index].addr.Encode(ipv4.ProtocolNumber, tcpip.FullAddress{NIC: nicid, Addr: ifs.mu.nic.Addr})
		rep.info[index].netmask.Encode(ipv4.ProtocolNumber, tcpip.FullAddress{NIC: nicid, Addr: tcpip.Address(ifs.mu.nic.Netmask)})

		// Long-hand for: broadaddr = ifs.mu.nic.Addr | ^ifs.mu.nic.Netmask
		broadaddr := []byte(ifs.mu.nic.Addr)
		for i := range broadaddr {
			broadaddr[i] |= ^ifs.mu.nic.Netmask[i]
		}
		rep.info[index].broadaddr.Encode(ipv4.ProtocolNumber, tcpip.FullAddress{NIC: nicid, Addr: tcpip.Address(broadaddr)})
		ifs.mu.Unlock()

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

func (ios *legacyIostate) opIoctl(msg *zxsocket.Msg) zx.Status {
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

func (ios *legacyIostate) opGetSockName(msg *zxsocket.Msg) zx.Status {
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

func (ios *legacyIostate) opGetPeerName(msg *zxsocket.Msg) (status zx.Status) {
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

			if err := ios.dataHandle.Share(zx.Handle(newIostate(ios.ns, ios.netProto, ios.transProto, wq, ep, true, true))); err != nil {
				return err
			}
		}
	}
}

func (ios *legacyIostate) opListen(msg *zxsocket.Msg) (status zx.Status) {
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

func (ios *legacyIostate) opConnect(msg *zxsocket.Msg) (status zx.Status) {
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
			case zx.ErrOk, zx.ErrPeerClosed:
			default:
				panic(err)
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

func (ios *legacyIostate) opClose(cookie int64) zx.Status {
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

func (ios *legacyIostate) zxsocketHandler(msg *zxsocket.Msg, rh zx.Socket, cookie int64) zx.Status {
	op := msg.Op()
	if debug {
		log.Printf("zxsocketHandler: op=%x, len=%d, arg=%v, hcount=%d", op, msg.Datalen, msg.Arg, msg.Hcount)
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
		log.Printf("zxsocketHandler: unknown socket op: %x", op)
		return zx.ErrNotSupported
	}
	// TODO do_halfclose
}

var _ net.SocketControl = (*iostate)(nil)

func tcpipErrorToCode(err *tcpip.Error) int16 {
	if debug {
		errStr := err.String()
		if pc, _, _, ok := runtime.Caller(1); ok {
			errStr = runtime.FuncForPC(pc).Name() + ": " + errStr
		}
		if err := log.Output(2, err.String()); err != nil {
			panic(err)
		}
	}
	switch err {
	case tcpip.ErrUnknownProtocol:
		return C.EINVAL
	case tcpip.ErrUnknownNICID:
		return C.EINVAL
	case tcpip.ErrUnknownProtocolOption:
		return C.ENOPROTOOPT
	case tcpip.ErrDuplicateNICID:
		return C.EEXIST
	case tcpip.ErrDuplicateAddress:
		return C.EEXIST
	case tcpip.ErrNoRoute:
		return C.EHOSTUNREACH
	case tcpip.ErrBadLinkEndpoint:
		return C.EINVAL
	case tcpip.ErrAlreadyBound:
		return C.EINVAL
	case tcpip.ErrInvalidEndpointState:
		return C.EINVAL
	case tcpip.ErrAlreadyConnecting:
		return C.EALREADY
	case tcpip.ErrAlreadyConnected:
		return C.EISCONN
	case tcpip.ErrNoPortAvailable:
		return C.EAGAIN
	case tcpip.ErrPortInUse:
		return C.EADDRINUSE
	case tcpip.ErrBadLocalAddress:
		return C.EADDRNOTAVAIL
	case tcpip.ErrClosedForSend:
		return C.EPIPE
	case tcpip.ErrClosedForReceive:
		return C.EAGAIN
	case tcpip.ErrWouldBlock:
		return C.EWOULDBLOCK
	case tcpip.ErrConnectionRefused:
		return C.ECONNREFUSED
	case tcpip.ErrTimeout:
		return C.ETIMEDOUT
	case tcpip.ErrAborted:
		return C.EPIPE
	case tcpip.ErrConnectStarted:
		return C.EINPROGRESS
	case tcpip.ErrDestinationRequired:
		return C.EDESTADDRREQ
	case tcpip.ErrNotSupported:
		return C.EOPNOTSUPP
	case tcpip.ErrQueueSizeNotSupported:
		return C.ENOTTY
	case tcpip.ErrNotConnected:
		return C.ENOTCONN
	case tcpip.ErrConnectionReset:
		return C.ECONNRESET
	case tcpip.ErrConnectionAborted:
		return C.ECONNABORTED
	case tcpip.ErrNoSuchFile:
		return C.ENOENT
	case tcpip.ErrInvalidOptionValue:
		return C.EINVAL
	case tcpip.ErrNoLinkAddress:
		return C.EHOSTDOWN
	case tcpip.ErrBadAddress:
		return C.EFAULT
	case tcpip.ErrNetworkUnreachable:
		return C.ENETUNREACH
	case tcpip.ErrMessageTooLong:
		return C.EMSGSIZE
	case tcpip.ErrNoBufferSpace:
		return C.ENOBUFS
	default:
		panic(fmt.Sprintf("unknown error %v", err))
	}
}

func (ios *iostate) Connect(sockaddr []uint8) (int16, error) {
	addr, err := decodeAddr(sockaddr)
	if err != nil {
		return tcpipErrorToCode(tcpip.ErrBadAddress), nil
	}
	if err := ios.ep.Connect(addr); err != nil {
		return tcpipErrorToCode(err), nil
	}
	return 0, nil
}

func (ios *iostate) Bind(sockaddr []uint8) (int16, error) {
	addr, err := decodeAddr(sockaddr)
	if err != nil {
		return tcpipErrorToCode(tcpip.ErrBadAddress), nil
	}
	if err := ios.ep.Bind(addr, nil); err != nil {
		return tcpipErrorToCode(err), nil
	}
	return 0, nil
}

func (ios *iostate) Listen(backlog int16) (int16, error) {
	if err := ios.ep.Listen(int(backlog)); err != nil {
		return tcpipErrorToCode(err), nil
	}

	return 0, nil
}

func (ios *iostate) Accept(flags int16) (int16, error) {
	ep, wq, err := ios.ep.Accept()
	// NB: we need to do this before checking the error, or the incoming signal
	// will never be cleared.
	//
	// We lock here to ensure that no incoming connection changes readiness
	// while we clear the signal.
	ios.incomingAssertedMu.Lock()
	if ios.ep.Readiness(waiter.EventIn) == 0 {
		if err := ios.dataHandle.Handle().SignalPeer(ZXSIO_SIGNAL_INCOMING, 0); err != nil {
			panic(err)
		}
	}
	ios.incomingAssertedMu.Unlock()
	if err != nil {
		return tcpipErrorToCode(err), nil
	}
	if err := ios.dataHandle.Share(zx.Handle(newIostate(ios.ns, ios.netProto, ios.transProto, wq, ep, true, false))); err != nil {
		panic(err)
	}
	return 0, nil
}

func (ios *iostate) GetSockOpt(level, optName int16) (int16, []uint8, error) {
	val, err := GetSockOpt(ios.ep, ios.transProto, level, optName, false)
	if err != nil {
		return tcpipErrorToCode(err), nil, nil
	}
	b := make([]byte, reflect.TypeOf(val).Size())
	n := copyAsBytes(b, val)
	if n < len(b) {
		panic(fmt.Sprintf("short %T: %d/%d", val, n, len(b)))
	}
	return 0, b, nil
}

func (ios *iostate) SetSockOpt(level, optName int16, optVal []uint8) (int16, error) {
	if err := SetSockOpt(ios.ep, level, optName, optVal); err != nil {
		return tcpipErrorToCode(err), nil
	}
	return 0, nil
}

func (ios *iostate) GetSockName() (int16, []uint8, error) {
	addr, err := ios.ep.GetLocalAddress()
	if err != nil {
		return tcpipErrorToCode(err), nil, nil
	}
	return 0, encodeAddr(ios.netProto, addr), nil
}

func (ios *iostate) GetPeerName() (int16, []uint8, error) {
	addr, err := ios.ep.GetRemoteAddress()
	if err != nil {
		return tcpipErrorToCode(err), nil, nil
	}
	return 0, encodeAddr(ios.netProto, addr), nil
}

func (ios *iostate) Ioctl(req int16, in []uint8) (int16, []uint8, error) {
	switch uint32(req) {
	// TODO(ZX-766): remove when dart/runtime/bin/socket_base_fuchsia.cc uses getifaddrs().
	case ioctlNetcGetNumIfs:
		lastIfInfo = ios.buildIfInfos()
		var b [4]byte
		binary.LittleEndian.PutUint32(b[:], uint32(lastIfInfo.n_info))
		return 0, b[:], nil

	// TODO(ZX-766): remove when dart/runtime/bin/socket_base_fuchsia.cc uses getifaddrs().
	case ioctlNetcGetIfInfoAt:
		if lastIfInfo == nil {
			log.Printf("ioctlNetcGetIfInfoAt: called before ioctlNetcGetNumIfs")
			return tcpipErrorToCode(tcpip.ErrInvalidEndpointState), nil, nil
		}
		if len(in) != 4 {
			log.Printf("ioctlNetcGetIfInfoAt: bad input length %d", len(in))
			return tcpipErrorToCode(tcpip.ErrInvalidOptionValue), nil, nil
		}
		requestedIndex := binary.LittleEndian.Uint32(in)
		if requestedIndex >= uint32(lastIfInfo.n_info) {
			log.Printf("ioctlNetcGetIfInfoAt: index out of range (%d vs %d)", requestedIndex, lastIfInfo.n_info)
			return tcpipErrorToCode(tcpip.ErrInvalidOptionValue), nil, nil
		}
		return 0, lastIfInfo.info[requestedIndex].Marshal(), nil

	case ioctlNetcGetNodename:
		return 0, append([]byte(ios.ns.getNodeName()), 0), nil

	default:
		return 0, nil, fmt.Errorf("opIoctl req=0x%x, in=%x", req, in)
	}
}

func decodeAddr(addr []uint8) (tcpip.FullAddress, error) {
	var sockaddrStorage C.struct_sockaddr_storage
	if err := sockaddrStorage.Unmarshal(addr); err != nil {
		return tcpip.FullAddress{}, err
	}
	return sockaddrStorage.Decode()
}

func (ios *iostate) Close() (int16, error) {
	// Signal that we're about to close. This tells the various message loops to finish
	// processing, and let us know when they're done.
	if err := ios.dataHandle.Handle().Signal(0, LOCAL_SIGNAL_CLOSING); err != nil {
		panic(err)
	}

	close(ios.closing)
	if ios.loopWriteDone != nil {
		<-ios.loopWriteDone
	}

	ios.ep.Close()

	return 0, nil
}
