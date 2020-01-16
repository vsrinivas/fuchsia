// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"encoding/binary"
	"fmt"
	"net"
	"reflect"
	"runtime"
	"strings"
	"sync"
	"sync/atomic"
	"syscall/zx"
	"syscall/zx/fidl"
	"syscall/zx/zxsocket"
	"syscall/zx/zxwait"
	"unsafe"

	"syslog"

	"fidl/fuchsia/io"
	"fidl/fuchsia/posix/socket"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/buffer"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
	"gvisor.dev/gvisor/pkg/tcpip/transport/icmp"
	"gvisor.dev/gvisor/pkg/tcpip/transport/tcp"
	"gvisor.dev/gvisor/pkg/tcpip/transport/udp"
	"gvisor.dev/gvisor/pkg/waiter"
)

/*
#cgo CFLAGS: -D_GNU_SOURCE
#cgo CFLAGS: -I${SRCDIR}/../../../../zircon/system/ulib/zxs/include
#cgo CFLAGS: -I${SRCDIR}/../../../../zircon/third_party/ulib/musl/include
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>

// wire format for datagram messages
typedef struct fdio_socket_msg {
  struct sockaddr_storage addr;
  socklen_t addrlen;
  int32_t flags;
} fdio_socket_msg_t;
*/
import "C"

// Data owned by providerImpl used for statistics and other
// introspection.
type socketMetadata struct {
	newSocketNotifications chan<- struct{}
	ns                     *Netstack
}

// endpoint is the base structure that models all network sockets.
type endpoint struct {
	// TODO(fxb/37419): Remove TransitionalBase after methods landed.
	*io.NodeTransitionalBase

	wq *waiter.Queue
	ep tcpip.Endpoint

	mu struct {
		sync.Mutex
		sockOptTimestamp bool
	}

	// The number of live bindings that reference this structure.
	clones int64

	transProto tcpip.TransportProtocolNumber
	netProto   tcpip.NetworkProtocolNumber

	metadata *socketMetadata
}

func (ep *endpoint) close(cleanup func()) int64 {
	clones := atomic.AddInt64(&ep.clones, -1)

	if clones == 0 {
		if cleanup != nil {
			cleanup()
		}
		ep.ep.Close()
	}

	return clones
}

func (ep *endpoint) Sync() (int32, error) {
	syslog.VLogTf(syslog.DebugVerbosity, "Sync", "%p", ep)

	return 0, &zx.Error{Status: zx.ErrNotSupported, Text: fmt.Sprintf("%T", ep)}
}

func (ep *endpoint) GetAttr() (int32, io.NodeAttributes, error) {
	syslog.VLogTf(syslog.DebugVerbosity, "GetAttr", "%p", ep)

	return 0, io.NodeAttributes{}, &zx.Error{Status: zx.ErrNotSupported, Text: fmt.Sprintf("%T", ep)}
}

func (ep *endpoint) SetAttr(flags uint32, attributes io.NodeAttributes) (int32, error) {
	syslog.VLogTf(syslog.DebugVerbosity, "SetAttr", "%p", ep)

	return 0, &zx.Error{Status: zx.ErrNotSupported, Text: fmt.Sprintf("%T", ep)}
}

func (ep *endpoint) Bind(sockaddr []uint8) (socket.BaseSocketBindResult, error) {
	addr, err := decodeAddr(sockaddr)
	if err != nil {
		return socket.BaseSocketBindResultWithErr(tcpipErrorToCode(tcpip.ErrBadAddress)), nil
	}
	if err := ep.ep.Bind(addr); err != nil {
		return socket.BaseSocketBindResultWithErr(tcpipErrorToCode(err)), nil
	}

	{
		localAddr, err := ep.ep.GetLocalAddress()
		if err != nil {
			panic(err)
		}
		syslog.VLogTf(syslog.DebugVerbosity, "bind", "%p: local=%+v", ep, localAddr)
	}

	return socket.BaseSocketBindResultWithResponse(socket.BaseSocketBindResponse{}), nil
}

func (ep *endpoint) Connect(sockaddr []uint8) (socket.BaseSocketConnectResult, error) {
	addr, err := decodeAddr(sockaddr)
	if err != nil {
		return socket.BaseSocketConnectResultWithErr(tcpipErrorToCode(tcpip.ErrBadAddress)), nil
	}
	// NB: We can't just compare the length to zero because that would
	// mishandle the IPv6-mapped IPv4 unspecified address.
	disconnect := addr.Port == 0 && (len(addr.Addr) == 0 || net.IP(addr.Addr).IsUnspecified())
	if disconnect {
		if err := ep.ep.Disconnect(); err != nil {
			return socket.BaseSocketConnectResultWithErr(tcpipErrorToCode(err)), nil
		}
	} else {
		if l := len(addr.Addr); l > 0 {
			if ep.netProto == ipv4.ProtocolNumber && l != header.IPv4AddressSize {
				syslog.VLogTf(syslog.DebugVerbosity, "connect", "%p: unsupported address %s", ep, addr.Addr)
				return socket.BaseSocketConnectResultWithErr(tcpipErrorToCode(tcpip.ErrAddressFamilyNotSupported)), nil
			}
		}
		if err := ep.ep.Connect(addr); err != nil {
			if err == tcpip.ErrConnectStarted {
				localAddr, err := ep.ep.GetLocalAddress()
				if err != nil {
					panic(err)
				}
				syslog.VLogTf(syslog.DebugVerbosity, "connect", "%p: started, local=%+v, addr=%+v", ep, localAddr, addr)
			}
			return socket.BaseSocketConnectResultWithErr(tcpipErrorToCode(err)), nil
		}
	}

	{
		localAddr, err := ep.ep.GetLocalAddress()
		if err != nil {
			panic(err)
		}

		if disconnect {
			syslog.VLogTf(syslog.DebugVerbosity, "connect", "%p: local=%+v, remote=disconnected", ep, localAddr)
		} else {
			remoteAddr, err := ep.ep.GetRemoteAddress()
			if err != nil {
				panic(err)
			}
			syslog.VLogTf(syslog.DebugVerbosity, "connect", "%p: local=%+v, remote=%+v", ep, localAddr, remoteAddr)
		}
	}

	return socket.BaseSocketConnectResultWithResponse(socket.BaseSocketConnectResponse{}), nil
}

func (ep *endpoint) GetSockName() (socket.BaseSocketGetSockNameResult, error) {
	addr, err := ep.ep.GetLocalAddress()
	if err != nil {
		return socket.BaseSocketGetSockNameResultWithErr(tcpipErrorToCode(err)), nil
	}
	return socket.BaseSocketGetSockNameResultWithResponse(socket.BaseSocketGetSockNameResponse{
		Addr: encodeAddr(ep.netProto, addr),
	}), nil
}

