// Copyright 2017 The Fuchsia Authors. All rights reserved.
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

	"netstack/fidlconv"
	"syslog"

	"fidl/fuchsia/io"
	"fidl/fuchsia/posix/socket"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/header"
	"github.com/google/netstack/tcpip/network/ipv4"
	"github.com/google/netstack/tcpip/transport/tcp"
	"github.com/google/netstack/waiter"
)

// #cgo CFLAGS: -D_GNU_SOURCE
// #cgo CFLAGS: -I${SRCDIR}/../../../../zircon/system/ulib/zxs/include
// #cgo CFLAGS: -I${SRCDIR}/../../../../zircon/third_party/ulib/musl/include
// #cgo CFLAGS: -I${SRCDIR}/../../../../garnet/public
// #include <errno.h>
// #include <fcntl.h>
// #include <lib/zxs/protocol.h>
// #include <netinet/tcp.h>
// #include <lib/netstack/c/netconfig.h>
import "C"

const localSignalClosing = zx.SignalUser5

type iostate struct {
	wq *waiter.Queue
	ep tcpip.Endpoint

	ns *Netstack

	mu struct {
		sync.Mutex
		sockOptTimestamp bool
	}

	// The number of live `socketImpl`s that reference this iostate.
	clones int64

	netProto   tcpip.NetworkProtocolNumber   // IPv4 or IPv6
	transProto tcpip.TransportProtocolNumber // TCP or UDP

	dataHandle         zx.Socket // used to communicate with libc
	incomingAssertedMu sync.Mutex

	loopWriteDone chan struct{} // report that loopWrite finished

	closing chan struct{}
}

// loopWrite connects libc write to the network stack.
func (ios *iostate) loopWrite() error {
	const sigs = zx.SignalSocketReadable | zx.SignalSocketPeerWriteDisabled |
		zx.SignalSocketPeerClosed | localSignalClosing

	waitEntry, notifyCh := waiter.NewChannelEntry(nil)
	ios.wq.EventRegister(&waitEntry, waiter.EventOut)
	defer ios.wq.EventUnregister(&waitEntry)
	for {
		// TODO: obviously allocating for each read is silly.
		// A quick hack we can do is store these in a ring buffer,
		// as the lifecycle of this buffer.View starts here, and
		// ends in nearby code we control in link.go.
		v := make([]byte, 0, 2048)
		n, err := ios.dataHandle.Read(v[:cap(v)], 0)
		if err != nil {
			if err, ok := err.(*zx.Error); ok {
				switch err.Status {
				case zx.ErrPeerClosed:
					return nil
				case zx.ErrBadState:
					// Reading has been disabled for this socket endpoint.
					if err := ios.ep.Shutdown(tcpip.ShutdownWrite); err != nil && err != tcpip.ErrNotConnected {
						return fmt.Errorf("Endpoint.Shutdown(ShutdownWrite): %s", err)
					}
					return nil
				case zx.ErrShouldWait:
					obs, err := zxwait.Wait(zx.Handle(ios.dataHandle), sigs, zx.TimensecInfinite)
					if err != nil {
						if err, ok := err.(*zx.Error); ok {
							switch err.Status {
							case zx.ErrCanceled:
								return nil
							}
						}
						panic(err)
					}
					switch {
					case obs&zx.SignalSocketPeerWriteDisabled != 0:
						// The next Read will return zx.BadState.
						continue
					case obs&zx.SignalSocketReadable != 0:
						// The client might have written some data into the socket.
						// Always continue to the 'for' loop below and try to read them
						// even if the signals show the client has closed the dataHandle.
						continue
					case obs&zx.SignalSocketPeerClosed != 0:
						return nil
					case obs&localSignalClosing != 0:
						return nil
					}
				}
			}
			panic(err)
		}
		v = v[:n]

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
				optsStr := "<TCP>"
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
	}
}

