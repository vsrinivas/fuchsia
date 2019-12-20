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
	"syscall/zx/mxnet"
	"syscall/zx/zxwait"
	"unsafe"

	"syslog"

	"fidl/fuchsia/io"
	"fidl/fuchsia/posix/socket"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
	"gvisor.dev/gvisor/pkg/tcpip/transport/icmp"
	"gvisor.dev/gvisor/pkg/tcpip/transport/tcp"
	"gvisor.dev/gvisor/pkg/tcpip/transport/udp"
	"gvisor.dev/gvisor/pkg/waiter"
)

// #cgo CFLAGS: -D_GNU_SOURCE
// #cgo CFLAGS: -I${SRCDIR}/../../../../zircon/system/ulib/zxs/include
// #cgo CFLAGS: -I${SRCDIR}/../../../../zircon/third_party/ulib/musl/include
// #include <errno.h>
// #include <fcntl.h>
// #include <lib/zxs/protocol.h>
// #include <netinet/in.h>
import "C"

type socketProviderImpl struct {
	ns *Netstack
}

type providerImpl struct {
	ns             *Netstack
	controlService socket.ControlService
	metadata       socketMetadata
}

var _ socket.Provider = (*providerImpl)(nil)

// Highest two bits are used to modify the socket type.
const sockTypesMask = 0x7fff &^ (C.SOCK_CLOEXEC | C.SOCK_NONBLOCK)

func toTransProto(typ, protocol int16) (int16, tcpip.TransportProtocolNumber) {
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
		}
	}
	return C.EPROTONOSUPPORT, 0
}

func (sp *providerImpl) Socket(domain, typ, protocol int16) (int16, socket.ControlInterface, error) {
	var netProto tcpip.NetworkProtocolNumber
	switch domain {
	case C.AF_INET:
		netProto = ipv4.ProtocolNumber
	case C.AF_INET6:
		netProto = ipv6.ProtocolNumber
	default:
		return C.EPFNOSUPPORT, socket.ControlInterface{}, nil
	}

	code, transProto := toTransProto(typ, protocol)
	if code != 0 {
		return code, socket.ControlInterface{}, nil
	}

	wq := new(waiter.Queue)
	sp.ns.mu.Lock()
	ep, err := sp.ns.mu.stack.NewEndpoint(transProto, netProto, wq)
	sp.ns.mu.Unlock()
	if err != nil {
		return tcpipErrorToCode(err), socket.ControlInterface{}, nil
	}
	{
		controlInterface, err := sp.newSocket(netProto, transProto, wq, ep)
		return 0, controlInterface, err
	}
}

const localSignalClosing = zx.SignalUser5

// Data owned by providerImpl used for statistics and other
// introspection.
type socketMetadata struct {
	// Reference to the netstack global endpoint-map.
	endpoints *endpointsMap
	// socketsCreated should be incremented on successful calls to Socket and
	// Accept.
	socketsCreated tcpip.StatCounter
	// socketsDestroyed should be incremented when the resources for a socket
	// are destroyed.
	socketsDestroyed       tcpip.StatCounter
	newSocketNotifications chan<- struct{}
}

type endpoint struct {
	wq *waiter.Queue
	ep tcpip.Endpoint

	mu struct {
		sync.Mutex
		sockOptTimestamp bool
	}

	// The number of live `socketImpl`s that reference this endpoint.
	clones int64

	netProto   tcpip.NetworkProtocolNumber
	transProto tcpip.TransportProtocolNumber

	local, peer zx.Socket

	incoming struct {
		mu struct {
			sync.Mutex
			asserted bool
		}
	}

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

	metadata *socketMetadata
}