func (ep *endpoint) GetPeerName() (socket.BaseSocketGetPeerNameResult, error) {
	addr, err := ep.ep.GetRemoteAddress()
	if err != nil {
		return socket.BaseSocketGetPeerNameResultWithErr(tcpipErrorToCode(err)), nil
	}
	return socket.BaseSocketGetPeerNameResultWithResponse(socket.BaseSocketGetPeerNameResponse{
		Addr: encodeAddr(ep.netProto, addr),
	}), nil
}

func (ep *endpoint) SetSockOpt(level, optName int16, optVal []uint8) (socket.BaseSocketSetSockOptResult, error) {
	if level == C.SOL_SOCKET && optName == C.SO_TIMESTAMP {
		if len(optVal) < sizeOfInt32 {
			return socket.BaseSocketSetSockOptResultWithErr(tcpipErrorToCode(tcpip.ErrInvalidOptionValue)), nil
		}

		v := binary.LittleEndian.Uint32(optVal)
		ep.mu.Lock()
		ep.mu.sockOptTimestamp = v != 0
		ep.mu.Unlock()
	} else {
		if err := SetSockOpt(ep.ep, ep.metadata.ns, level, optName, optVal); err != nil {
			return socket.BaseSocketSetSockOptResultWithErr(tcpipErrorToCode(err)), nil
		}
	}
	syslog.VLogTf(syslog.DebugVerbosity, "setsockopt", "%p: level=%d, optName=%d, optVal[%d]=%v", ep, level, optName, len(optVal), optVal)

	return socket.BaseSocketSetSockOptResultWithResponse(socket.BaseSocketSetSockOptResponse{}), nil
}

func (ep *endpoint) GetSockOpt(level, optName int16) (socket.BaseSocketGetSockOptResult, error) {
	var val interface{}
	if level == C.SOL_SOCKET && optName == C.SO_TIMESTAMP {
		ep.mu.Lock()
		if ep.mu.sockOptTimestamp {
			val = int32(1)
		} else {
			val = int32(0)
		}
		ep.mu.Unlock()
	} else {
		var err *tcpip.Error
		val, err = GetSockOpt(ep.ep, ep.metadata.ns, ep.netProto, ep.transProto, level, optName)
		if err != nil {
			return socket.BaseSocketGetSockOptResultWithErr(tcpipErrorToCode(err)), nil
		}
	}
	if val, ok := val.([]byte); ok {
		return socket.BaseSocketGetSockOptResultWithResponse(socket.BaseSocketGetSockOptResponse{
			Optval: val,
		}), nil
	}
	b := make([]byte, reflect.TypeOf(val).Size())
	n := copyAsBytes(b, val)
	if n < len(b) {
		panic(fmt.Sprintf("short %T: %d/%d", val, n, len(b)))
	}
	syslog.VLogTf(syslog.DebugVerbosity, "getsockopt", "%p: level=%d, optName=%d, optVal[%d]=%v", ep, level, optName, len(b), b)

	return socket.BaseSocketGetSockOptResultWithResponse(socket.BaseSocketGetSockOptResponse{
		Optval: b,
	}), nil
}

type boolWithMutex struct {
	mu struct {
		sync.Mutex
		asserted bool
	}
}

// endpointWithSocket implements a network socket that uses a zircon socket for
// its data plane. This structure creates a pair of goroutines which are
// responsible for moving data and signals between the underlying
// tcpip.Endpoint and the zircon socket.
type endpointWithSocket struct {
	endpoint

	local, peer zx.Socket

	incoming boolWithMutex

	// Along with (*endpoint).close, these channels are used to coordinate
	// orderly shutdown of loops, handles, and endpoints. See the comment
	// on (*endpoint).close for more information.
	//
	// Notes:
	//
	//  - closing is signaled iff close has been called and the reference
	//    count has reached zero.
	//
	//  - loop{Read,Write}Done are signaled iff loop{Read,Write} have
	//    exited, respectively.
	closing, loopReadDone, loopWriteDone chan struct{}

	// This is used to make sure that endpoint.close only cleans up its
	// resources once - the first time it was closed.
	closeOnce sync.Once
}

func newEndpointWithSocket(ep tcpip.Endpoint, wq *waiter.Queue, transProto tcpip.TransportProtocolNumber, netProto tcpip.NetworkProtocolNumber, metadata *socketMetadata) (*endpointWithSocket, error) {
	var flags uint32
	if transProto == tcp.ProtocolNumber {
		flags |= zx.SocketStream
	} else {
		flags |= zx.SocketDatagram
	}
	localS, peerS, err := zx.NewSocket(flags)
	if err != nil {
		return nil, err
	}

	eps := &endpointWithSocket{
		endpoint: endpoint{
			ep:         ep,
			wq:         wq,
			transProto: transProto,
			netProto:   netProto,
			metadata:   metadata,
		},
		local:         localS,
		peer:          peerS,
		loopReadDone:  make(chan struct{}),
		loopWriteDone: make(chan struct{}),
		closing:       make(chan struct{}),
	}

	// Register synchronously to avoid missing incoming event notifications
	// between this function returning and the goroutines below being scheduled.
	// Such races can lead to deadlock.
	inEntry, inCh := waiter.NewChannelEntry(nil)
	eps.wq.EventRegister(&inEntry, waiter.EventIn)

	initCh := make(chan struct{})
	go func() {
		defer close(eps.loopReadDone)
		defer eps.wq.EventUnregister(&inEntry)

		eps.loopRead(inCh, initCh)
	}()
	go func() {
		defer close(eps.loopWriteDone)

		eps.loopWrite()
	}()

	syslog.VLogTf(syslog.DebugVerbosity, "socket", "%p", eps)

	// Wait for initial state checking to complete.
	<-initCh

	metadata.addEndpoint(zx.Handle(localS), ep)

	return eps, nil
}

type endpointWithEvent struct {
	endpoint

	mu struct {
		sync.Mutex
		readView buffer.View
		sender   tcpip.FullAddress
	}

	local, peer zx.Handle

	incoming boolWithMutex

	entry waiter.Entry
}

func (epe *endpointWithEvent) close() int64 {
	epe.wq.EventUnregister(&epe.entry)

	return epe.endpoint.close(func() {
		// Copy the handle before closing below; (*zx.Handle).Close sets the
		// receiver to zx.HandleInvalid.
		key := epe.local

		if err := epe.local.Close(); err != nil {
			panic(err)
		}

		if err := epe.peer.Close(); err != nil {
			panic(err)
		}

		epe.endpoint.metadata.removeEndpoint(key)
	})
}

