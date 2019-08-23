// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"fidl/fuchsia/posix/socket"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/network/ipv4"
	"github.com/google/netstack/tcpip/network/ipv6"
	"github.com/google/netstack/tcpip/transport/icmp"
	"github.com/google/netstack/tcpip/transport/tcp"
	"github.com/google/netstack/tcpip/transport/udp"
	"github.com/google/netstack/waiter"
)

// #cgo CFLAGS: -I${SRCDIR}/../../../../zircon/system/ulib/zxs/include
// #cgo CFLAGS: -I${SRCDIR}/../../../../zircon/ulib/musl/include
// #include <errno.h>
// #include <lib/zxs/protocol.h>
import "C"

type socketProviderImpl struct {
	ns *Netstack
}

type providerImpl struct {
	ns             *Netstack
	controlService socket.ControlService
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
		controlInterface, err := newIostate(sp.ns, netProto, transProto, wq, ep, &sp.controlService)
		return 0, controlInterface, err
	}
}
