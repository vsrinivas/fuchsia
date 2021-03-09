// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package netstack

import (
	"bytes"
	"context"
	"encoding/binary"
	"fmt"
	"io"
	"reflect"
	"runtime"
	"sort"
	"strings"
	"sync"
	"sync/atomic"
	"syscall/zx"
	"syscall/zx/fidl"
	"syscall/zx/zxsocket"
	"syscall/zx/zxwait"
	"time"
	"unsafe"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"
	"go.fuchsia.dev/fuchsia/src/lib/component"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	fidlio "fidl/fuchsia/io"
	fidlnet "fidl/fuchsia/net"
	"fidl/fuchsia/posix"
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

// TODO(https://fxbug.dev/44347) We shouldn't need any of this includes after we remove
// C structs from the wire.

/*
#cgo CFLAGS: -D_GNU_SOURCE
#cgo CFLAGS: -I${SRCDIR}/../../../../zircon/system/ulib/zxs/include
#cgo CFLAGS: -I${SRCDIR}/../../../../zircon/third_party/ulib/musl/include
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
*/
import "C"

var _ io.Writer = (*socketWriter)(nil)

type socketWriter struct {
	socket    zx.Socket
	lastError error
}

func (w *socketWriter) Write(p []byte) (int, error) {
	n, err := w.socket.Write(p, 0)
	if err == nil && n != len(p) {
		err = &zx.Error{Status: zx.ErrShouldWait}
	}
	w.lastError = err
	return n, err
}

var _ tcpip.Payloader = (*socketReader)(nil)

type socketReader struct {
	socket    zx.Socket
	lastError error
	lastRead  int
}

func (r *socketReader) Read(p []byte) (int, error) {
	n, err := r.socket.Read(p, 0)
	if err == nil && n != len(p) {
		err = &zx.Error{Status: zx.ErrShouldWait}
	}
	r.lastError = err
	r.lastRead = n
	return n, err
}

func (r *socketReader) Len() int {
	n, err := func() (int, error) {
		var info zx.InfoSocket
		if err := r.socket.Handle().GetInfo(zx.ObjectInfoSocket, unsafe.Pointer(&info), uint(unsafe.Sizeof(info))); err != nil {
			return 0, err
		}
		return info.RXBufAvailable, nil
	}()
	if err == nil && n == 0 {
		err = &zx.Error{Status: zx.ErrShouldWait}
	}
	r.lastError = err
	r.lastRead = n
	return n
}

type hardError struct {
	mu struct {
		sync.Mutex
		err tcpip.Error
	}
}

// endpoint is the base structure that models all network sockets.
type endpoint struct {
	// TODO(https://fxbug.dev/37419): Remove TransitionalBase after methods landed.
	*fidlio.NodeWithCtxTransitionalBase

	wq *waiter.Queue
	ep tcpip.Endpoint

	mu struct {
		sync.Mutex
		refcount         uint32
		sockOptTimestamp bool
	}

	transProto tcpip.TransportProtocolNumber
	netProto   tcpip.NetworkProtocolNumber

	key uint64

	ns *Netstack

	// gVisor stack clears the hard error on the endpoint on a read, so,
	// save the error when returned by gVisor endpoint calls.
	hardError hardError
}

func (ep *endpoint) incRef() {
	ep.mu.Lock()
	ep.mu.refcount++
	ep.mu.Unlock()
}

func (ep *endpoint) decRef() bool {
	ep.mu.Lock()
	doubleClose := ep.mu.refcount == 0
	ep.mu.refcount--
	doClose := ep.mu.refcount == 0
	ep.mu.Unlock()
	if doubleClose {
		panic(fmt.Sprintf("%p: double close", ep))
	}
	return doClose
}

// storeAndRetrieveLocked evaluates if the input error is a "hard
// error" (one which puts the endpoint in an unrecoverable error state) and
// stores it. Returns the pre-existing hard error if it was already set or the
// new value if changed.
//
// Must be called with he.mu held.
func (he *hardError) storeAndRetrieveLocked(err tcpip.Error) tcpip.Error {
	if he.mu.err == nil {
		switch err.(type) {
		case *tcpip.ErrConnectionAborted, *tcpip.ErrConnectionReset,
			*tcpip.ErrNetworkUnreachable, *tcpip.ErrNoRoute, *tcpip.ErrTimeout,
			*tcpip.ErrConnectionRefused:
			he.mu.err = err
		}
	}
	return he.mu.err
}

func (ep *endpoint) Sync(fidl.Context) (int32, error) {
	_ = syslog.DebugTf("Sync", "%p", ep)

	return 0, &zx.Error{Status: zx.ErrNotSupported, Text: fmt.Sprintf("%T", ep)}
}

func (ep *endpoint) GetAttr(fidl.Context) (int32, fidlio.NodeAttributes, error) {
	_ = syslog.DebugTf("GetAttr", "%p", ep)

	return 0, fidlio.NodeAttributes{}, &zx.Error{Status: zx.ErrNotSupported, Text: fmt.Sprintf("%T", ep)}
}

func (ep *endpoint) SetAttr(fidl.Context, uint32, fidlio.NodeAttributes) (int32, error) {
	_ = syslog.DebugTf("SetAttr", "%p", ep)

	return 0, &zx.Error{Status: zx.ErrNotSupported, Text: fmt.Sprintf("%T", ep)}
}

func (ep *endpoint) Bind(_ fidl.Context, sockaddr fidlnet.SocketAddress) (socket.BaseSocketBindResult, error) {
	addr, err := toTCPIPFullAddress(sockaddr)
	if err != nil {
		return socket.BaseSocketBindResultWithErr(tcpipErrorToCode(&tcpip.ErrBadAddress{})), nil
	}
	if err := ep.ep.Bind(addr); err != nil {
		return socket.BaseSocketBindResultWithErr(tcpipErrorToCode(err)), nil
	}

	{
		localAddr, err := ep.ep.GetLocalAddress()
		if err != nil {
			panic(err)
		}
		_ = syslog.DebugTf("bind", "%p: local=%+v", ep, localAddr)
	}

	return socket.BaseSocketBindResultWithResponse(socket.BaseSocketBindResponse{}), nil
}

func (ep *endpoint) Connect(_ fidl.Context, address fidlnet.SocketAddress) (socket.BaseSocketConnectResult, error) {
	addr, err := toTCPIPFullAddress(address)
	if err != nil {
		return socket.BaseSocketConnectResultWithErr(tcpipErrorToCode(&tcpip.ErrBadAddress{})), nil
	}
	if l := len(addr.Addr); l > 0 {
		if ep.netProto == ipv4.ProtocolNumber && l != header.IPv4AddressSize {
			_ = syslog.DebugTf("connect", "%p: unsupported address %s", ep, addr.Addr)
			return socket.BaseSocketConnectResultWithErr(tcpipErrorToCode(&tcpip.ErrAddressFamilyNotSupported{})), nil
		}
	}

	{
		// Acquire hard error lock across ep calls to avoid races and store the
		// hard error deterministically.
		ep.hardError.mu.Lock()
		err := ep.ep.Connect(addr)
		hardError := ep.hardError.storeAndRetrieveLocked(err)
		ep.hardError.mu.Unlock()
		if err != nil {
			switch err.(type) {
			case *tcpip.ErrConnectStarted:
				localAddr, err := ep.ep.GetLocalAddress()
				if err != nil {
					panic(err)
				}
				_ = syslog.DebugTf("connect", "%p: started, local=%+v, addr=%+v", ep, localAddr, addr)
			// For TCP endpoints, gVisor Connect() returns this error when the endpoint
			// is in an error state and the hard error state has already been read from the
			// endpoint via other APIs. Apply the saved hard error state here.
			case *tcpip.ErrConnectionAborted:
				if hardError != nil {
					err = hardError
				}
			}
			return socket.BaseSocketConnectResultWithErr(tcpipErrorToCode(err)), nil
		}
	}

	{
		localAddr, err := ep.ep.GetLocalAddress()
		if err != nil {
			panic(err)
		}

		remoteAddr, err := ep.ep.GetRemoteAddress()
		if err != nil {
			if _, ok := err.(*tcpip.ErrNotConnected); !ok {
				panic(err)
			}
			_ = syslog.DebugTf("connect", "%p: local=%+v, remote=disconnected", ep, localAddr)
		} else {
			_ = syslog.DebugTf("connect", "%p: local=%+v, remote=%+v", ep, localAddr, remoteAddr)
		}
	}

	return socket.BaseSocketConnectResultWithResponse(socket.BaseSocketConnectResponse{}), nil
}

func (ep *endpoint) Disconnect(_ fidl.Context) (socket.BaseSocketDisconnectResult, error) {
	if err := ep.ep.Disconnect(); err != nil {
		return socket.BaseSocketDisconnectResultWithErr(tcpipErrorToCode(err)), nil
	}
	return socket.BaseSocketDisconnectResultWithResponse(socket.BaseSocketDisconnectResponse{}), nil
}

func (ep *endpoint) GetSockName(fidl.Context) (socket.BaseSocketGetSockNameResult, error) {
	addr, err := ep.ep.GetLocalAddress()
	if err != nil {
		return socket.BaseSocketGetSockNameResultWithErr(tcpipErrorToCode(err)), nil
	}
	return socket.BaseSocketGetSockNameResultWithResponse(socket.BaseSocketGetSockNameResponse{
		Addr: toNetSocketAddress(ep.netProto, addr),
	}), nil
}

func (ep *endpoint) GetPeerName(fidl.Context) (socket.BaseSocketGetPeerNameResult, error) {
	addr, err := ep.ep.GetRemoteAddress()
	if err != nil {
		return socket.BaseSocketGetPeerNameResultWithErr(tcpipErrorToCode(err)), nil
	}
	return socket.BaseSocketGetPeerNameResultWithResponse(socket.BaseSocketGetPeerNameResponse{
		Addr: toNetSocketAddress(ep.netProto, addr),
	}), nil
}

func (ep *endpoint) SetSockOpt(_ fidl.Context, level, optName int16, optVal []uint8) (socket.BaseSocketSetSockOptResult, error) {
	if level == C.SOL_SOCKET && optName == C.SO_TIMESTAMP {
		if len(optVal) < sizeOfInt32 {
			return socket.BaseSocketSetSockOptResultWithErr(tcpipErrorToCode(&tcpip.ErrInvalidOptionValue{})), nil
		}

		v := binary.LittleEndian.Uint32(optVal)
		ep.mu.Lock()
		ep.mu.sockOptTimestamp = v != 0
		ep.mu.Unlock()
	} else {
		if err := SetSockOpt(ep.ep, ep.ns, level, optName, optVal); err != nil {
			return socket.BaseSocketSetSockOptResultWithErr(tcpipErrorToCode(err)), nil
		}
	}
	_ = syslog.DebugTf("setsockopt", "%p: level=%d, optName=%d, optVal[%d]=%v", ep, level, optName, len(optVal), optVal)

	return socket.BaseSocketSetSockOptResultWithResponse(socket.BaseSocketSetSockOptResponse{}), nil
}

func (ep *endpoint) GetSockOpt(_ fidl.Context, level, optName int16) (socket.BaseSocketGetSockOptResult, error) {
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
		var err tcpip.Error
		val, err = GetSockOpt(ep.ep, ep.ns, &ep.hardError, ep.netProto, ep.transProto, level, optName)
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
	_ = syslog.DebugTf("getsockopt", "%p: level=%d, optName=%d, optVal[%d]=%v", ep, level, optName, len(b), b)

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

	// These channels enable coordination of orderly shutdown of loops, handles,
	// and endpoints. See the comment on `close` for more information.
	mu struct {
		sync.Mutex

		// loop{Read,Write,Poll}Done are signaled iff loop{Read,Write,Poll} have
		// exited, respectively.
		loopReadDone, loopWriteDone, loopPollDone <-chan struct{}
	}

	// closing is signaled iff close has been called.
	closing chan struct{}

	// This is used to make sure that endpoint.close only cleans up its
	// resources once - the first time it was closed.
	closeOnce sync.Once

	// Used to unblock waiting to write when SO_LINGER is enabled.
	linger chan struct{}

	onHUpOnce sync.Once

	// onHUp is used to register callback for closing events.
	onHUp waiter.Entry

	// onListen is used to register callbacks for listening sockets.
	onListen sync.Once

	// onConnect is used to register callbacks for connected sockets.
	onConnect sync.Once
}

func newEndpointWithSocket(ep tcpip.Endpoint, wq *waiter.Queue, transProto tcpip.TransportProtocolNumber, netProto tcpip.NetworkProtocolNumber, ns *Netstack) (*endpointWithSocket, error) {
	localS, peerS, err := zx.NewSocket(zx.SocketStream)
	if err != nil {
		return nil, err
	}

	eps := &endpointWithSocket{
		endpoint: endpoint{
			ep:         ep,
			wq:         wq,
			transProto: transProto,
			netProto:   netProto,
			ns:         ns,
		},
		local:   localS,
		peer:    peerS,
		closing: make(chan struct{}),
		linger:  make(chan struct{}),
	}

	onHUp := func() {
		eps.onHUpOnce.Do(func() {
			if !eps.endpoint.ns.onRemoveEndpoint(eps.endpoint.key) {
				_ = syslog.Errorf("endpoint map delete error, endpoint with key %d does not exist", eps.endpoint.key)
			}
			// Run this in a separate goroutine to avoid deadlock.
			//
			// The waiter.Queue lock is held by the caller of this callback.
			// close() blocks on completions of `loop*`, which
			// depend on acquiring waiter.Queue lock to unregister events.
			go func() {
				eps.wq.EventUnregister(&eps.onHUp)
				eps.close()
			}()
		})
	}

	// Add the endpoint before registering callback for hangup event.
	// The callback could be called soon after registration, where the
	// endpoint is attempted to be removed from the map.
	ns.onAddEndpoint(&eps.endpoint)

	eps.onHUp.Callback = callback(func(*waiter.Entry, waiter.EventMask) {
		onHUp()
	})
	eps.wq.EventRegister(&eps.onHUp, waiter.EventHUp)

	// Accepted endpoints which are already reset would not notify hangup event.
	// Check for the hard error state and handle any cleanup.
	//
	// Note that we register a callback for hangup event and add the endpoint
	// to the internal map before checking for the error state. This is to avoid
	// losing notification of hangup event with a race between checking for hard
	// error state in gVisor endpoint and the same endpoint's error state being
	// updated because of processing of an incoming RST.
	//
	// Acquire hard error lock across ep calls to avoid races and store the
	// hard error deterministically.
	eps.endpoint.hardError.mu.Lock()
	hardError := eps.endpoint.hardError.storeAndRetrieveLocked(eps.ep.LastError())
	eps.endpoint.hardError.mu.Unlock()
	if hardError != nil {
		onHUp()
	}
	return eps, nil
}

func (eps *endpointWithSocket) loopPoll(ch chan<- struct{}) {
	defer close(ch)

	sigs := zx.Signals(zx.SignalSocketWriteDisabled | localSignalClosing)

	for {
		obs, err := zxwait.Wait(zx.Handle(eps.local), sigs, zx.TimensecInfinite)
		if err != nil {
			panic(err)
		}

		if obs&sigs&zx.SignalSocketWriteDisabled != 0 {
			sigs ^= zx.SignalSocketWriteDisabled
			switch err := eps.ep.Shutdown(tcpip.ShutdownRead); err.(type) {
			case nil, *tcpip.ErrNotConnected:
				// Shutdown can return ErrNotConnected if the endpoint was connected
				// but no longer is.
			default:
				panic(err)
			}
		}

		if obs&localSignalClosing != 0 {
			// We're shutting down.
			return
		}
	}
}

type endpointWithEvent struct {
	endpoint

	local, peer zx.Handle

	incoming boolWithMutex

	entry waiter.Entry
}

func (epe *endpointWithEvent) Describe(fidl.Context) (fidlio.NodeInfo, error) {
	var info fidlio.NodeInfo
	event, err := epe.peer.Duplicate(zx.RightsBasic)
	_ = syslog.DebugTf("Describe", "%p: err=%v", epe, err)
	if err != nil {
		return info, err
	}
	info.SetDatagramSocket(fidlio.DatagramSocket{Event: event})
	return info, nil
}

func (epe *endpointWithEvent) Shutdown(_ fidl.Context, how socket.ShutdownMode) (socket.DatagramSocketShutdownResult, error) {
	var signals zx.Signals
	var flags tcpip.ShutdownFlags

	if how&socket.ShutdownModeRead != 0 {
		signals |= zxsocket.SignalShutdownRead
		flags |= tcpip.ShutdownRead
	}
	if how&socket.ShutdownModeWrite != 0 {
		signals |= zxsocket.SignalShutdownWrite
		flags |= tcpip.ShutdownWrite
	}
	if flags == 0 {
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

const localSignalClosing = zx.SignalUser1

// close destroys the endpoint and releases associated resources.
//
// When called, close signals loopRead and loopWrite (via closing and
// local) to exit, and then blocks until its arguments are signaled. close
// is typically called with loop{Read,Write}Done.
//
// Note, calling close on an endpoint that has already been closed is safe as
// the cleanup work will only be done once.
func (eps *endpointWithSocket) close() {
	eps.closeOnce.Do(func() {
		// Interrupt waits on notification channels. Notification reads
		// are always combined with closing in a select statement.
		close(eps.closing)

		// Interrupt waits on endpoint.local. Handle waits always
		// include localSignalClosing.
		if err := eps.local.Handle().Signal(0, localSignalClosing); err != nil {
			panic(err)
		}

		// Grab the loop channels _after_ having closed `eps.closing` to avoid a
		// race in which the loops are allowed to start without guaranteeing that
		// this routine will wait for them to return.
		eps.mu.Lock()
		channels := []<-chan struct{}{
			eps.mu.loopReadDone,
			eps.mu.loopWriteDone,
			eps.mu.loopPollDone,
		}
		eps.mu.Unlock()

		// The interruptions above cause our loops to exit. Wait until
		// they do before releasing resources they may be using.
		for _, ch := range channels {
			if ch != nil {
				<-ch
			}
		}

		// The gVisor endpoint could have got a hard error after the
		// read/write loops have ended, check for that here.
		eps.endpoint.hardError.mu.Lock()
		err := eps.endpoint.hardError.storeAndRetrieveLocked(eps.ep.LastError())
		eps.endpoint.hardError.mu.Unlock()
		// Signal the client about hard errors that require special errno
		// handling by the client for read/write calls.
		switch err.(type) {
		case *tcpip.ErrConnectionRefused:
			if err := eps.local.Handle().SignalPeer(0, zxsocket.SignalConnectionRefused); err != nil {
				panic(fmt.Sprintf("Handle().SignalPeer(0, zxsocket.SignalConnectionRefused) = %s", err))
			}
		case *tcpip.ErrConnectionReset:
			if err := eps.local.Handle().SignalPeer(0, zxsocket.SignalConnectionReset); err != nil {
				panic(fmt.Sprintf("Handle().SignalPeer(0, zxsocket.SignalConnectionReset) = %s", err))
			}
		}

		if err := eps.local.Close(); err != nil {
			panic(err)
		}

		eps.ep.Close()

		_ = syslog.DebugTf("close", "%p", eps)
	})
}

func (eps *endpointWithSocket) Listen(_ fidl.Context, backlog int16) (socket.StreamSocketListenResult, error) {
	if backlog < 0 {
		backlog = 0
	}

	if err := eps.ep.Listen(int(backlog)); err != nil {
		return socket.StreamSocketListenResultWithErr(tcpipErrorToCode(err)), nil
	}

	// It is possible to call `listen` on a connected socket - such a call would
	// fail above, so we register the callback only in the success case to avoid
	// incorrectly handling events on connected sockets.
	eps.onListen.Do(func() {
		// Start polling for any shutdown events from the client as shutdown is
		// allowed on a listening stream socket.
		eps.startPollLoop()
		var entry waiter.Entry
		cb := func() {
			var err error
			eps.incoming.mu.Lock()
			if !eps.incoming.mu.asserted && eps.endpoint.ep.Readiness(waiter.EventIn) != 0 {
				err = eps.local.Handle().SignalPeer(0, zxsocket.SignalIncoming)
				eps.incoming.mu.asserted = true
			}
			eps.incoming.mu.Unlock()
			if err != nil {
				if err, ok := err.(*zx.Error); ok && (err.Status == zx.ErrBadHandle || err.Status == zx.ErrPeerClosed) {
					// The endpoint is closing -- this is possible when an incoming
					// connection races with the listening endpoint being closed.
					go eps.wq.EventUnregister(&entry)
				} else {
					panic(err)
				}
			}
		}
		entry.Callback = callback(func(_ *waiter.Entry, m waiter.EventMask) {
			if m&waiter.EventErr == 0 {
				cb()
			}
		})
		eps.wq.EventRegister(&entry, waiter.EventIn|waiter.EventErr)

		// We're registering after calling Listen, so we might've missed an event.
		// Call the callback once to check for already-present incoming
		// connections.
		cb()
	})

	_ = syslog.DebugTf("listen", "%p: backlog=%d", eps, backlog)

	return socket.StreamSocketListenResultWithResponse(socket.StreamSocketListenResponse{}), nil
}

func (eps *endpointWithSocket) startPollLoop() {
	eps.mu.Lock()
	defer eps.mu.Unlock()
	select {
	case <-eps.closing:
	default:
		ch := make(chan struct{})
		eps.mu.loopPollDone = ch
		go eps.loopPoll(ch)
	}
}

func (eps *endpointWithSocket) startReadWriteLoops(signals zx.Signals) {
	eps.mu.Lock()
	defer eps.mu.Unlock()
	select {
	case <-eps.closing:
	default:
		if err := eps.local.Handle().SignalPeer(0, signals); err != nil {
			panic(err)
		}
		for _, m := range []struct {
			done *<-chan struct{}
			fn   func(chan<- struct{})
		}{
			{&eps.mu.loopReadDone, eps.loopRead},
			{&eps.mu.loopWriteDone, eps.loopWrite},
		} {
			ch := make(chan struct{})
			*m.done = ch
			go m.fn(ch)
		}
	}
}

func (eps *endpointWithSocket) Connect(ctx fidl.Context, address fidlnet.SocketAddress) (socket.BaseSocketConnectResult, error) {
	result, err := eps.endpoint.Connect(ctx, address)
	if err != nil {
		return socket.BaseSocketConnectResult{}, err
	}
	switch result.Which() {
	case socket.BaseSocketConnectResultErr:
		if result.Err != posix.ErrnoEinprogress {
			break
		}
		fallthrough
	case socket.BaseSocketConnectResultResponse:
		// It is possible to call `connect` on a listening socket - such a call
		// would fail above, so we register the callback only in the success case
		// to avoid incorrectly handling events on connected sockets.
		eps.onConnect.Do(func() {
			var (
				once  sync.Once
				entry waiter.Entry
			)
			cb := func(m waiter.EventMask) {
				once.Do(func() {
					var signals zx.Signals = zxsocket.SignalOutgoing
					if m&waiter.EventErr == 0 {
						signals |= zxsocket.SignalConnected
					}
					go eps.wq.EventUnregister(&entry)
					eps.startPollLoop()
					eps.startReadWriteLoops(signals)
				})
			}
			entry.Callback = callback(func(_ *waiter.Entry, m waiter.EventMask) {
				cb(m)
			})
			eps.wq.EventRegister(&entry, waiter.EventOut|waiter.EventErr)

			// We're registering after calling Connect, so we might've missed an
			// event. Call the callback once to check for an already-complete (even
			// with error) handshake.
			if m := eps.ep.Readiness(waiter.EventOut | waiter.EventErr); m != 0 {
				cb(m)
			}
		})
	}
	return result, nil
}

func (eps *endpointWithSocket) Accept(wantAddr bool) (posix.Errno, *tcpip.FullAddress, *endpointWithSocket, error) {
	var addr *tcpip.FullAddress
	if wantAddr {
		addr = new(tcpip.FullAddress)
	}
	ep, wq, err := eps.endpoint.ep.Accept(addr)
	if err != nil {
		return tcpipErrorToCode(err), nil, nil, nil
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
			return 0, nil, nil, err
		}
	}

	switch localAddr, err := ep.GetLocalAddress(); err.(type) {
	case *tcpip.ErrNotConnected:
		// This should never happen as of writing as GetLocalAddress does not
		// actually return any errors. However, we handle the tcpip.ErrNotConnected
		// case now for the same reasons as mentioned below for the
		// ep.GetRemoteAddress case.
		_ = syslog.DebugTf("accept", "%p: disconnected", eps)
	case nil:
		switch remoteAddr, err := ep.GetRemoteAddress(); err.(type) {
		case *tcpip.ErrNotConnected:
			// GetRemoteAddress returns a tcpip.ErrNotConnected error if ep is no
			// longer connected. This can happen if the endpoint was closed after the
			// call to Accept returned, but before this point. A scenario this was
			// actually witnessed was when a TCP RST was received after the call to
			// Accept returned, but before this point. If GetRemoteAddress returns
			// other (unexpected) errors, panic.
			_ = syslog.DebugTf("accept", "%p: local=%+v, disconnected", eps, localAddr)
		case nil:
			_ = syslog.DebugTf("accept", "%p: local=%+v, remote=%+v", eps, localAddr, remoteAddr)
		default:
			panic(err)
		}
	default:
		panic(err)
	}

	{
		eps, err := newEndpointWithSocket(ep, wq, eps.transProto, eps.netProto, eps.endpoint.ns)
		if err != nil {
			return 0, nil, nil, err
		}

		eps.onConnect.Do(func() { eps.startReadWriteLoops(zxsocket.SignalOutgoing | zxsocket.SignalConnected) })

		return 0, addr, eps, nil
	}
}

// loopWrite shuttles signals and data from the zircon socket to the tcpip.Endpoint.
func (eps *endpointWithSocket) loopWrite(ch chan<- struct{}) {
	defer close(ch)

	const sigs = zx.SignalSocketReadable | zx.SignalSocketPeerWriteDisabled | localSignalClosing

	waitEntry, notifyCh := waiter.NewChannelEntry(nil)
	eps.wq.EventRegister(&waitEntry, waiter.EventOut)
	defer eps.wq.EventUnregister(&waitEntry)

	reader := socketReader{
		socket: eps.local,
	}
	for {
		reader.lastError = nil
		reader.lastRead = 0

		// Acquire hard error lock across ep calls to avoid races and store the
		// hard error deterministically.
		eps.hardError.mu.Lock()
		n, err := eps.ep.Write(&reader, tcpip.WriteOptions{
			// We must write atomically in order to guarantee all the data fetched
			// from the zircon socket is consumed by the endpoint.
			Atomic: true,
		})
		hardError := eps.hardError.storeAndRetrieveLocked(err)
		eps.hardError.mu.Unlock()
		if n != int64(reader.lastRead) {
			panic(fmt.Sprintf("partial write into endpoint (%s); got %d, want %d", err, n, reader.lastRead))
		}
		// TODO(https://fxbug.dev/35006): Handle all transport write errors.
		switch err.(type) {
		case nil, *tcpip.ErrBadBuffer:
			switch err := reader.lastError.(type) {
			case nil:
				continue
			case *zx.Error:
				switch err.Status {
				case zx.ErrShouldWait:
					obs, err := zxwait.Wait(zx.Handle(eps.local), sigs, zx.TimensecInfinite)
					if err != nil {
						panic(err)
					}
					switch {
					case obs&zx.SignalSocketReadable != 0:
						// The client might have written some data into the socket. Always
						// continue to the loop below and try to read even if the signals
						// show the client has closed the socket.
						continue
					case obs&localSignalClosing != 0:
						// We're shutting down.
						return
					case obs&zx.SignalSocketPeerWriteDisabled != 0:
						// Fallthrough.
					default:
						panic(fmt.Sprintf("impossible signals observed: %b/%b", obs, sigs))
					}
					fallthrough
				case zx.ErrBadState:
					// Reading has been disabled for this socket endpoint.
					switch err := eps.ep.Shutdown(tcpip.ShutdownWrite); err.(type) {
					case nil, *tcpip.ErrNotConnected:
						// Shutdown can return ErrNotConnected if the endpoint was
						// connected but no longer is.
					default:
						panic(err)
					}
					return
				}
			}
			panic(err)
		case *tcpip.ErrNotConnected:
			// Write never returns ErrNotConnected except for endpoints that were
			// never connected. Such endpoints should never reach this loop.
			panic(fmt.Sprintf("connected endpoint returned %s", err))
		case *tcpip.ErrWouldBlock:
			// NB: we can't select on closing here because the client may have
			// written some data into the buffer and then immediately closed the
			// socket.
			//
			// We must wait until the linger timeout.
			select {
			case <-eps.linger:
				return
			case <-notifyCh:
				continue
			}
		case *tcpip.ErrConnectionRefused:
			// Connection refused is a "hard error" that may be observed on either the
			// read or write loops.
			// TODO(https://fxbug.dev/61594): Allow the socket to be reused for
			// another connection attempt to match Linux.
			return
		case *tcpip.ErrClosedForSend:
			// Closed for send can be issued when the endpoint is in an error state,
			// which is encoded by the presence of a hard error having been
			// observed.
			// To avoid racing signals with the closing caused by a hard error,
			// we won't signal here if a hard error is already observed.
			if hardError == nil {
				if err := eps.local.Shutdown(zx.SocketShutdownRead); err != nil {
					panic(err)
				}
				_ = syslog.DebugTf("zx_socket_shutdown", "%p: ZX_SOCKET_SHUTDOWN_READ", eps)
			}
			return
		case *tcpip.ErrConnectionAborted, *tcpip.ErrConnectionReset, *tcpip.ErrNetworkUnreachable, *tcpip.ErrNoRoute:
			return
		case *tcpip.ErrTimeout:
			// The maximum duration of missing ACKs was reached, or the maximum
			// number of unacknowledged keepalives was reached.
			return
		default:
			_ = syslog.Errorf("TCP Endpoint.Write(): %s", err)
		}
	}
}

// loopRead shuttles signals and data from the tcpip.Endpoint to the zircon socket.
func (eps *endpointWithSocket) loopRead(ch chan<- struct{}) {
	defer close(ch)

	inEntry, inCh := waiter.NewChannelEntry(nil)
	eps.wq.EventRegister(&inEntry, waiter.EventIn)
	defer eps.wq.EventUnregister(&inEntry)

	const sigs = zx.SignalSocketWritable | zx.SignalSocketWriteDisabled | localSignalClosing

	writer := socketWriter{
		socket: eps.local,
	}
	for {
		// Acquire hard error lock across ep calls to avoid races and store the
		// hard error deterministically.
		eps.hardError.mu.Lock()
		res, err := eps.ep.Read(&writer, tcpip.ReadOptions{})
		hardError := eps.hardError.storeAndRetrieveLocked(err)
		eps.hardError.mu.Unlock()
		// TODO(https://fxbug.dev/35006): Handle all transport read errors.
		switch err.(type) {
		case *tcpip.ErrNotConnected:
			// Read never returns ErrNotConnected except for endpoints that were
			// never connected. Such endpoints should never reach this loop.
			panic(fmt.Sprintf("connected endpoint returned %s", err))
		case *tcpip.ErrTimeout:
			// At the time of writing, this error indicates that a TCP connection
			// has failed. This can occur during the TCP handshake if the peer
			// fails to respond to a SYN within 60 seconds, or if the retransmit
			// logic gives up after 60 seconds of missing ACKs from the peer, or if
			// the maximum number of unacknowledged keepalives is reached.
			//
			// The connection was alive but now is dead - this is equivalent to
			// having received a TCP RST.
			return
		case *tcpip.ErrConnectionRefused:
			// Connection refused is a "hard error" that may be observed on either the
			// read or write loops.
			// TODO(https://fxbug.dev/61594): Allow the socket to be reused for
			// another connection attempt to match Linux.
			return
		case *tcpip.ErrWouldBlock:
			select {
			case <-inCh:
				continue
			case <-eps.closing:
				// We're shutting down.
				return
			}
		case *tcpip.ErrClosedForReceive:
			// Closed for receive can be issued when the endpoint is in an error
			// state, which is encoded by the presence of a hard error having been
			// observed.
			// To avoid racing signals with the closing caused by a hard error,
			// we won't signal here if a hard error is already observed.
			if hardError == nil {
				if err := eps.local.Shutdown(zx.SocketShutdownWrite); err != nil {
					panic(err)
				}
				_ = syslog.DebugTf("zx_socket_shutdown", "%p: ZX_SOCKET_SHUTDOWN_WRITE", eps)
			}
			return
		case *tcpip.ErrConnectionAborted, *tcpip.ErrConnectionReset, *tcpip.ErrNetworkUnreachable, *tcpip.ErrNoRoute:
			return
		case nil, *tcpip.ErrBadBuffer:
			if err == nil {
				eps.ep.ModerateRecvBuf(res.Count)
			}
			// `tcpip.Endpoint.Read` returns a nil error if _anything_ was written -
			// even if the writer returned an error - we always want to handle those
			// errors.
			switch err := writer.lastError.(type) {
			case nil:
				continue
			case *zx.Error:
				switch err.Status {
				case zx.ErrShouldWait:
					obs, err := zxwait.Wait(zx.Handle(eps.local), sigs, zx.TimensecInfinite)
					if err != nil {
						panic(err)
					}
					switch {
					case obs&zx.SignalSocketWritable != 0:
						continue
					case obs&localSignalClosing != 0:
						// We're shutting down.
						return
					case obs&zx.SignalSocketWriteDisabled != 0:
						// Fallthrough.
					default:
						panic(fmt.Sprintf("impossible signals observed: %b/%b", obs, sigs))
					}
					fallthrough
				case zx.ErrBadState:
					// Writing has been disabled for this socket endpoint.
					switch err := eps.ep.Shutdown(tcpip.ShutdownRead); err.(type) {
					case nil:
					case *tcpip.ErrNotConnected:
						// An ErrNotConnected while connected is expected if there
						// is pending data to be read and the connection has been
						// reset by the other end of the endpoint. The endpoint will
						// allow the pending data to be read without error but will
						// return ErrNotConnected if Shutdown is called. Otherwise
						// this is unexpected, panic.
						_ = syslog.InfoTf("loopRead", "%p: client shutdown a closed endpoint; ep info: %#v", eps, eps.endpoint.ep.Info())
					default:
						panic(err)
					}
					return
				}
			}
			panic(err)
		default:
			_ = syslog.Errorf("Endpoint.Read(): %s", err)
		}
	}
}

type datagramSocketImpl struct {
	*endpointWithEvent

	cancel context.CancelFunc
}

var _ socket.DatagramSocketWithCtx = (*datagramSocketImpl)(nil)

func (s *datagramSocketImpl) close() {
	if s.endpoint.decRef() {
		s.wq.EventUnregister(&s.entry)

		if err := s.local.Close(); err != nil {
			panic(fmt.Sprintf("local.Close() = %s", err))
		}

		if err := s.peer.Close(); err != nil {
			panic(fmt.Sprintf("peer.Close() = %s", err))
		}

		if !s.ns.onRemoveEndpoint(s.endpoint.key) {
			_ = syslog.Errorf("endpoint map delete error, endpoint with key %d does not exist", s.endpoint.key)
		}

		s.ep.Close()

		_ = syslog.DebugTf("close", "%p", s.endpointWithEvent)
	}
	s.cancel()
}

func (s *datagramSocketImpl) Close(fidl.Context) (int32, error) {
	_ = syslog.DebugTf("Close", "%p", s.endpointWithEvent)
	s.close()
	return int32(zx.ErrOk), nil
}

func (s *datagramSocketImpl) addConnection(_ fidl.Context, object fidlio.NodeWithCtxInterfaceRequest) {
	{
		sCopy := *s
		s := &sCopy

		s.ns.stats.SocketCount.Increment()
		s.endpoint.incRef()
		go func() {
			defer s.ns.stats.SocketCount.Decrement()

			ctx, cancel := context.WithCancel(context.Background())
			s.cancel = cancel
			defer func() {
				// Avoid double close when the peer calls Close and then hangs up.
				if ctx.Err() == nil {
					s.close()
				}
			}()

			stub := socket.DatagramSocketWithCtxStub{Impl: s}
			component.ServeExclusive(ctx, &stub, object.Channel, func(err error) {
				// NB: this protocol is not discoverable, so the bindings do not include its name.
				_ = syslog.WarnTf("fuchsia.posix.socket.DatagramSocket", "%s", err)
			})
		}()
	}
}

func (s *datagramSocketImpl) Clone(ctx fidl.Context, flags uint32, object fidlio.NodeWithCtxInterfaceRequest) error {
	s.addConnection(ctx, object)

	_ = syslog.DebugTf("Clone", "%p: flags=%b", s.endpointWithEvent, flags)

	return nil
}

func (s *datagramSocketImpl) RecvMsg(_ fidl.Context, wantAddr bool, dataLen uint32, wantControl bool, flags socket.RecvMsgFlags) (socket.DatagramSocketRecvMsgResult, error) {
	var b bytes.Buffer
	dst := tcpip.LimitedWriter{
		W: &b,
		N: int64(dataLen),
	}
	// TODO(https://fxbug.dev/21106): do something with control messages.
	_ = wantControl
	res, err := s.ep.Read(&dst, tcpip.ReadOptions{
		Peek:           flags&socket.RecvMsgFlagsPeek != 0,
		NeedRemoteAddr: wantAddr,
	})
	if _, ok := err.(*tcpip.ErrBadBuffer); ok && dataLen == 0 {
		err = nil
	}
	if err != nil {
		return socket.DatagramSocketRecvMsgResultWithErr(tcpipErrorToCode(err)), nil
	}
	{
		var err error
		// We lock here to ensure that no incoming data changes readiness while we
		// clear the signal.
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
	var addr *fidlnet.SocketAddress
	if wantAddr {
		sockaddr := toNetSocketAddress(s.netProto, res.RemoteAddr)
		addr = &sockaddr
	}

	return socket.DatagramSocketRecvMsgResultWithResponse(socket.DatagramSocketRecvMsgResponse{
		Addr:      addr,
		Data:      b.Bytes(),
		Truncated: uint32(res.Total - res.Count),
	}), nil
}

// NB: Due to another soft transition that happened, SendMsg is the "final
// state" we want to get at, while SendMsg2 is the "old" one.
func (s *datagramSocketImpl) SendMsg(_ fidl.Context, addr *fidlnet.SocketAddress, data []uint8, control socket.SendControlData, _ socket.SendMsgFlags) (socket.DatagramSocketSendMsgResult, error) {
	var writeOpts tcpip.WriteOptions
	if addr != nil {
		addr, err := toTCPIPFullAddress(*addr)
		if err != nil {
			return socket.DatagramSocketSendMsgResultWithErr(tcpipErrorToCode(&tcpip.ErrBadAddress{})), nil
		}
		if s.endpoint.netProto == ipv4.ProtocolNumber && len(addr.Addr) == header.IPv6AddressSize {
			return socket.DatagramSocketSendMsgResultWithErr(tcpipErrorToCode(&tcpip.ErrAddressFamilyNotSupported{})), nil
		}
		writeOpts.To = &addr
	}
	// TODO(https://fxbug.dev/21106): do something with control.
	_ = control
	var r bytes.Reader
	r.Reset(data)
	n, err := s.ep.Write(&r, writeOpts)
	if err != nil {
		return socket.DatagramSocketSendMsgResultWithErr(tcpipErrorToCode(err)), nil
	}
	return socket.DatagramSocketSendMsgResultWithResponse(socket.DatagramSocketSendMsgResponse{Len: n}), nil
}

type streamSocketImpl struct {
	*endpointWithSocket

	cancel context.CancelFunc
}

var _ socket.StreamSocketWithCtx = (*streamSocketImpl)(nil)

func newStreamSocket(eps *endpointWithSocket) (socket.StreamSocketWithCtxInterface, error) {
	localC, peerC, err := zx.NewChannel(0)
	if err != nil {
		return socket.StreamSocketWithCtxInterface{}, err
	}
	s := &streamSocketImpl{
		endpointWithSocket: eps,
	}
	s.addConnection(context.Background(), fidlio.NodeWithCtxInterfaceRequest{Channel: localC})
	_ = syslog.DebugTf("NewStream", "%p", s.endpointWithSocket)
	return socket.StreamSocketWithCtxInterface{Channel: peerC}, nil
}

func (s *streamSocketImpl) close() {
	defer s.cancel()

	if s.endpoint.decRef() {
		linger := s.ep.SocketOptions().GetLinger()

		doClose := func() {
			s.endpointWithSocket.close()

			if err := s.peer.Close(); err != nil {
				panic(err)
			}
		}

		if linger.Enabled {
			// `man 7 socket`:
			//
			//  When enabled, a close(2) or shutdown(2) will not return until all
			//  queued messages for the socket have been successfully sent or the
			//  linger timeout has been reached. Otherwise, the call returns
			//  immediately and the closing is done in the background. When the
			//  socket is closed as part of exit(2), it always lingers in the
			//  background.
			//
			// Thus we must allow linger-amount-of-time for pending writes to flush,
			// and do so synchronously if linger is enabled.
			time.AfterFunc(linger.Timeout, func() { close(s.linger) })
			doClose()
		} else {
			// Here be dragons.
			//
			// Normally, with linger disabled, the socket is immediately closed to
			// the application (accepting no more writes) and lingers for TCP_LINGER2
			// duration. However, because of the use of a zircon socket in front of
			// the netstack endpoint, we can't be sure that all writes have flushed
			// from the zircon socket to the netstack endpoint when we observe
			// `close(3)`. This in turn means we can't close the netstack endpoint
			// (which would start the TCP_LINGER2 timer), because there may still be
			// data pending in the zircon socket (e.g. when the netstack endpoint's
			// send buffer is full). We need *some* condition to break us out of this
			// deadlock.
			//
			// We pick TCP_LINGER2 somewhat arbitrarily. In the worst case, this
			// means that our true TCP linger time will be twice the configured
			// value, but this is the best we can do without rethinking the
			// interfaces.

			// If no data is in the buffer, close synchronously. This is an important
			// optimization that prevents flakiness when a socket is closed and
			// another socket is immediately bound to the port.
			if reader := (socketReader{socket: s.endpointWithSocket.local}); reader.Len() == 0 {
				doClose()
			} else {
				var linger tcpip.TCPLingerTimeoutOption
				if err := s.ep.GetSockOpt(&linger); err != nil {
					panic(fmt.Sprintf("GetSockOpt(%T): %s", linger, err))
				}
				time.AfterFunc(time.Duration(linger), func() { close(s.linger) })

				go doClose()
			}
		}
	}
}

func (s *streamSocketImpl) Close(fidl.Context) (int32, error) {
	_ = syslog.DebugTf("Close", "%p", s.endpointWithSocket)
	s.close()
	return int32(zx.ErrOk), nil
}

func (s *streamSocketImpl) addConnection(_ fidl.Context, object fidlio.NodeWithCtxInterfaceRequest) {
	{
		sCopy := *s
		s := &sCopy

		s.ns.stats.SocketCount.Increment()
		s.endpoint.incRef()
		go func() {
			defer s.ns.stats.SocketCount.Decrement()

			ctx, cancel := context.WithCancel(context.Background())
			s.cancel = cancel
			defer func() {
				// Avoid double close when the peer calls Close and then hangs up.
				if ctx.Err() == nil {
					s.close()
				}
			}()

			stub := socket.StreamSocketWithCtxStub{Impl: s}
			component.ServeExclusive(ctx, &stub, object.Channel, func(err error) {
				// NB: this protocol is not discoverable, so the bindings do not include its name.
				_ = syslog.WarnTf("fuchsia.posix.socket.StreamSocket", "%s", err)
			})
		}()
	}
}

func (s *streamSocketImpl) Clone(ctx fidl.Context, flags uint32, object fidlio.NodeWithCtxInterfaceRequest) error {
	s.addConnection(ctx, object)

	_ = syslog.DebugTf("Clone", "%p: flags=%b", s.endpointWithSocket, flags)

	return nil
}

func (s *streamSocketImpl) Describe(fidl.Context) (fidlio.NodeInfo, error) {
	var info fidlio.NodeInfo
	h, err := s.endpointWithSocket.peer.Handle().Duplicate(zx.RightsBasic | zx.RightRead | zx.RightWrite)
	_ = syslog.DebugTf("Describe", "%p: err=%v", s.endpointWithSocket, err)
	if err != nil {
		return info, err
	}
	info.SetStreamSocket(fidlio.StreamSocket{Socket: zx.Socket(h)})
	return info, nil
}

func (s *streamSocketImpl) Accept(_ fidl.Context, wantAddr bool) (socket.StreamSocketAcceptResult, error) {
	code, addr, eps, err := s.endpointWithSocket.Accept(wantAddr)
	if err != nil {
		return socket.StreamSocketAcceptResult{}, err
	}
	if code != 0 {
		return socket.StreamSocketAcceptResultWithErr(code), nil
	}
	streamSocketInterface, err := newStreamSocket(eps)
	if err != nil {
		return socket.StreamSocketAcceptResult{}, err
	}
	// TODO(https://fxbug.dev/67600): this copies a lock; avoid this when FIDL bindings are better.
	response := socket.StreamSocketAcceptResponse{
		S: streamSocketInterface,
	}
	if addr != nil {
		sockaddr := toNetSocketAddress(s.netProto, *addr)
		response.Addr = &sockaddr
	}
	return socket.StreamSocketAcceptResultWithResponse(response), nil
}

func (ns *Netstack) onAddEndpoint(e *endpoint) {
	ns.stats.SocketsCreated.Increment()
	var key uint64
	// Reserve key value 0 to indicate that the endpoint was never
	// added to the endpoints map.
	for key == 0 {
		key = atomic.AddUint64(&ns.endpoints.nextKey, 1)
	}
	// Check if the key exists in the map already. The key is a uint64 value
	// and we skip adding the endpoint to the map in the unlikely wrap around
	// case for now.
	if ep, loaded := ns.endpoints.LoadOrStore(key, e.ep); loaded {
		var info stack.TransportEndpointInfo
		switch t := ep.Info().(type) {
		case *tcp.EndpointInfo:
			info = t.TransportEndpointInfo
		case *stack.TransportEndpointInfo:
			info = *t
		}
		_ = syslog.Errorf("endpoint map store error, key %d exists for endpoint %+v", key, info)
	} else {
		e.key = key
	}
}

func (ns *Netstack) onRemoveEndpoint(key uint64) bool {
	ns.stats.SocketsDestroyed.Increment()
	// Key value 0 would indicate that the endpoint was never
	// added to the endpoints map.
	if key == 0 {
		return false
	}
	_, deleted := ns.endpoints.LoadAndDelete(key)
	return deleted
}

type providerImpl struct {
	ns *Netstack
}

var _ socket.ProviderWithCtx = (*providerImpl)(nil)

func toTransProtoStream(_ socket.Domain, proto socket.StreamSocketProtocol) (posix.Errno, tcpip.TransportProtocolNumber) {
	switch proto {
	case socket.StreamSocketProtocolTcp:
		return 0, tcp.ProtocolNumber
	}
	return posix.ErrnoEprotonosupport, 0
}

func toTransProtoDatagram(domain socket.Domain, proto socket.DatagramSocketProtocol) (posix.Errno, tcpip.TransportProtocolNumber) {
	switch proto {
	case socket.DatagramSocketProtocolUdp:
		return 0, udp.ProtocolNumber
	case socket.DatagramSocketProtocolIcmpEcho:
		switch domain {
		case socket.DomainIpv4:
			return 0, icmp.ProtocolNumber4
		case socket.DomainIpv6:
			return 0, icmp.ProtocolNumber6
		}
	}
	return posix.ErrnoEprotonosupport, 0
}

func toNetProto(domain socket.Domain) (posix.Errno, tcpip.NetworkProtocolNumber) {
	switch domain {
	case socket.DomainIpv4:
		return 0, ipv4.ProtocolNumber
	case socket.DomainIpv6:
		return 0, ipv6.ProtocolNumber
	default:
		return posix.ErrnoEpfnosupport, 0
	}
}

type callback func(*waiter.Entry, waiter.EventMask)

func (cb callback) Callback(e *waiter.Entry, m waiter.EventMask) {
	cb(e, m)
}

func (sp *providerImpl) DatagramSocket(ctx fidl.Context, domain socket.Domain, proto socket.DatagramSocketProtocol) (socket.ProviderDatagramSocketResult, error) {
	code, netProto := toNetProto(domain)
	if code != 0 {
		return socket.ProviderDatagramSocketResultWithErr(code), nil
	}
	code, transProto := toTransProtoDatagram(domain, proto)
	if code != 0 {
		return socket.ProviderDatagramSocketResultWithErr(code), nil
	}

	wq := new(waiter.Queue)
	ep, tcpErr := sp.ns.stack.NewEndpoint(transProto, netProto, wq)
	if tcpErr != nil {
		return socket.ProviderDatagramSocketResultWithErr(tcpipErrorToCode(tcpErr)), nil
	}

	var localE, peerE zx.Handle
	if status := zx.Sys_eventpair_create(0, &localE, &peerE); status != zx.ErrOk {
		return socket.ProviderDatagramSocketResult{}, &zx.Error{Status: status, Text: "zx.EventPair"}
	}
	localC, peerC, err := zx.NewChannel(0)
	if err != nil {
		return socket.ProviderDatagramSocketResult{}, err
	}
	s := &datagramSocketImpl{
		endpointWithEvent: &endpointWithEvent{
			endpoint: endpoint{
				ep:         ep,
				wq:         wq,
				transProto: transProto,
				netProto:   netProto,
				ns:         sp.ns,
			},
			local: localE,
			peer:  peerE,
		},
	}

	s.entry.Callback = callback(func(*waiter.Entry, waiter.EventMask) {
		var err error
		s.endpointWithEvent.incoming.mu.Lock()
		if !s.endpointWithEvent.incoming.mu.asserted && s.endpoint.ep.Readiness(waiter.EventIn) != 0 {
			err = s.endpointWithEvent.local.SignalPeer(0, zxsocket.SignalIncoming)
			s.endpointWithEvent.incoming.mu.asserted = true
		}
		s.endpointWithEvent.incoming.mu.Unlock()
		if err != nil {
			panic(err)
		}
	})

	s.wq.EventRegister(&s.entry, waiter.EventIn)

	s.addConnection(ctx, fidlio.NodeWithCtxInterfaceRequest{Channel: localC})
	_ = syslog.DebugTf("NewDatagram", "%p", s.endpointWithEvent)
	datagramSocketInterface := socket.DatagramSocketWithCtxInterface{Channel: peerC}

	sp.ns.onAddEndpoint(&s.endpoint)

	if err := s.endpointWithEvent.local.SignalPeer(0, zxsocket.SignalOutgoing); err != nil {
		panic(fmt.Sprintf("local.SignalPeer(0, zxsocket.SignalOutgoing) = %s", err))
	}

	return socket.ProviderDatagramSocketResultWithResponse(socket.ProviderDatagramSocketResponse{
		S: socket.DatagramSocketWithCtxInterface{Channel: datagramSocketInterface.Channel},
	}), nil

}

func (sp *providerImpl) StreamSocket(_ fidl.Context, domain socket.Domain, proto socket.StreamSocketProtocol) (socket.ProviderStreamSocketResult, error) {
	code, netProto := toNetProto(domain)
	if code != 0 {
		return socket.ProviderStreamSocketResultWithErr(code), nil
	}
	code, transProto := toTransProtoStream(domain, proto)
	if code != 0 {
		return socket.ProviderStreamSocketResultWithErr(code), nil
	}

	wq := new(waiter.Queue)
	ep, tcpErr := sp.ns.stack.NewEndpoint(transProto, netProto, wq)
	if tcpErr != nil {
		return socket.ProviderStreamSocketResultWithErr(tcpipErrorToCode(tcpErr)), nil
	}

	socketEp, err := newEndpointWithSocket(ep, wq, transProto, netProto, sp.ns)
	if err != nil {
		return socket.ProviderStreamSocketResult{}, err
	}
	streamSocketInterface, err := newStreamSocket(socketEp)
	if err != nil {
		return socket.ProviderStreamSocketResult{}, err
	}
	return socket.ProviderStreamSocketResultWithResponse(socket.ProviderStreamSocketResponse{
		S: socket.StreamSocketWithCtxInterface{Channel: streamSocketInterface.Channel},
	}), nil
}

func (sp *providerImpl) InterfaceIndexToName(_ fidl.Context, index uint64) (socket.ProviderInterfaceIndexToNameResult, error) {
	if info, ok := sp.ns.stack.NICInfo()[tcpip.NICID(index)]; ok {
		return socket.ProviderInterfaceIndexToNameResultWithResponse(socket.ProviderInterfaceIndexToNameResponse{
			Name: info.Name,
		}), nil
	}
	return socket.ProviderInterfaceIndexToNameResultWithErr(int32(zx.ErrNotFound)), nil
}

func (sp *providerImpl) InterfaceNameToIndex(_ fidl.Context, name string) (socket.ProviderInterfaceNameToIndexResult, error) {
	for id, info := range sp.ns.stack.NICInfo() {
		if info.Name == name {
			return socket.ProviderInterfaceNameToIndexResultWithResponse(socket.ProviderInterfaceNameToIndexResponse{
				Index: uint64(id),
			}), nil
		}
	}
	return socket.ProviderInterfaceNameToIndexResultWithErr(int32(zx.ErrNotFound)), nil
}

// Adapted from helper function `nicStateFlagsToLinux` in gvisor's
// sentry/socket/netstack package.
func nicInfoFlagsToFIDL(info stack.NICInfo) socket.InterfaceFlags {
	ifs := info.Context.(*ifState)
	var bits socket.InterfaceFlags
	flags := info.Flags
	if flags.Loopback {
		bits |= socket.InterfaceFlagsLoopback
	}
	if flags.Running {
		bits |= socket.InterfaceFlagsRunning
	}
	if flags.Promiscuous {
		bits |= socket.InterfaceFlagsPromisc
	}
	// Check `IsUpLocked` because netstack interfaces are always defined to be
	// `Up` in gVisor.
	ifs.mu.Lock()
	if ifs.IsUpLocked() {
		bits |= socket.InterfaceFlagsUp
	}
	ifs.mu.Unlock()
	// Approximate that all interfaces support multicasting.
	bits |= socket.InterfaceFlagsMulticast
	return bits
}

func (sp *providerImpl) InterfaceNameToFlags(_ fidl.Context, name string) (socket.ProviderInterfaceNameToFlagsResult, error) {
	for _, info := range sp.ns.stack.NICInfo() {
		if info.Name == name {
			return socket.ProviderInterfaceNameToFlagsResultWithResponse(socket.ProviderInterfaceNameToFlagsResponse{
				Flags: nicInfoFlagsToFIDL(info),
			}), nil
		}
	}
	return socket.ProviderInterfaceNameToFlagsResultWithErr(int32(zx.ErrNotFound)), nil
}

func (sp *providerImpl) GetInterfaceAddresses(fidl.Context) ([]socket.InterfaceAddresses, error) {
	nicInfos := sp.ns.stack.NICInfo()

	resultInfos := make([]socket.InterfaceAddresses, 0, len(nicInfos))
	for id, info := range nicInfos {
		// Ensure deterministic API response.
		sort.Slice(info.ProtocolAddresses, func(i, j int) bool {
			x, y := info.ProtocolAddresses[i], info.ProtocolAddresses[j]
			if x.Protocol != y.Protocol {
				return x.Protocol < y.Protocol
			}
			ax, ay := x.AddressWithPrefix, y.AddressWithPrefix
			if ax.Address != ay.Address {
				return ax.Address < ay.Address
			}
			return ax.PrefixLen < ay.PrefixLen
		})

		addrs := make([]fidlnet.Subnet, 0, len(info.ProtocolAddresses))
		for _, a := range info.ProtocolAddresses {
			if a.Protocol != ipv4.ProtocolNumber && a.Protocol != ipv6.ProtocolNumber {
				continue
			}
			addrs = append(addrs, fidlnet.Subnet{
				Addr:      fidlconv.ToNetIpAddress(a.AddressWithPrefix.Address),
				PrefixLen: uint8(a.AddressWithPrefix.PrefixLen),
			})
		}

		var resultInfo socket.InterfaceAddresses
		resultInfo.SetId(uint64(id))
		resultInfo.SetName(info.Name)
		resultInfo.SetAddresses(addrs)

		// gVisor assumes interfaces are always up, which is not the case on Fuchsia,
		// so overwrite it with Fuchsia's interface state.
		bits := nicInfoFlagsToFIDL(info)
		// TODO(https://fxbug.dev/64758): don't `SetFlags` once all clients are
		// transitioned to use `interface_flags`.
		resultInfo.SetFlags(uint32(bits))
		resultInfo.SetInterfaceFlags(bits)

		resultInfos = append(resultInfos, resultInfo)
	}

	// Ensure deterministic API response.
	sort.Slice(resultInfos, func(i, j int) bool {
		return resultInfos[i].Id < resultInfos[j].Id
	})
	return resultInfos, nil
}

func tcpipErrorToCode(err tcpip.Error) posix.Errno {
	if _, ok := err.(*tcpip.ErrConnectStarted); !ok {
		if pc, file, line, ok := runtime.Caller(1); ok {
			if i := strings.LastIndexByte(file, '/'); i != -1 {
				file = file[i+1:]
			}
			_ = syslog.Debugf("%s: %s:%d: %s", runtime.FuncForPC(pc).Name(), file, line, err)
		} else {
			_ = syslog.Debugf("%s", err)
		}
	}
	switch err.(type) {
	case *tcpip.ErrUnknownProtocol:
		return posix.ErrnoEinval
	case *tcpip.ErrUnknownNICID:
		return posix.ErrnoEinval
	case *tcpip.ErrUnknownDevice:
		return posix.ErrnoEnodev
	case *tcpip.ErrUnknownProtocolOption:
		return posix.ErrnoEnoprotoopt
	case *tcpip.ErrDuplicateNICID:
		return posix.ErrnoEexist
	case *tcpip.ErrDuplicateAddress:
		return posix.ErrnoEexist
	case *tcpip.ErrNoRoute:
		return posix.ErrnoEhostunreach
	case *tcpip.ErrAlreadyBound:
		return posix.ErrnoEinval
	case *tcpip.ErrInvalidEndpointState:
		return posix.ErrnoEinval
	case *tcpip.ErrAlreadyConnecting:
		return posix.ErrnoEalready
	case *tcpip.ErrAlreadyConnected:
		return posix.ErrnoEisconn
	case *tcpip.ErrNoPortAvailable:
		return posix.ErrnoEagain
	case *tcpip.ErrPortInUse:
		return posix.ErrnoEaddrinuse
	case *tcpip.ErrBadLocalAddress:
		return posix.ErrnoEaddrnotavail
	case *tcpip.ErrClosedForSend:
		return posix.ErrnoEpipe
	case *tcpip.ErrClosedForReceive:
		return posix.ErrnoEagain
	case *tcpip.ErrWouldBlock:
		return posix.Ewouldblock
	case *tcpip.ErrConnectionRefused:
		return posix.ErrnoEconnrefused
	case *tcpip.ErrTimeout:
		return posix.ErrnoEtimedout
	case *tcpip.ErrAborted:
		return posix.ErrnoEpipe
	case *tcpip.ErrConnectStarted:
		return posix.ErrnoEinprogress
	case *tcpip.ErrDestinationRequired:
		return posix.ErrnoEdestaddrreq
	case *tcpip.ErrNotSupported:
		return posix.ErrnoEopnotsupp
	case *tcpip.ErrQueueSizeNotSupported:
		return posix.ErrnoEnotty
	case *tcpip.ErrNotConnected:
		return posix.ErrnoEnotconn
	case *tcpip.ErrConnectionReset:
		return posix.ErrnoEconnreset
	case *tcpip.ErrConnectionAborted:
		return posix.ErrnoEconnaborted
	case *tcpip.ErrNoSuchFile:
		return posix.ErrnoEnoent
	case *tcpip.ErrInvalidOptionValue:
		return posix.ErrnoEinval
	case *tcpip.ErrBadAddress:
		return posix.ErrnoEfault
	case *tcpip.ErrNetworkUnreachable:
		return posix.ErrnoEnetunreach
	case *tcpip.ErrMessageTooLong:
		return posix.ErrnoEmsgsize
	case *tcpip.ErrNoBufferSpace:
		return posix.ErrnoEnobufs
	case *tcpip.ErrBroadcastDisabled, *tcpip.ErrNotPermitted:
		return posix.ErrnoEacces
	case *tcpip.ErrAddressFamilyNotSupported:
		return posix.ErrnoEafnosupport
	default:
		panic(fmt.Sprintf("unknown error %v", err))
	}
}