func (epe *endpointWithEvent) Describe() (io.NodeInfo, error) {
	var info io.NodeInfo
	event, err := epe.peer.Duplicate(zx.RightsBasic)
	syslog.VLogTf(syslog.DebugVerbosity, "Describe", "%p: err=%v", epe, err)
	if err != nil {
		return info, err
	}
	info.SetDatagramSocket(io.DatagramSocket{Event: event})
	return info, nil
}

func (epe *endpointWithEvent) Shutdown(how int16) (socket.DatagramSocketShutdownResult, error) {
	var signals zx.Signals
	var flags tcpip.ShutdownFlags
	switch how {
	case C.SHUT_RD:
		signals = zxsioSignalShutdownRead
		flags = tcpip.ShutdownRead
	case C.SHUT_WR:
		signals = zxsioSignalShutdownWrite
		flags = tcpip.ShutdownWrite
	case C.SHUT_RDWR:
		signals = zxsioSignalShutdownRead | zxsioSignalShutdownWrite
		flags = tcpip.ShutdownRead | tcpip.ShutdownWrite
	default:
		return socket.DatagramSocketShutdownResultWithErr(C.EINVAL), nil
	}
	if err := epe.ep.Shutdown(flags); err != nil {
		return socket.DatagramSocketShutdownResultWithErr(tcpipErrorToCode(err)), nil
	}
	if flags&tcpip.ShutdownRead != 0 {
		epe.wq.EventUnregister(&epe.entry)
	}
	if err := epe.local.SignalPeer(0, signals); err != nil {
		return socket.DatagramSocketShutdownResult{}, err
	}
	return socket.DatagramSocketShutdownResultWithResponse(socket.DatagramSocketShutdownResponse{}), nil
}

const (
	localSignalClosing = zx.SignalUser1
	// TODO(tamird): use definitions from third_party/go when they're available.
	zxsioSignalShutdownRead  = zx.SignalUser4
	zxsioSignalShutdownWrite = zx.SignalUser5
)

// close destroys the endpoint and releases associated resources, taking its
// reference count into account.
//
// When called, close signals loopRead and loopWrite (via closing and
// local) to exit, and then blocks until its arguments are signaled. close
// is typically called with loop{Read,Write}Done.
//
// Note, calling close on an endpoint that has already been closed is safe as
// the cleanup work will only be done once.
func (eps *endpointWithSocket) close(loopDone ...<-chan struct{}) int64 {
	return eps.endpoint.close(func() {
		eps.closeOnce.Do(func() {
			// Interrupt waits on notification channels. Notification reads
			// are always combined with closing in a select statement.
			close(eps.closing)

			// Interrupt waits on endpoint.local. Handle waits always
			// include localSignalClosing.
			if err := eps.local.Handle().Signal(0, localSignalClosing); err != nil {
				panic(err)
			}

			// The interruptions above cause our loops to exit. Wait until
			// they do before releasing resources they may be using.
			for _, ch := range loopDone {
				<-ch
			}

			// Copy the handle before closing below; (*zx.Handle).Close sets the
			// receiver to zx.HandleInvalid.
			key := zx.Handle(eps.local)

			if err := eps.local.Close(); err != nil {
				panic(err)
			}

			if err := eps.peer.Close(); err != nil {
				panic(err)
			}

			eps.endpoint.metadata.removeEndpoint(key)
		})
	})
}

func (eps *endpointWithSocket) Listen(backlog int16) (socket.StreamSocketListenResult, error) {
	if err := eps.ep.Listen(int(backlog)); err != nil {
		return socket.StreamSocketListenResultWithErr(tcpipErrorToCode(err)), nil
	}

	syslog.VLogTf(syslog.DebugVerbosity, "listen", "%p: backlog=%d", eps, backlog)

	return socket.StreamSocketListenResultWithResponse(socket.StreamSocketListenResponse{}), nil
}

func (eps *endpointWithSocket) Accept() (int32, *endpointWithSocket, error) {
	ep, wq, err := eps.endpoint.ep.Accept()
	if err != nil {
		return tcpipErrorToCode(err), nil, nil
	}
	{
		var err error
		// We lock here to ensure that no incoming connection changes readiness
		// while we clear the signal.
		eps.incoming.mu.Lock()
		if eps.incoming.mu.asserted && eps.endpoint.ep.Readiness(waiter.EventIn) == 0 {
			err = eps.local.Handle().SignalPeer(zxsocket.SignalIncoming, 0)
			eps.incoming.mu.asserted = false
		}
		eps.incoming.mu.Unlock()
		if err != nil {
			ep.Close()
			return 0, nil, err
		}
	}

	if localAddr, err := ep.GetLocalAddress(); err == tcpip.ErrNotConnected {
		// This should never happen as of writing as GetLocalAddress
		// does not actually return any errors. However, we handle
		// the tcpip.ErrNotConnected case now for the same reasons
		// as mentioned below for the ep.GetRemoteAddress case.
		syslog.VLogTf(syslog.DebugVerbosity, "accept", "%p: disconnected", eps)
	} else if err != nil {
		panic(err)
	} else {
		// GetRemoteAddress returns a tcpip.ErrNotConnected error if ep is no
		// longer connected. This can happen if the endpoint was closed after
		// the call to Accept returned, but before this point. A scenario this
		// was actually witnessed was when a TCP RST was received after the call
		// to Accept returned, but before this point. If GetRemoteAddress
		// returns other (unexpected) errors, panic.
		if remoteAddr, err := ep.GetRemoteAddress(); err == tcpip.ErrNotConnected {
			syslog.VLogTf(syslog.DebugVerbosity, "accept", "%p: local=%+v, disconnected", eps, localAddr)
		} else if err != nil {
			panic(err)
		} else {
			syslog.VLogTf(syslog.DebugVerbosity, "accept", "%p: local=%+v, remote=%+v", eps, localAddr, remoteAddr)
		}
	}
	{
		ep, err := newEndpointWithSocket(ep, wq, eps.transProto, eps.netProto, eps.endpoint.metadata)
		return 0, ep, err
	}
}