// loopRead connects libc read to the network stack.
func (ios *iostate) loopRead(inCh <-chan struct{}) error {
	const sigs = zx.SignalSocketWritable | zx.SignalSocketWriteDisabled |
		zx.SignalSocketPeerClosed | localSignalClosing

	outEntry, outCh := waiter.NewChannelEntry(nil)
	connected := ios.transProto != tcp.ProtocolNumber
	if !connected {
		ios.wq.EventRegister(&outEntry, waiter.EventOut)
		defer func() {
			if !connected {
				// If connected became true then we must have already unregistered
				// below.  We must never unregister the same entry twice because that
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
				select {
				case <-ios.closing:
					return nil
				case <-inCh:
					// We got an incoming connection; we must be a listening socket.
					// Because we are a listening socket, we don't expect anymore outbound
					// events so there's no harm in letting outEntry remain registered
					// until the end of the function.
					ios.incomingAssertedMu.Lock()
					err := ios.dataHandle.Handle().SignalPeer(0, mxnet.MXSIO_SIGNAL_INCOMING)
					ios.incomingAssertedMu.Unlock()
					if err != nil {
						if err, ok := err.(*zx.Error); ok {
							switch err.Status {
							case zx.ErrBadHandle, zx.ErrPeerClosed:
								return nil
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

				if err := ios.dataHandle.Handle().SignalPeer(0, signals); err != nil {
					if err, ok := err.(*zx.Error); ok {
						switch err.Status {
						case zx.ErrBadHandle, zx.ErrPeerClosed:
							return nil
						}
					}
					panic(err)
				}
			}
			switch err {
			case nil:
			case tcpip.ErrClosedForReceive:
				return nil
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
			n, err := ios.dataHandle.Write(v, 0)
			if err != nil {
				if err, ok := err.(*zx.Error); ok {
					switch err.Status {
					case zx.ErrBadHandle, zx.ErrPeerClosed:
						return nil
					case zx.ErrBadState:
						// Writing has been disabled for this socket endpoint.
						if err := ios.ep.Shutdown(tcpip.ShutdownRead); err != nil {
							return fmt.Errorf("Endpoint.Shutdown(ShutdownRead): %s", err)
						}
						return nil
					case zx.ErrShouldWait:
						obs, err := zxwait.Wait(zx.Handle(ios.dataHandle), sigs, zx.TimensecInfinite)
						if err != nil {
							if err, ok := err.(*zx.Error); ok {
								switch err.Status {
								case zx.ErrBadHandle, zx.ErrCanceled:
									return nil
								}
							}
							panic(err)
						}
						switch {
						case obs&zx.SignalSocketWriteDisabled != 0:
							// The next Write will return zx.BadState.
							continue
						case obs&zx.SignalSocketWritable != 0:
							continue
						case obs&zx.SignalSocketPeerClosed != 0:
							return nil
						case obs&localSignalClosing != 0:
							return nil
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
				break writeLoop
			}
		}
	}
}

func newIostate(ns *Netstack, netProto tcpip.NetworkProtocolNumber, transProto tcpip.TransportProtocolNumber, wq *waiter.Queue, ep tcpip.Endpoint, controlService *socket.ControlService) (socket.ControlInterface, error) {
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

	// This must be registered before returning to prevent a race
	// condition.
	inEntry, inCh := waiter.NewChannelEntry(nil)
	ios.wq.EventRegister(&inEntry, waiter.EventIn)

	go func() {
		defer ios.wq.EventUnregister(&inEntry)

		if err := ios.loopRead(inCh); err != nil {
			syslog.VLogf(syslog.DebugVerbosity, "%p: loopRead: %s", ios, err)
		}
		if err := ios.dataHandle.Shutdown(zx.SocketShutdownWrite); err != nil {
			if err, ok := err.(*zx.Error); ok {
				switch err.Status {
				case zx.ErrBadHandle:
					return
				}
			}
			syslog.Warnf("%p: %s", ios, err)
		}
	}()
	go func() {
		defer close(ios.loopWriteDone)

		if err := ios.loopWrite(); err != nil {
			syslog.VLogf(syslog.DebugVerbosity, "%p: loopWrite: %s", ios, err)
		}
		if err := ios.dataHandle.Shutdown(zx.SocketShutdownRead); err != nil {
			if err, ok := err.(*zx.Error); ok {
				switch err.Status {
				case zx.ErrBadHandle:
					return
				}
			}
			syslog.Warnf("%p: %s", ios, err)
		}
	}()

	syslog.VLogTf(syslog.DebugVerbosity, "socket", "%p", ios)

	s := &socketImpl{
		iostate:        ios,
		peer:           peerS,
		controlService: controlService,
	}
	if err := s.Clone(0, io.NodeInterfaceRequest{Channel: localC}); err != nil {
		s.close()
		return socket.ControlInterface{}, err
	}
	return socket.ControlInterface{Channel: peerC}, nil
}

func (ios *iostate) buildIfInfos() *C.netc_get_if_info_t {
	rep := &C.netc_get_if_info_t{}

	ios.ns.mu.Lock()
	defer ios.ns.mu.Unlock()
	var index C.uint
	for nicid, ifs := range ios.ns.mu.ifStates {
		ifs.mu.Lock()
		info, err := ifs.toNetInterface2Locked()
		ifs.mu.Unlock()
		if err != nil {
			syslog.Errorf("NIC %d: error getting info: %s", ifs.nicid, err)
			continue
		}
		if info.Addr == fidlconv.ToNetIpAddress(ipv4Loopback) {
			continue
		}
		name := info.Name
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
		rep.info[index].addr.Encode(ipv4.ProtocolNumber, tcpip.FullAddress{NIC: nicid, Addr: fidlconv.ToTCPIPAddress(info.Addr)})
		rep.info[index].netmask.Encode(ipv4.ProtocolNumber, tcpip.FullAddress{NIC: nicid, Addr: fidlconv.ToTCPIPAddress(info.Netmask)})
		rep.info[index].broadaddr.Encode(ipv4.ProtocolNumber, tcpip.FullAddress{NIC: nicid, Addr: fidlconv.ToTCPIPAddress(info.Broadaddr)})

		index++
	}
	rep.n_info = index
	return rep
}

func ioctlNum(kind, family, number uint32) uint32 {
	return ((kind & 0xF) << 20) | ((family & 0xFF) << 8) | (number & 0xFF)
}

const (
	ioctlKindDefault     = 0x0  // IOCTL_KIND_DEFAULT
	ioctlFamilyNetconfig = 0x26 // IOCTL_FAMILY_NETCONFIG
)

var (
	ioctlNetcGetNumIfs   = ioctlNum(ioctlKindDefault, ioctlFamilyNetconfig, 1)
	ioctlNetcGetIfInfoAt = ioctlNum(ioctlKindDefault, ioctlFamilyNetconfig, 2)
	ioctlNetcGetNodename = ioctlNum(ioctlKindDefault, ioctlFamilyNetconfig, 8)
)

// We remember the interface list from the last time ioctlNetcGetNumIfs was called. This avoids
// a race condition if the interface list changes between calls to ioctlNetcGetIfInfoAt.
var lastIfInfo *C.netc_get_if_info_t

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

type socketImpl struct {
	*iostate

	peer           zx.Socket
	controlService *socket.ControlService
	bindingKey     fidl.BindingKey
}

func (s *socketImpl) Clone(flags uint32, object io.NodeInterfaceRequest) error {
	clones := atomic.AddInt64(&s.iostate.clones, 1)
	{
		sCopy := *s
		s := &sCopy
		bindingKey, err := s.controlService.Add(s, object.Channel, func(error) { s.close() })
		sCopy.bindingKey = bindingKey

		syslog.VLogTf(syslog.DebugVerbosity, "Clone", "%p: clones=%d flags=%b err=%v", s.iostate, clones, flags, err)

		return err
	}
}

func (s *socketImpl) close() {
	clones := atomic.AddInt64(&s.iostate.clones, -1)

	if clones == 0 {
		close(s.iostate.closing)

		// Signal that we're about to close. This tells the various message loops to finish
		// processing, and let us know when they're done.
		if err := s.iostate.dataHandle.Handle().Signal(0, localSignalClosing); err != nil {
			panic(err)
		}

		// NB: we can't wait for loopRead to finish here because the dataHandle
		// may be full, and loopRead will never exit.
		if ch := s.iostate.loopWriteDone; ch != nil {
			<-ch
		}

		// This has to happen after loopWrite exits because ios.ep is used there.
		s.iostate.ep.Close()

		if err := s.iostate.dataHandle.Close(); err != nil {
			panic(err)
		}

		if err := s.peer.Close(); err != nil {
			panic(err)
		}
	}

	removed := s.controlService.Remove(s.bindingKey)

	syslog.VLogTf(syslog.DebugVerbosity, "close", "%p: clones=%d removed=%t", s.iostate, clones, removed)
}

func (s *socketImpl) Close() (int32, error) {
	s.close()
	return int32(zx.ErrOk), nil
}

func (s *socketImpl) Describe() (io.NodeInfo, error) {
	var info io.NodeInfo
	h, err := s.peer.Handle().Duplicate(zx.RightSameRights)
	syslog.VLogTf(syslog.DebugVerbosity, "Describe", "%p: err=%v", s.iostate, err)
	if err != nil {
		return info, err
	}
	info.SetSocket(io.Socket{Socket: zx.Socket(h)})
	return info, nil
}

func (s *socketImpl) Sync() (int32, error) {
	syslog.VLogTf(syslog.DebugVerbosity, "Sync", "%p", s.iostate)

	return 0, &zx.Error{Status: zx.ErrNotSupported, Text: "fuchsia.posix.socket.Control"}
}
func (s *socketImpl) GetAttr() (int32, io.NodeAttributes, error) {
	syslog.VLogTf(syslog.DebugVerbosity, "GetAttr", "%p", s.iostate)

	return 0, io.NodeAttributes{}, &zx.Error{Status: zx.ErrNotSupported, Text: "fuchsia.posix.socket.Control"}
}
func (s *socketImpl) SetAttr(flags uint32, attributes io.NodeAttributes) (int32, error) {
	syslog.VLogTf(syslog.DebugVerbosity, "SetAttr", "%p", s.iostate)

	return 0, &zx.Error{Status: zx.ErrNotSupported, Text: "fuchsia.posix.socket.Control"}
}
func (s *socketImpl) Ioctl(opcode uint32, maxOut uint64, handles []zx.Handle, in []uint8) (int32, []zx.Handle, []uint8, error) {
	syslog.VLogTf(syslog.DebugVerbosity, "Ioctl", "%p", s.iostate)

	return 0, nil, nil, &zx.Error{Status: zx.ErrNotSupported, Text: "fuchsia.posix.socket.Control"}
}

func (s *socketImpl) IoctlPosix(req int16, in []uint8) (int16, []uint8, error) {
	syslog.VLogTf(syslog.DebugVerbosity, "IoctlPosix", "%p", s.iostate)

	return s.iostate.Ioctl(req, in)
}

func (s *socketImpl) Accept(flags int16) (int16, socket.ControlInterface, error) {
	ep, wq, err := s.iostate.ep.Accept()
	// NB: we need to do this before checking the error, or the incoming signal
	// will never be cleared.
	//
	// We lock here to ensure that no incoming connection changes readiness
	// while we clear the signal.
	s.iostate.incomingAssertedMu.Lock()
	if s.iostate.ep.Readiness(waiter.EventIn) == 0 {
		if err := s.iostate.dataHandle.Handle().SignalPeer(mxnet.MXSIO_SIGNAL_INCOMING, 0); err != nil {
			panic(err)
		}
	}
	s.iostate.incomingAssertedMu.Unlock()
	if err != nil {
		return tcpipErrorToCode(err), socket.ControlInterface{}, nil
	}

	localAddr, err := ep.GetLocalAddress()
	if err != nil {
		panic(err)
	}
	remoteAddr, err := ep.GetRemoteAddress()
	if err != nil {
		panic(err)
	}
	syslog.VLogTf(syslog.DebugVerbosity, "accept", "%p: local=%+v, remote=%+v", s.iostate, localAddr, remoteAddr)

	{
		controlInterface, err := newIostate(s.iostate.ns, s.iostate.netProto, s.iostate.transProto, wq, ep, s.controlService)
		return 0, controlInterface, err
	}
}

func (ios *iostate) Connect(sockaddr []uint8) (int16, error) {
	addr, err := decodeAddr(sockaddr)
	if err != nil {
		return tcpipErrorToCode(tcpip.ErrBadAddress), nil
	}
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

	{
		localAddr, err := ios.ep.GetLocalAddress()
		if err != nil {
			panic(err)
		}

		// NB: We can't just compare the length to zero because that would
		// mishandle the IPv6-mapped IPv4 unspecified address.
		if len(addr.Addr) == 0 || net.IP(addr.Addr).IsUnspecified() {
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

func (ios *iostate) Bind(sockaddr []uint8) (int16, error) {
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

func (ios *iostate) Listen(backlog int16) (int16, error) {
	if err := ios.ep.Listen(int(backlog)); err != nil {
		return tcpipErrorToCode(err), nil
	}

	syslog.VLogTf(syslog.DebugVerbosity, "listen", "%p: backlog=%d", ios, backlog)

	return 0, nil
}

func (ios *iostate) GetSockOpt(level, optName int16) (int16, []uint8, error) {
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

func (ios *iostate) SetSockOpt(level, optName int16, optVal []uint8) (int16, error) {
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
			syslog.Infof("ioctlNetcGetIfInfoAt: called before ioctlNetcGetNumIfs")
			return tcpipErrorToCode(tcpip.ErrInvalidEndpointState), nil, nil
		}
		if len(in) != 4 {
			syslog.Errorf("ioctlNetcGetIfInfoAt: bad input length %d", len(in))
			return tcpipErrorToCode(tcpip.ErrInvalidOptionValue), nil, nil
		}
		requestedIndex := binary.LittleEndian.Uint32(in)
		if requestedIndex >= uint32(lastIfInfo.n_info) {
			syslog.Infof("ioctlNetcGetIfInfoAt: index out of range (%d vs %d)", requestedIndex, lastIfInfo.n_info)
			return tcpipErrorToCode(tcpip.ErrInvalidOptionValue), nil, nil
		}
		return 0, lastIfInfo.info[requestedIndex].Marshal(), nil

	case ioctlNetcGetNodename:
		return 0, append([]byte(ios.ns.getDeviceName()), 0), nil

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