// loopWrite connects libc write to the network stack.
func (ios *endpoint) loopWrite() {
	closeFn := func() { ios.close(ios.loopReadDone) }

	const sigs = zx.SignalSocketReadable | zx.SignalSocketPeerWriteDisabled |
		zx.SignalSocketPeerClosed | localSignalClosing

	waitEntry, notifyCh := waiter.NewChannelEntry(nil)
	ios.wq.EventRegister(&waitEntry, waiter.EventOut)
	defer ios.wq.EventUnregister(&waitEntry)

	for {
		// TODO: obviously allocating for each read is silly. A quick hack we can
		// do is store these in a ring buffer, as the lifecycle of this buffer.View
		// starts here, and ends in nearby code we control in link.go.
		v := make([]byte, 0, 2048)
		n, err := ios.local.Read(v[:cap(v)], 0)
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
					if err := ios.ep.Shutdown(tcpip.ShutdownWrite); err != nil && err != tcpip.ErrNotConnected {
						panic(err)
					}
					return
				case zx.ErrShouldWait:
					obs, err := zxwait.Wait(zx.Handle(ios.local), sigs, zx.TimensecInfinite)
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
		if ios.transProto != tcp.ProtocolNumber {
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
			// TODO(fxb.dev/35006): Handle all transport write errors.
			switch err {
			case nil:
				if ios.transProto != tcp.ProtocolNumber {
					if n < int64(len(v)) {
						panic(fmt.Sprintf("UDP disallows short writes; saw: %d/%d", n, len(v)))
					}
				}
				v = v[n:]
				if len(v) != 0 {
					continue
				}
			case tcpip.ErrWouldBlock:
				if ios.transProto != tcp.ProtocolNumber {
					panic(fmt.Sprintf("UDP writes are nonblocking; saw %d/%d", n, len(v)))
				}

				// NB: we can't select on ios.closing here because the client may have
				// written some data into the buffer and then immediately closed the
				// socket. We don't have a choice but to wait around until we get the
				// data out or the connection fails.
				<-notifyCh
				continue
			case tcpip.ErrClosedForSend:
				if err := ios.local.Shutdown(zx.SocketShutdownRead); err != nil {
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

// loopRead connects libc read to the network stack.
func (ios *endpoint) loopRead(inCh <-chan struct{}, initCh chan<- struct{}) {
	var initOnce sync.Once
	initDone := func() { initOnce.Do(func() { close(initCh) }) }

	closeFn := func() { ios.close(ios.loopWriteDone) }

	const sigs = zx.SignalSocketWritable | zx.SignalSocketWriteDisabled |
		zx.SignalSocketPeerClosed | localSignalClosing

	outEntry, outCh := waiter.NewChannelEntry(nil)
	connected := ios.transProto != tcp.ProtocolNumber
	if !connected {
		ios.wq.EventRegister(&outEntry, waiter.EventOut)
		defer func() {
			if !connected {
				// If connected became true then we must have already unregistered
				// below. We must never unregister the same entry twice because that
				// can corrupt the waiter queue.
				ios.wq.EventUnregister(&outEntry)
			}
		}()
	}

	var sender tcpip.FullAddress
	for {
		var v []byte

		for {
			var err *tcpip.Error
			v, _, err = ios.ep.Read(&sender)
			if err == tcpip.ErrInvalidEndpointState {
				if connected {
					panic(fmt.Sprintf("connected endpoint returned %s", err))
				}
				// We're not connected; unblock the caller before waiting for incoming packets.
				initDone()
				select {
				case <-ios.closing:
					// We're shutting down.
					return
				case <-inCh:
					// We got an incoming connection; we must be a listening socket.
					// Because we are a listening socket, we don't expect anymore
					// outbound events so there's no harm in letting outEntry remain
					// registered until the end of the function.
					var err error
					ios.incoming.mu.Lock()
					if !ios.incoming.mu.asserted {
						err = ios.local.Handle().SignalPeer(0, mxnet.MXSIO_SIGNAL_INCOMING)
						ios.incoming.mu.asserted = true
					}
					ios.incoming.mu.Unlock()
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
				var signals zx.Signals = mxnet.MXSIO_SIGNAL_OUTGOING
				switch err {
				case nil, tcpip.ErrWouldBlock, tcpip.ErrClosedForReceive:
					connected = true
					ios.wq.EventUnregister(&outEntry)

					signals |= mxnet.MXSIO_SIGNAL_CONNECTED
				}

				if err := ios.local.Handle().SignalPeer(0, signals); err != nil {
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
				case <-ios.closing:
					// We're shutting down.
					return
				}
			case tcpip.ErrWouldBlock:
				select {
				case <-inCh:
					continue
				case <-ios.closing:
					// We're shutting down.
					return
				}
			case tcpip.ErrClosedForReceive:
				if err := ios.local.Shutdown(zx.SocketShutdownWrite); err != nil {
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

		if ios.transProto != tcp.ProtocolNumber {
			var fdioSocketMsg C.struct_fdio_socket_msg
			fdioSocketMsg.addrlen = C.socklen_t(fdioSocketMsg.addr.Encode(ios.netProto, sender))

			const size = C.sizeof_struct_fdio_socket_msg
			v = append((*[size]byte)(unsafe.Pointer(&fdioSocketMsg))[:], v...)
		}

		for {
			n, err := ios.local.Write(v, 0)
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
						if err := ios.ep.Shutdown(tcpip.ShutdownRead); err != nil {
							// An ErrNotConnected while connected is expected if there
							// is pending data to be read and the connection has been
							// reset by the other end of the endpoint. The endpoint will
							// allow the pending data to be read without error but will
							// return ErrNotConnected if Shutdown is called. Otherwise
							// this is unexpected, panic.
							if !(connected && err == tcpip.ErrNotConnected) {
								panic(err)
							}
							syslog.InfoTf("loopRead", "%p: client shutdown a closed endpoint with %d bytes pending data; ep info: %+v", ios, len(v), ios.ep.Info())
						}
						return
					case zx.ErrShouldWait:
						obs, err := zxwait.Wait(zx.Handle(ios.local), sigs, zx.TimensecInfinite)
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
			if ios.transProto != tcp.ProtocolNumber {
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

func (sp *providerImpl) newSocket(netProto tcpip.NetworkProtocolNumber, transProto tcpip.TransportProtocolNumber, wq *waiter.Queue, ep tcpip.Endpoint) (socket.ControlInterface, error) {
	var flags uint32
	if transProto == tcp.ProtocolNumber {
		flags |= zx.SocketStream
	} else {
		flags |= zx.SocketDatagram
	}
	localS, peerS, err := zx.NewSocket(flags)
	if err != nil {
		return socket.ControlInterface{}, err
	}
	localC, peerC, err := zx.NewChannel(0)
	if err != nil {
		return socket.ControlInterface{}, err
	}

	ios := &endpoint{
		netProto:      netProto,
		transProto:    transProto,
		wq:            wq,
		ep:            ep,
		local:         localS,
		peer:          peerS,
		loopReadDone:  make(chan struct{}),
		loopWriteDone: make(chan struct{}),
		closing:       make(chan struct{}),
		metadata:      &sp.metadata,
	}

	// As the ios.local would be unique across all endpoints for the netstack,
	// we can use that as a key for the endpoints. The ep.ID is not yet initialized
	// at this point and hence we cannot use that as a key.
	if ep, loaded := ios.metadata.endpoints.LoadOrStore(zx.Handle(ios.local), ios.ep); loaded {
		var info stack.TransportEndpointInfo
		switch t := ep.Info().(type) {
		case *tcp.EndpointInfo:
			info = t.TransportEndpointInfo
		case *stack.TransportEndpointInfo:
			info = *t
		}
		syslog.Errorf("endpoint map store error, key %d exists with endpoint %+v", ios.local, info)
	}

	// This must be registered before returning to prevent a race
	// condition.
	inEntry, inCh := waiter.NewChannelEntry(nil)
	ios.wq.EventRegister(&inEntry, waiter.EventIn)

	initCh := make(chan struct{})
	go func() {
		defer close(ios.loopReadDone)
		defer ios.wq.EventUnregister(&inEntry)

		ios.loopRead(inCh, initCh)
	}()
	go func() {
		defer close(ios.loopWriteDone)

		ios.loopWrite()
	}()

	syslog.VLogTf(syslog.DebugVerbosity, "socket", "%p", ios)

	s := &socketImpl{
		endpoint: ios,
		sp:       sp,
	}
	if err := s.Clone(0, io.NodeInterfaceRequest{Channel: localC}); err != nil {
		s.close()
		return socket.ControlInterface{}, err
	}

	ios.metadata.socketsCreated.Increment()
	select {
	case ios.metadata.newSocketNotifications <- struct{}{}:
	default:
	}

	// Wait for initial state checking to complete.
	<-initCh

	return socket.ControlInterface{Channel: peerC}, nil
}

// close destroys the endpoint and releases associated resources, taking its
// reference count into account.
//
// When called, close signals loopRead and loopWrite (via endpoint.closing and
// ios.local) to exit, and then blocks until its arguments are signaled. close
// is typically called with ios.loop{Read,Write}Done.
//
// Note, calling close on an endpoint that has already been closed is safe as
// the cleanup work will only be done once.
func (ios *endpoint) close(loopDone ...<-chan struct{}) int64 {
	clones := atomic.AddInt64(&ios.clones, -1)

	if clones == 0 {
		ios.closeOnce.Do(func() {
			// Interrupt waits on notification channels. Notification reads
			// are always combined with ios.closing in a select statement.
			close(ios.closing)

			// Interrupt waits on endpoint.local. Handle waits always
			// include localSignalClosing.
			if err := ios.local.Handle().Signal(0, localSignalClosing); err != nil {
				panic(err)
			}

			// The interruptions above cause our loops to exit. Wait until
			// they do before releasing resources they may be using.
			for _, ch := range loopDone {
				<-ch
			}

			ios.ep.Close()

			// Delete this endpoint from the global endpoints.
			ios.metadata.endpoints.Delete(zx.Handle(ios.local))

			if err := ios.local.Close(); err != nil {
				panic(err)
			}

			if err := ios.peer.Close(); err != nil {
				panic(err)
			}

			ios.metadata.socketsDestroyed.Increment()
		})
	}

	return clones
}

func tcpipErrorToCode(err *tcpip.Error) int16 {
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

var _ socket.Control = (*socketImpl)(nil)

// TODO(fxb/37419): Remove TransitionalBase after methods landed.
type socketImpl struct {
	*endpoint
	*io.NodeTransitionalBase
	bindingKey fidl.BindingKey
	// listening sockets can call Accept.
	sp *providerImpl
}

func (s *socketImpl) Clone(flags uint32, object io.NodeInterfaceRequest) error {
	clones := atomic.AddInt64(&s.endpoint.clones, 1)
	{
		sCopy := *s
		s := &sCopy
		bindingKey, err := s.sp.controlService.Add(s, object.Channel, func(error) { s.close() })
		sCopy.bindingKey = bindingKey

		syslog.VLogTf(syslog.DebugVerbosity, "Clone", "%p: clones=%d flags=%b key=%d err=%v", s.endpoint, clones, flags, bindingKey, err)

		return err
	}
}

func (s *socketImpl) close() {
	clones := s.endpoint.close(s.endpoint.loopReadDone, s.endpoint.loopWriteDone)

	removed := s.sp.controlService.Remove(s.bindingKey)

	syslog.VLogTf(syslog.DebugVerbosity, "close", "%p: clones=%d key=%d removed=%t", s.endpoint, clones, s.bindingKey, removed)
}

func (s *socketImpl) Close() (int32, error) {
	s.close()
	return int32(zx.ErrOk), nil
}

func (s *socketImpl) Describe() (io.NodeInfo, error) {
	var info io.NodeInfo
	h, err := s.endpoint.peer.Handle().Duplicate(zx.RightsBasic | zx.RightRead | zx.RightWrite)
	syslog.VLogTf(syslog.DebugVerbosity, "Describe", "%p: err=%v", s.endpoint, err)
	if err != nil {
		return info, err
	}
	info.SetSocket(io.Socket{Socket: zx.Socket(h)})
	return info, nil
}

func (s *socketImpl) Sync() (int32, error) {
	syslog.VLogTf(syslog.DebugVerbosity, "Sync", "%p", s.endpoint)

	return 0, &zx.Error{Status: zx.ErrNotSupported, Text: "fuchsia.posix.socket.Control"}
}
func (s *socketImpl) GetAttr() (int32, io.NodeAttributes, error) {
	syslog.VLogTf(syslog.DebugVerbosity, "GetAttr", "%p", s.endpoint)

	return 0, io.NodeAttributes{}, &zx.Error{Status: zx.ErrNotSupported, Text: "fuchsia.posix.socket.Control"}
}
func (s *socketImpl) SetAttr(flags uint32, attributes io.NodeAttributes) (int32, error) {
	syslog.VLogTf(syslog.DebugVerbosity, "SetAttr", "%p", s.endpoint)

	return 0, &zx.Error{Status: zx.ErrNotSupported, Text: "fuchsia.posix.socket.Control"}
}
func (s *socketImpl) Accept(flags int16) (int16, socket.ControlInterface, error) {
	ep, wq, err := s.endpoint.ep.Accept()
	if err != nil {
		return tcpipErrorToCode(err), socket.ControlInterface{}, nil
	}
	{
		var err error
		// We lock here to ensure that no incoming connection changes readiness
		// while we clear the signal.
		s.endpoint.incoming.mu.Lock()
		if s.endpoint.incoming.mu.asserted && s.endpoint.ep.Readiness(waiter.EventIn) == 0 {
			err = s.endpoint.local.Handle().SignalPeer(mxnet.MXSIO_SIGNAL_INCOMING, 0)
			s.endpoint.incoming.mu.asserted = false
		}
		s.endpoint.incoming.mu.Unlock()
		if err != nil {
			ep.Close()
			return 0, socket.ControlInterface{}, err
		}
	}

	localAddr, err := ep.GetLocalAddress()
	if err == tcpip.ErrNotConnected {
		// This should never happen as of writing as GetLocalAddress
		// does not actually return any errors. However, we handle
		// the tcpip.ErrNotConnected case now for the same reasons
		// as mentioned below for the ep.GetRemoteAddress case.
		syslog.VLogTf(syslog.DebugVerbosity, "accept", "%p: disconnected", s.endpoint)
	} else if err != nil {
		panic(err)
	} else {
		// GetRemoteAddress returns a tcpip.ErrNotConnected error if ep is no
		// longer connected. This can happen if the endpoint was closed after
		// the call to Accept returned, but before this point. A scenario this
		// was actually witnessed was when a TCP RST was received after the call
		// to Accept returned, but before this point. If GetRemoteAddress
		// returns other (unexpected) errors, panic.
		remoteAddr, err := ep.GetRemoteAddress()
		if err == tcpip.ErrNotConnected {
			syslog.VLogTf(syslog.DebugVerbosity, "accept", "%p: local=%+v, disconnected", s.endpoint, localAddr)
		} else if err != nil {
			panic(err)
		} else {
			syslog.VLogTf(syslog.DebugVerbosity, "accept", "%p: local=%+v, remote=%+v", s.endpoint, localAddr, remoteAddr)
		}
	}

	{
		controlInterface, err := s.sp.newSocket(s.endpoint.netProto, s.endpoint.transProto, wq, ep)
		return 0, controlInterface, err
	}
}

func (ios *endpoint) Connect(sockaddr []uint8) (int16, error) {
	addr, err := decodeAddr(sockaddr)
	if err != nil {
		return tcpipErrorToCode(tcpip.ErrBadAddress), nil
	}
	// NB: We can't just compare the length to zero because that would
	// mishandle the IPv6-mapped IPv4 unspecified address.
	disconnect := addr.Port == 0 && (len(addr.Addr) == 0 || net.IP(addr.Addr).IsUnspecified())
	if disconnect {
		if err := ios.ep.Disconnect(); err != nil {
			return tcpipErrorToCode(err), nil
		}
	} else {
		if l := len(addr.Addr); l > 0 {
			if ios.netProto == ipv4.ProtocolNumber && l != header.IPv4AddressSize {
				syslog.VLogTf(syslog.DebugVerbosity, "connect", "%p: unsupported address %s", ios, addr.Addr)
				return C.EAFNOSUPPORT, nil
			}
		}
		if err := ios.ep.Connect(addr); err != nil {
			if err == tcpip.ErrConnectStarted {
				localAddr, err := ios.ep.GetLocalAddress()
				if err != nil {
					panic(err)
				}
				syslog.VLogTf(syslog.DebugVerbosity, "connect", "%p: started, local=%+v, addr=%+v", ios, localAddr, addr)
			}
			return tcpipErrorToCode(err), nil
		}
	}

	{
		localAddr, err := ios.ep.GetLocalAddress()
		if err != nil {
			panic(err)
		}

		if disconnect {
			syslog.VLogTf(syslog.DebugVerbosity, "connect", "%p: local=%+v, remote=disconnected", ios, localAddr)
		} else {
			remoteAddr, err := ios.ep.GetRemoteAddress()
			if err != nil {
				panic(err)
			}
			syslog.VLogTf(syslog.DebugVerbosity, "connect", "%p: local=%+v, remote=%+v", ios, localAddr, remoteAddr)
		}
	}

	return 0, nil
}

func (ios *endpoint) Bind(sockaddr []uint8) (int16, error) {
	addr, err := decodeAddr(sockaddr)
	if err != nil {
		return tcpipErrorToCode(tcpip.ErrBadAddress), nil
	}
	if err := ios.ep.Bind(addr); err != nil {
		return tcpipErrorToCode(err), nil
	}

	{
		localAddr, err := ios.ep.GetLocalAddress()
		if err != nil {
			panic(err)
		}
		syslog.VLogTf(syslog.DebugVerbosity, "bind", "%p: local=%+v", ios, localAddr)
	}

	return 0, nil
}

func (ios *endpoint) Listen(backlog int16) (int16, error) {
	if err := ios.ep.Listen(int(backlog)); err != nil {
		return tcpipErrorToCode(err), nil
	}

	syslog.VLogTf(syslog.DebugVerbosity, "listen", "%p: backlog=%d", ios, backlog)

	return 0, nil
}

func (ios *endpoint) GetSockOpt(level, optName int16) (int16, []uint8, error) {
	var val interface{}
	if level == C.SOL_SOCKET && optName == C.SO_TIMESTAMP {
		ios.mu.Lock()
		if ios.mu.sockOptTimestamp {
			val = int32(1)
		} else {
			val = int32(0)
		}
		ios.mu.Unlock()
	} else {
		var err *tcpip.Error
		val, err = GetSockOpt(ios.ep, ios.netProto, ios.transProto, level, optName)
		if err != nil {
			return tcpipErrorToCode(err), nil, nil
		}
	}
	if val, ok := val.([]byte); ok {
		return 0, val, nil
	}
	b := make([]byte, reflect.TypeOf(val).Size())
	n := copyAsBytes(b, val)
	if n < len(b) {
		panic(fmt.Sprintf("short %T: %d/%d", val, n, len(b)))
	}
	syslog.VLogTf(syslog.DebugVerbosity, "getsockopt", "%p: level=%d, optName=%d, optVal[%d]=%v", ios, level, optName, len(b), b)

	return 0, b, nil
}

func (ios *endpoint) SetSockOpt(level, optName int16, optVal []uint8) (int16, error) {
	if level == C.SOL_SOCKET && optName == C.SO_TIMESTAMP {
		if len(optVal) < sizeOfInt32 {
			return tcpipErrorToCode(tcpip.ErrInvalidOptionValue), nil
		}

		v := binary.LittleEndian.Uint32(optVal)
		ios.mu.Lock()
		ios.mu.sockOptTimestamp = v != 0
		ios.mu.Unlock()
	} else {
		if err := SetSockOpt(ios.ep, level, optName, optVal); err != nil {
			return tcpipErrorToCode(err), nil
		}
	}
	syslog.VLogTf(syslog.DebugVerbosity, "setsockopt", "%p: level=%d, optName=%d, optVal[%d]=%v", ios, level, optName, len(optVal), optVal)

	return 0, nil
}

func (ios *endpoint) GetSockName() (int16, []uint8, error) {
	addr, err := ios.ep.GetLocalAddress()
	if err != nil {
		return tcpipErrorToCode(err), nil, nil
	}
	return 0, encodeAddr(ios.netProto, addr), nil
}

func (ios *endpoint) GetPeerName() (int16, []uint8, error) {
	addr, err := ios.ep.GetRemoteAddress()
	if err != nil {
		return tcpipErrorToCode(err), nil, nil
	}
	return 0, encodeAddr(ios.netProto, addr), nil
}

func decodeAddr(addr []uint8) (tcpip.FullAddress, error) {
	var sockaddrStorage C.struct_sockaddr_storage
	if err := sockaddrStorage.Unmarshal(addr); err != nil {
		return tcpip.FullAddress{}, err
	}
	return sockaddrStorage.Decode()
}