// loopWrite shuttles signals and data from the zircon socket to the tcpip.Endpoint.
func (eps *endpointWithSocket) loopWrite() {
	closeFn := func() { eps.close(eps.loopReadDone) }

	const sigs = zx.SignalSocketReadable | zx.SignalSocketPeerWriteDisabled |
		zx.SignalSocketPeerClosed | localSignalClosing

	waitEntry, notifyCh := waiter.NewChannelEntry(nil)
	eps.wq.EventRegister(&waitEntry, waiter.EventOut)
	defer eps.wq.EventUnregister(&waitEntry)

	for {
		// TODO: obviously allocating for each read is silly. A quick hack we can
		// do is store these in a ring buffer, as the lifecycle of this buffer.View
		// starts here, and ends in nearby code we control in link.go.
		v := make([]byte, 0, 2048)
		n, err := eps.local.Read(v[:cap(v)], 0)
		if err != nil {
			if err, ok := err.(*zx.Error); ok {
				switch err.Status {
				case zx.ErrPeerClosed:
					// The client has unexpectedly disappeared. We normally expect the
					// client to close gracefully via FIDL, but it didn't.
					closeFn()
					return
				case zx.ErrBadState:
					// Reading has been disabled for this socket endpoint.
					if err := eps.ep.Shutdown(tcpip.ShutdownWrite); err != nil && err != tcpip.ErrNotConnected {
						panic(err)
					}
					return
				case zx.ErrShouldWait:
					obs, err := zxwait.Wait(zx.Handle(eps.local), sigs, zx.TimensecInfinite)
					if err != nil {
						panic(err)
					}
					switch {
					case obs&zx.SignalSocketPeerWriteDisabled != 0:
						// The next Read will return zx.ErrBadState.
						continue
					case obs&zx.SignalSocketReadable != 0:
						// The client might have written some data into the socket. Always
						// continue to the loop below and try to read even if the signals
						// show the client has closed the socket.
						continue
					case obs&zx.SignalSocketPeerClosed != 0:
						// The next Read will return zx.ErrPeerClosed.
						continue
					case obs&localSignalClosing != 0:
						// We're shutting down.
						return
					}
				}
			}
			panic(err)
		}
		v = v[:n]

		var opts tcpip.WriteOptions
		if eps.transProto != tcp.ProtocolNumber {
			const size = C.sizeof_struct_fdio_socket_msg
			var fdioSocketMsg C.struct_fdio_socket_msg
			if n := copy((*[size]byte)(unsafe.Pointer(&fdioSocketMsg))[:], v); n != size {
				syslog.Errorf("truncated datagram: %d/%d", n, size)
				closeFn()
				return
			}
			if fdioSocketMsg.addrlen != 0 {
				addr, err := fdioSocketMsg.addr.Decode()
				if err != nil {
					syslog.Errorf("malformed datagram: %s", err)
					closeFn()
					return
				}
				opts.To = &addr
			}
			v = v[size:]
		}

		for {
			n, resCh, err := eps.ep.Write(tcpip.SlicePayload(v), opts)
			if resCh != nil {
				if err != tcpip.ErrNoLinkAddress {
					panic(fmt.Sprintf("err=%v inconsistent with presence of resCh", err))
				}
				if eps.transProto == tcp.ProtocolNumber {
					panic(fmt.Sprintf("TCP link address resolutions happen on connect; saw %d/%d", n, len(v)))
				}
				<-resCh
				continue
			}
			// TODO(fxb.dev/35006): Handle all transport write errors.
			switch err {
			case nil:
				if eps.transProto != tcp.ProtocolNumber {
					if n < int64(len(v)) {
						panic(fmt.Sprintf("UDP disallows short writes; saw: %d/%d", n, len(v)))
					}
				}
				v = v[n:]
				if len(v) != 0 {
					continue
				}
			case tcpip.ErrWouldBlock:
				if eps.transProto != tcp.ProtocolNumber {
					panic(fmt.Sprintf("UDP writes are nonblocking; saw %d/%d", n, len(v)))
				}

				// NB: we can't select on closing here because the client may have
				// written some data into the buffer and then immediately closed the
				// socket. We don't have a choice but to wait around until we get the
				// data out or the connection fails.
				<-notifyCh
				continue
			case tcpip.ErrClosedForSend:
				if err := eps.local.Shutdown(zx.SocketShutdownRead); err != nil {
					panic(err)
				}
				return
			case tcpip.ErrConnectionReset:
				// We got a TCP RST.
				closeFn()
				return
			default:
				optsStr := "<TCP>"
				if to := opts.To; to != nil {
					optsStr = fmt.Sprintf("%+v", *to)
				}
				syslog.Errorf("Endpoint.Write(%s): %s", optsStr, err)
			}
			break
		}
	}
}

// loopRead shuttles signals and data from the tcpip.Endpoint to the zircon socket.
func (eps *endpointWithSocket) loopRead(inCh <-chan struct{}, initCh chan<- struct{}) {
	var initOnce sync.Once
	initDone := func() { initOnce.Do(func() { close(initCh) }) }

	closeFn := func() { eps.close(eps.loopWriteDone) }

	const sigs = zx.SignalSocketWritable | zx.SignalSocketWriteDisabled |
		zx.SignalSocketPeerClosed | localSignalClosing

	outEntry, outCh := waiter.NewChannelEntry(nil)
	connected := eps.transProto != tcp.ProtocolNumber
	if !connected {
		eps.wq.EventRegister(&outEntry, waiter.EventOut)
		defer func() {
			if !connected {
				// If connected became true then we must have already unregistered
				// below. We must never unregister the same entry twice because that
				// can corrupt the waiter queue.
				eps.wq.EventUnregister(&outEntry)
			}
		}()
	}

	var sender tcpip.FullAddress
	for {
		var v []byte

		for {
			var err *tcpip.Error
			v, _, err = eps.ep.Read(&sender)
			if err == tcpip.ErrInvalidEndpointState {
				if connected {
					panic(fmt.Sprintf("connected endpoint returned %s", err))
				}
				// We're not connected; unblock the caller before waiting for incoming packets.
				initDone()
				select {
				case <-eps.closing:
					// We're shutting down.
					return
				case <-inCh:
					// We got an incoming connection; we must be a listening socket.
					// Because we are a listening socket, we don't expect anymore
					// outbound events so there's no harm in letting outEntry remain
					// registered until the end of the function.
					var err error
					eps.incoming.mu.Lock()
					if !eps.incoming.mu.asserted && eps.endpoint.ep.Readiness(waiter.EventIn) != 0 {
						err = eps.local.Handle().SignalPeer(0, zxsocket.SignalIncoming)
						eps.incoming.mu.asserted = true
					}
					eps.incoming.mu.Unlock()
					if err != nil {
						if err, ok := err.(*zx.Error); ok {
							switch err.Status {
							case zx.ErrPeerClosed:
								// The client has unexpectedly disappeared. We normally expect
								// the client to close gracefully via FIDL, but it didn't.
								closeFn()
								return
							}
						}
						panic(err)
					}
					continue
				case <-outCh:
					// We became connected; the next Read will reflect this.
					continue
				}
			} else if !connected {
				var signals zx.Signals = zxsocket.SignalOutgoing
				switch err {
				case nil, tcpip.ErrWouldBlock, tcpip.ErrClosedForReceive:
					connected = true
					eps.wq.EventUnregister(&outEntry)

					signals |= zxsocket.SignalConnected
				}

				if err := eps.local.Handle().SignalPeer(0, signals); err != nil {
					if err, ok := err.(*zx.Error); ok {
						switch err.Status {
						case zx.ErrPeerClosed:
							// The client has unexpectedly disappeared. We normally expect
							// the client to close gracefully via FIDL, but it didn't.
							closeFn()
							return
						}
					}
					panic(err)
				}
			}
			// Either we're connected or not; unblock the caller.
			initDone()
			// TODO(fxb.dev/35006): Handle all transport read errors.
			switch err {
			case nil:
			case tcpip.ErrNoLinkAddress:
				// TODO(tamird/iyerm): revisit this assertion when
				// https://github.com/google/gvisor/issues/751 is fixed.
				if connected {
					panic(fmt.Sprintf("Endpoint.Read() = %s on a connected socket should never happen", err))
				}
				// At the time of writing, this error is only possible when link
				// address resolution fails during an outbound TCP connection attempt.
				// This happens via the following call hierarchy:
				//
				//  (*tcp.endpoint).protocolMainLoop
				//    (*tcp.handshake).execute
				//      (*tcp.handshake).resolveRoute
				//        (*stack.Route).Resolve
				//          (*stack.Stack).GetLinkAddress
				//            (*stack.linkAddrCache).get
				//
				// This is equivalent to the connection having been refused.
				fallthrough
			case tcpip.ErrTimeout:
				// At the time of writing, this error indicates that a TCP connection
				// has failed. This can occur during the TCP handshake if the peer
				// fails to respond to a SYN within 60 seconds, or if the retransmit
				// logic gives up after 60 seconds of missing ACKs from the peer, or if
				// the maximum number of unacknowledged keepalives is reached.
				if connected {
					// The connection was alive but now is dead - this is equivalent to
					// having received a TCP RST.
					closeFn()
					return
				}
				// The connection was never created. This is equivalent to the
				// connection having been refused.
				fallthrough
			case tcpip.ErrConnectionRefused:
				// Linux allows sockets with connection errors to be reused. If the
				// client calls connect() again (and the underlying Endpoint correctly
				// permits the attempt), we need to wait for an outbound event again.
				select {
				case <-outCh:
					continue
				case <-eps.closing:
					// We're shutting down.
					return
				}
			case tcpip.ErrWouldBlock:
				select {
				case <-inCh:
					continue
				case <-eps.closing:
					// We're shutting down.
					return
				}
			case tcpip.ErrClosedForReceive:
				if err := eps.local.Shutdown(zx.SocketShutdownWrite); err != nil {
					panic(err)
				}
				return
			case tcpip.ErrConnectionReset:
				// We got a TCP RST.
				closeFn()
				return
			default:
				syslog.Errorf("Endpoint.Read(): %s", err)
			}
			break
		}

		if eps.transProto != tcp.ProtocolNumber {
			var fdioSocketMsg C.struct_fdio_socket_msg
			fdioSocketMsg.addrlen = C.socklen_t(fdioSocketMsg.addr.Encode(eps.netProto, sender))

			const size = C.sizeof_struct_fdio_socket_msg
			v = append((*[size]byte)(unsafe.Pointer(&fdioSocketMsg))[:], v...)
		}

		for {
			n, err := eps.local.Write(v, 0)
			if err != nil {
				if err, ok := err.(*zx.Error); ok {
					switch err.Status {
					case zx.ErrPeerClosed:
						// The client has unexpectedly disappeared. We normally expect the
						// client to close gracefully via FIDL, but it didn't.
						closeFn()
						return
					case zx.ErrBadState:
						// Writing has been disabled for this socket endpoint.
						if err := eps.ep.Shutdown(tcpip.ShutdownRead); err != nil {
							// An ErrNotConnected while connected is expected if there
							// is pending data to be read and the connection has been
							// reset by the other end of the endpoint. The endpoint will
							// allow the pending data to be read without error but will
							// return ErrNotConnected if Shutdown is called. Otherwise
							// this is unexpected, panic.
							if !(connected && err == tcpip.ErrNotConnected) {
								panic(err)
							}
							syslog.InfoTf("loopRead", "%p: client shutdown a closed endpoint with %d bytes pending data; ep info: %+v", eps, len(v), eps.endpoint.ep.Info())
						}
						return
					case zx.ErrShouldWait:
						obs, err := zxwait.Wait(zx.Handle(eps.local), sigs, zx.TimensecInfinite)
						if err != nil {
							panic(err)
						}
						switch {
						case obs&zx.SignalSocketWriteDisabled != 0:
							// The next Write will return zx.ErrBadState.
							continue
						case obs&zx.SignalSocketWritable != 0:
							continue
						case obs&zx.SignalSocketPeerClosed != 0:
							// The next Write will return zx.ErrPeerClosed.
							continue
						case obs&localSignalClosing != 0:
							// We're shutting down.
							return
						}
					}
				}
				panic(err)
			}
			if eps.transProto != tcp.ProtocolNumber {
				if n < len(v) {
					panic(fmt.Sprintf("UDP disallows short writes; saw: %d/%d", n, len(v)))
				}
			}
			v = v[n:]
			if len(v) == 0 {
				break
			}
		}
	}
}

type controlImpl struct {
	*endpointWithSocket
	bindingKey fidl.BindingKey
	service    *socket.ControlService
}

var _ socket.Control = (*controlImpl)(nil)

func newControl(eps *endpointWithSocket, service *socket.ControlService) (socket.ControlInterface, error) {
	localC, peerC, err := zx.NewChannel(0)
	if err != nil {
		return socket.ControlInterface{}, err
	}
	s := &controlImpl{
		endpointWithSocket: eps,
		service:            service,
	}
	if err := s.Clone(0, io.NodeInterfaceRequest{Channel: localC}); err != nil {
		s.close()
		return socket.ControlInterface{}, err
	}
	return socket.ControlInterface{Channel: peerC}, nil
}

func (s *controlImpl) Clone(flags uint32, object io.NodeInterfaceRequest) error {
	clones := atomic.AddInt64(&s.clones, 1)
	{
		sCopy := *s
		s := &sCopy
		bindingKey, err := s.service.Add(s, object.Channel, func(error) { s.close() })
		sCopy.bindingKey = bindingKey

		syslog.VLogTf(syslog.DebugVerbosity, "Clone", "%p: clones=%d flags=%b key=%d err=%v", s.endpointWithSocket, clones, flags, bindingKey, err)

		return err
	}
}

func (s *controlImpl) Describe() (io.NodeInfo, error) {
	var info io.NodeInfo
	h, err := s.endpointWithSocket.peer.Handle().Duplicate(zx.RightsBasic | zx.RightRead | zx.RightWrite)
	syslog.VLogTf(syslog.DebugVerbosity, "Describe", "%p: err=%v", s.endpointWithSocket, err)
	if err != nil {
		return info, err
	}
	info.SetSocket(io.Socket{Socket: zx.Socket(h)})
	return info, nil
}

func (s *controlImpl) close() {
	clones := s.endpointWithSocket.close(s.loopReadDone, s.loopWriteDone)

	removed := s.service.Remove(s.bindingKey)

	syslog.VLogTf(syslog.DebugVerbosity, "close", "%p: clones=%d key=%d removed=%t", s.endpointWithSocket, clones, s.bindingKey, removed)
}

func (s *controlImpl) Close() (int32, error) {
	s.close()
	return int32(zx.ErrOk), nil
}

func (s *controlImpl) Bind(sockaddr []uint8) (int16, error) {
	result, err := s.endpointWithSocket.Bind(sockaddr)
	if err != nil {
		return 0, err
	}
	switch w := result.Which(); w {
	case socket.BaseSocketBindResultResponse:
		return 0, nil
	case socket.BaseSocketBindResultErr:
		return int16(result.Err), nil
	default:
		panic(fmt.Sprintf("unknown variant %d", w))
	}
}

func (s *controlImpl) Connect(sockaddr []uint8) (int16, error) {
	result, err := s.endpointWithSocket.Connect(sockaddr)
	if err != nil {
		return 0, err
	}
	switch w := result.Which(); w {
	case socket.BaseSocketConnectResultResponse:
		return 0, nil
	case socket.BaseSocketConnectResultErr:
		return int16(result.Err), nil
	default:
		panic(fmt.Sprintf("unknown variant %d", w))
	}
}

func (s *controlImpl) Accept(int16) (int16, socket.ControlInterface, error) {
	code, eps, err := s.endpointWithSocket.Accept()
	if err != nil {
		return 0, socket.ControlInterface{}, err
	}
	if code != 0 {
		return int16(code), socket.ControlInterface{}, nil
	}
	controlInterface, err := newControl(eps, s.service)
	return 0, controlInterface, err
}

func (s *controlImpl) Listen(backlog int16) (int16, error) {
	result, err := s.endpointWithSocket.Listen(backlog)
	if err != nil {
		return 0, err
	}
	switch w := result.Which(); w {
	case socket.StreamSocketListenResultResponse:
		return 0, nil
	case socket.StreamSocketListenResultErr:
		return int16(result.Err), nil
	default:
		panic(fmt.Sprintf("unknown variant %d", w))
	}
}

func (s *controlImpl) GetSockName() (int16, []uint8, error) {
	result, err := s.endpointWithSocket.GetSockName()
	if err != nil {
		return 0, nil, err
	}
	switch w := result.Which(); w {
	case socket.BaseSocketGetSockNameResultResponse:
		return 0, result.Response.Addr, nil
	case socket.BaseSocketGetSockNameResultErr:
		return int16(result.Err), nil, nil
	default:
		panic(fmt.Sprintf("unknown variant %d", w))
	}
}

func (s *controlImpl) GetPeerName() (int16, []uint8, error) {
	result, err := s.endpointWithSocket.GetPeerName()
	if err != nil {
		return 0, nil, err
	}
	switch w := result.Which(); w {
	case socket.BaseSocketGetPeerNameResultResponse:
		return 0, result.Response.Addr, nil
	case socket.BaseSocketGetPeerNameResultErr:
		return int16(result.Err), nil, nil
	default:
		panic(fmt.Sprintf("unknown variant %d", w))
	}
}

func (s *controlImpl) SetSockOpt(level, optName int16, optVal []uint8) (int16, error) {
	result, err := s.endpointWithSocket.SetSockOpt(level, optName, optVal)
	if err != nil {
		return 0, err
	}
	switch w := result.Which(); w {
	case socket.BaseSocketSetSockOptResultResponse:
		return 0, nil
	case socket.BaseSocketSetSockOptResultErr:
		return int16(result.Err), nil
	default:
		panic(fmt.Sprintf("unknown variant %d", w))
	}
}

func (s *controlImpl) GetSockOpt(level, optName int16) (int16, []uint8, error) {
	result, err := s.endpointWithSocket.GetSockOpt(level, optName)
	if err != nil {
		return 0, nil, err
	}
	switch w := result.Which(); w {
	case socket.BaseSocketGetSockOptResultResponse:
		return 0, result.Response.Optval, nil
	case socket.BaseSocketGetSockOptResultErr:
		return int16(result.Err), nil, nil
	default:
		panic(fmt.Sprintf("unknown variant %d", w))
	}
}

type datagramSocketImpl struct {
	*endpointWithEvent
	bindingKey fidl.BindingKey
	service    *socket.DatagramSocketService
}

var _ socket.DatagramSocket = (*datagramSocketImpl)(nil)

func (s *datagramSocketImpl) close() {
	clones := s.endpointWithEvent.close()

	removed := s.service.Remove(s.bindingKey)

	syslog.VLogTf(syslog.DebugVerbosity, "close", "%p: clones=%d key=%d removed=%t", s.endpointWithEvent, clones, s.bindingKey, removed)
}

func (s *datagramSocketImpl) Close() (int32, error) {
	s.close()
	return int32(zx.ErrOk), nil
}

func (s *datagramSocketImpl) Clone(flags uint32, object io.NodeInterfaceRequest) error {
	clones := atomic.AddInt64(&s.clones, 1)
	{
		sCopy := *s
		s := &sCopy
		bindingKey, err := s.service.Add(s, object.Channel, func(error) { s.close() })
		sCopy.bindingKey = bindingKey

		syslog.VLogTf(syslog.DebugVerbosity, "Clone", "%p: clones=%d flags=%b key=%d err=%v", s.endpointWithEvent, clones, flags, bindingKey, err)

		return err
	}
}

func (s *datagramSocketImpl) RecvMsg(addrLen, dataLen, controlLen uint32, flags int16) (socket.DatagramSocketRecvMsgResult, error) {
	s.mu.Lock()
	var err *tcpip.Error
	if len(s.mu.readView) == 0 {
		// TODO(21106): do something with control messages.
		s.mu.readView, _, err = s.ep.Read(&s.mu.sender)
	}
	v, sender := s.mu.readView, s.mu.sender
	if flags&C.MSG_PEEK == 0 {
		s.mu.readView = nil
		s.mu.sender = tcpip.FullAddress{}
	}
	s.mu.Unlock()
	if err != nil {
		return socket.DatagramSocketRecvMsgResultWithErr(tcpipErrorToCode(err)), nil
	}
	{
		var err error
		// We lock here to ensure that no incoming connection changes readiness
		// while we clear the signal.
		s.incoming.mu.Lock()
		if s.endpointWithEvent.incoming.mu.asserted && s.endpoint.ep.Readiness(waiter.EventIn) == 0 {
			err = s.endpointWithEvent.local.SignalPeer(zxsocket.SignalIncoming, 0)
			s.endpointWithEvent.incoming.mu.asserted = false
		}
		s.incoming.mu.Unlock()
		if err != nil {
			panic(err)
		}
	}
	var addr []byte
	if addrLen != 0 {
		addr = encodeAddr(s.netProto, sender)
		if len(addr) > int(addrLen) {
			addr = addr[:addrLen]
		}
	}
	var truncated uint32
	if t := len(v) - int(dataLen); t > 0 {
		truncated = uint32(t)
		v = v[:dataLen]
	}
	return socket.DatagramSocketRecvMsgResultWithResponse(socket.DatagramSocketRecvMsgResponse{
		Addr:      addr,
		Data:      v,
		Truncated: truncated,
	}), nil
}

func (s *datagramSocketImpl) SendMsg(addr []uint8, data [][]uint8, control []uint8, flags int16) (socket.DatagramSocketSendMsgResult, error) {
	var writeOpts tcpip.WriteOptions
	if len(addr) != 0 {
		addr, err := decodeAddr(addr)
		if err != nil {
			return socket.DatagramSocketSendMsgResultWithErr(tcpipErrorToCode(tcpip.ErrBadAddress)), nil
		}
		writeOpts.To = &addr
	}
	// TODO(21106): do something with control.
	// TODO(tamird): do something with flags.

	dataLen := 0
	for _, data := range data {
		dataLen += len(data)
	}
	b := make([]byte, 0, dataLen)
	for _, data := range data {
		b = append(b, data...)
	}

	for {
		n, resCh, err := s.ep.Write(tcpip.SlicePayload(b), writeOpts)
		if resCh != nil {
			if err != tcpip.ErrNoLinkAddress {
				panic(fmt.Sprintf("err=%v inconsistent with presence of resCh", err))
			}
			<-resCh
			continue
		}
		if err != nil {
			return socket.DatagramSocketSendMsgResultWithErr(tcpipErrorToCode(err)), nil
		}
		return socket.DatagramSocketSendMsgResultWithResponse(socket.DatagramSocketSendMsgResponse{Len: n}), nil
	}
}

type streamSocketImpl struct {
	*endpointWithSocket
	bindingKey fidl.BindingKey
	service    *socket.StreamSocketService
}

var _ socket.StreamSocket = (*streamSocketImpl)(nil)

func newStreamSocket(eps *endpointWithSocket, service *socket.StreamSocketService) (socket.StreamSocketInterface, error) {
	localC, peerC, err := zx.NewChannel(0)
	if err != nil {
		return socket.StreamSocketInterface{}, err
	}
	s := &streamSocketImpl{
		endpointWithSocket: eps,
		service:            service,
	}
	if err := s.Clone(0, io.NodeInterfaceRequest{Channel: localC}); err != nil {
		s.close()
		return socket.StreamSocketInterface{}, err
	}
	return socket.StreamSocketInterface{Channel: peerC}, nil
}

func (s *streamSocketImpl) close() {
	clones := s.endpointWithSocket.close(s.loopReadDone, s.loopWriteDone)

	removed := s.service.Remove(s.bindingKey)

	syslog.VLogTf(syslog.DebugVerbosity, "close", "%p: clones=%d key=%d removed=%t", s.endpointWithSocket, clones, s.bindingKey, removed)
}

func (s *streamSocketImpl) Close() (int32, error) {
	s.close()
	return int32(zx.ErrOk), nil
}

func (s *streamSocketImpl) Clone(flags uint32, object io.NodeInterfaceRequest) error {
	clones := atomic.AddInt64(&s.clones, 1)
	{
		sCopy := *s
		s := &sCopy
		bindingKey, err := s.service.Add(s, object.Channel, func(error) { s.close() })
		sCopy.bindingKey = bindingKey

		syslog.VLogTf(syslog.DebugVerbosity, "Clone", "%p: clones=%d flags=%b key=%d err=%v", s.endpointWithSocket, clones, flags, bindingKey, err)

		return err
	}
}

func (s *streamSocketImpl) Describe() (io.NodeInfo, error) {
	var info io.NodeInfo
	h, err := s.endpointWithSocket.peer.Handle().Duplicate(zx.RightsBasic | zx.RightRead | zx.RightWrite)
	syslog.VLogTf(syslog.DebugVerbosity, "Describe", "%p: err=%v", s.endpointWithSocket, err)
	if err != nil {
		return info, err
	}
	info.SetStreamSocket(io.StreamSocket{Socket: zx.Socket(h)})
	return info, nil
}

func (s *streamSocketImpl) Accept(flags int16) (socket.StreamSocketAcceptResult, error) {
	code, eps, err := s.endpointWithSocket.Accept()
	if err != nil {
		return socket.StreamSocketAcceptResult{}, err
	}
	if code != 0 {
		return socket.StreamSocketAcceptResultWithErr(code), nil
	}
	streamSocketInterface, err := newStreamSocket(eps, s.service)
	if err != nil {
		return socket.StreamSocketAcceptResult{}, err
	}
	return socket.StreamSocketAcceptResultWithResponse(socket.StreamSocketAcceptResponse{S: streamSocketInterface}), nil
}

func (metadata *socketMetadata) addEndpoint(handle zx.Handle, ep tcpip.Endpoint) {
	if ep, loaded := metadata.ns.endpoints.LoadOrStore(handle, ep); loaded {
		var info stack.TransportEndpointInfo
		switch t := ep.Info().(type) {
		case *tcp.EndpointInfo:
			info = t.TransportEndpointInfo
		case *stack.TransportEndpointInfo:
			info = *t
		}
		syslog.Errorf("endpoint map store error, key %d exists with endpoint %+v", handle, info)
	}

	metadata.ns.stats.SocketsCreated.Increment()
	select {
	case metadata.newSocketNotifications <- struct{}{}:
	default:
	}
}

func (metadata *socketMetadata) removeEndpoint(handle zx.Handle) {
	metadata.ns.endpoints.Delete(handle)
	metadata.ns.stats.SocketsDestroyed.Increment()
}

type providerImpl struct {
	controlService        socket.ControlService
	datagramSocketService socket.DatagramSocketService
	streamSocketService   socket.StreamSocketService
	metadata              socketMetadata
}

var _ socket.Provider = (*providerImpl)(nil)

// Highest two bits modify the socket type.
const sockTypesMask = 0x7fff &^ (C.SOCK_CLOEXEC | C.SOCK_NONBLOCK)

func toTransProto(typ, protocol int16) (int32, tcpip.TransportProtocolNumber) {
	switch typ & sockTypesMask {
	case C.SOCK_STREAM:
		switch protocol {
		case C.IPPROTO_IP, C.IPPROTO_TCP:
			return 0, tcp.ProtocolNumber
		}
	case C.SOCK_DGRAM:
		switch protocol {
		case C.IPPROTO_IP, C.IPPROTO_UDP:
			return 0, udp.ProtocolNumber
		case C.IPPROTO_ICMP:
			return 0, icmp.ProtocolNumber4
		case C.IPPROTO_ICMPV6:
			return 0, icmp.ProtocolNumber6
		}
	}
	return C.EPROTONOSUPPORT, 0
}

func toNetProto(domain int16) (int32, tcpip.NetworkProtocolNumber) {
	switch domain {
	case C.AF_INET:
		return 0, ipv4.ProtocolNumber
	case C.AF_INET6:
		return 0, ipv6.ProtocolNumber
	case C.AF_PACKET:
		return C.EPERM, 0
	default:
		return C.EPFNOSUPPORT, 0
	}
}

func (sp *providerImpl) Socket(domain, typ, protocol int16) (int16, socket.ControlInterface, error) {
	code, netProto := toNetProto(domain)
	if code != 0 {
		return int16(code), socket.ControlInterface{}, nil
	}
	code, transProto := toTransProto(typ, protocol)
	if code != 0 {
		return int16(code), socket.ControlInterface{}, nil
	}
	wq := new(waiter.Queue)
	sp.metadata.ns.mu.Lock()
	ep, err := sp.metadata.ns.mu.stack.NewEndpoint(transProto, netProto, wq)
	sp.metadata.ns.mu.Unlock()
	if err != nil {
		return int16(tcpipErrorToCode(err)), socket.ControlInterface{}, nil
	}
	{
		ep, err := newEndpointWithSocket(ep, wq, transProto, netProto, &sp.metadata)
		if err != nil {
			return 0, socket.ControlInterface{}, err
		}
		controlInterface, err := newControl(ep, &sp.controlService)
		return 0, controlInterface, err
	}
}

type callback func(*waiter.Entry)

func (cb callback) Callback(e *waiter.Entry) {
	cb(e)
}

func (sp *providerImpl) Socket2(domain, typ, protocol int16) (socket.ProviderSocket2Result, error) {
	code, netProto := toNetProto(domain)
	if code != 0 {
		return socket.ProviderSocket2ResultWithErr(code), nil
	}
	code, transProto := toTransProto(typ, protocol)
	if code != 0 {
		return socket.ProviderSocket2ResultWithErr(code), nil
	}
	wq := new(waiter.Queue)
	sp.metadata.ns.mu.Lock()
	ep, err := sp.metadata.ns.mu.stack.NewEndpoint(transProto, netProto, wq)
	sp.metadata.ns.mu.Unlock()
	if err != nil {
		return socket.ProviderSocket2ResultWithErr(tcpipErrorToCode(err)), nil
	}

	if transProto == tcp.ProtocolNumber {
		ep, err := newEndpointWithSocket(ep, wq, transProto, netProto, &sp.metadata)
		if err != nil {
			return socket.ProviderSocket2Result{}, err
		}
		streamSocketInterface, err := newStreamSocket(ep, &sp.streamSocketService)
		if err != nil {
			return socket.ProviderSocket2Result{}, err
		}
		return socket.ProviderSocket2ResultWithResponse(socket.ProviderSocket2Response{
			S: socket.BaseSocketInterface(streamSocketInterface),
		}), nil
	} else {
		var localE, peerE zx.Handle
		if status := zx.Sys_eventpair_create(0, &localE, &peerE); status != zx.ErrOk {
			return socket.ProviderSocket2Result{}, &zx.Error{Status: status, Text: "zx.EventPair"}
		}
		localC, peerC, err := zx.NewChannel(0)
		if err != nil {
			return socket.ProviderSocket2Result{}, err
		}
		s := &datagramSocketImpl{
			endpointWithEvent: &endpointWithEvent{
				endpoint: endpoint{
					ep:         ep,
					wq:         wq,
					transProto: transProto,
					netProto:   netProto,
					metadata:   &sp.metadata,
				},
				local: localE,
				peer:  peerE,
			},
			service: &sp.datagramSocketService,
		}

		s.entry.Callback = callback(func(*waiter.Entry) {
			var err error
			s.endpointWithEvent.incoming.mu.Lock()
			if !s.endpointWithEvent.incoming.mu.asserted {
				err = s.endpointWithEvent.local.SignalPeer(0, zxsocket.SignalIncoming)
				s.endpointWithEvent.incoming.mu.asserted = true
			}
			s.endpointWithEvent.incoming.mu.Unlock()
			if err != nil {
				panic(err)
			}
		})

		s.wq.EventRegister(&s.entry, waiter.EventIn)

		if err := s.Clone(0, io.NodeInterfaceRequest{Channel: localC}); err != nil {
			s.close()
			return socket.ProviderSocket2Result{}, err
		}
		datagramSocketInterface := socket.DatagramSocketInterface{Channel: peerC}

		sp.metadata.addEndpoint(localE, ep)

		return socket.ProviderSocket2ResultWithResponse(socket.ProviderSocket2Response{
			S: socket.BaseSocketInterface(datagramSocketInterface),
		}), nil
	}
}

func tcpipErrorToCode(err *tcpip.Error) int32 {
	if err != tcpip.ErrConnectStarted {
		if pc, file, line, ok := runtime.Caller(1); ok {
			if i := strings.LastIndexByte(file, '/'); i != -1 {
				file = file[i+1:]
			}
			syslog.VLogf(syslog.DebugVerbosity, "%s: %s:%d: %s", runtime.FuncForPC(pc).Name(), file, line, err)
		} else {
			syslog.VLogf(syslog.DebugVerbosity, "%s", err)
		}
	}
	switch err {
	case tcpip.ErrUnknownProtocol:
		return C.EINVAL
	case tcpip.ErrUnknownNICID:
		return C.EINVAL
	case tcpip.ErrUnknownDevice:
		return C.ENODEV
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
	case tcpip.ErrBroadcastDisabled, tcpip.ErrNotPermitted:
		return C.EACCES
	case tcpip.ErrAddressFamilyNotSupported:
		return C.EAFNOSUPPORT
	default:
		panic(fmt.Sprintf("unknown error %v", err))
	}
}
