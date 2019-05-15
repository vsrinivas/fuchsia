// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"syscall/zx"

	"syslog"

	"netstack/util"

	"fidl/fuchsia/net"

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

var _ net.SocketProvider = (*socketProviderImpl)(nil)

func toTransProto(typ, protocol int16) (int16, tcpip.TransportProtocolNumber) {
	switch {
	case typ&C.SOCK_STREAM != 0:
		switch protocol {
		case C.IPPROTO_IP, C.IPPROTO_TCP:
			return 0, tcp.ProtocolNumber
		}
	case typ&C.SOCK_DGRAM != 0:
		switch protocol {
		case C.IPPROTO_IP, C.IPPROTO_UDP:
			return 0, udp.ProtocolNumber
		case C.IPPROTO_ICMP:
			return 0, icmp.ProtocolNumber4
		}
	}
	return C.EPROTONOSUPPORT, 0
}

func (sp *socketProviderImpl) Socket(domain, typ, protocol int16) (int16, zx.Socket, error) {
	var netProto tcpip.NetworkProtocolNumber
	switch domain {
	case C.AF_INET:
		netProto = ipv4.ProtocolNumber
	case C.AF_INET6:
		netProto = ipv6.ProtocolNumber
	default:
		return C.EPFNOSUPPORT, zx.Socket(zx.HandleInvalid), nil
	}

	code, transProto := toTransProto(typ, protocol)
	if code != 0 {
		return code, zx.Socket(zx.HandleInvalid), nil
	}

	wq := new(waiter.Queue)
	sp.ns.mu.Lock()
	ep, err := sp.ns.mu.stack.NewEndpoint(transProto, netProto, wq)
	sp.ns.mu.Unlock()
	if err != nil {
		return tcpipErrorToCode(err), zx.Socket(zx.HandleInvalid), nil
	}
	return 0, newIostate(sp.ns, netProto, transProto, wq, ep, false), nil
}

func (sp *socketProviderImpl) GetAddrInfo(node *string, service *string, hints *net.AddrInfoHints) (net.AddrInfoStatus, uint32, [4]net.AddrInfo, error) {
	if hints == nil {
		hints = &net.AddrInfoHints{}
	}
	if hints.SockType == 0 {
		hints.SockType = C.SOCK_STREAM
	}
	if hints.Protocol == 0 {
		switch hints.SockType {
		case C.SOCK_STREAM:
			hints.Protocol = C.IPPROTO_TCP
		case C.SOCK_DGRAM:
			hints.Protocol = C.IPPROTO_UDP
		}
	}

	code, transProto := toTransProto(int16(hints.SockType), int16(hints.Protocol))
	if code != 0 {
		syslog.VLogf(syslog.DebugVerbosity, "getaddrinfo: sockProto: %d", code)
		return net.AddrInfoStatusSystemError, 0, [4]net.AddrInfo{}, nil
	}

	var port uint16
	if service != nil && *service != "" {
		var err error
		if port, err = serviceLookup(*service, transProto); err != nil {
			syslog.VLogf(syslog.DebugVerbosity, "getaddrinfo: serviceLookup: %v", err)
			return net.AddrInfoStatusSystemError, 0, [4]net.AddrInfo{}, nil
		}
	}

	var addrs []tcpip.Address
	switch {
	case node == nil || *node == "":
		addrs = append(addrs, "\x00\x00\x00\x00")
	case *node == "localhost" || *node == sp.ns.getNodeName():
		switch hints.Family {
		case C.AF_UNSPEC:
			addrs = append(addrs, ipv4Loopback, ipv6Loopback)
		case C.AF_INET:
			addrs = append(addrs, ipv4Loopback)
		case C.AF_INET6:
			addrs = append(addrs, ipv6Loopback)
		default:
			return net.AddrInfoStatusSystemError, 0, [4]net.AddrInfo{}, nil
		}
	default:
		var err error
		if addrs, err = sp.ns.dnsClient.LookupIP(*node); err != nil {
			addrs = append(addrs, util.Parse(*node))
			syslog.VLogf(syslog.DebugVerbosity, "getaddrinfo: addr=%v, err=%v", addrs, err)
		}
	}

	if len(addrs) == 0 || len(addrs[0]) == 0 {
		return net.AddrInfoStatusNoName, 0, [4]net.AddrInfo{}, nil
	}

	// Reply up to 4 addresses.
	num := uint32(0)
	var results [4]net.AddrInfo
	for _, addr := range addrs {
		ai := net.AddrInfo{
			Flags:    0,
			SockType: hints.SockType,
			Protocol: hints.Protocol,
			Port:     port,
		}

		switch len(addr) {
		case 4:
			if hints.Family != C.AF_UNSPEC && hints.Family != C.AF_INET {
				continue
			}
			ai.Family = C.AF_INET
			ai.Addr.Len = uint32(copy(ai.Addr.Val[:], addr))
		case 16:
			if hints.Family != C.AF_UNSPEC && hints.Family != C.AF_INET6 {
				continue
			}
			ai.Family = C.AF_INET6
			ai.Addr.Len = uint32(copy(ai.Addr.Val[:], addr))
		default:
			syslog.VLogf(syslog.DebugVerbosity, "getaddrinfo: len(addr)=%d, wrong size", len(addr))
			// TODO: failing to resolve is a valid reply. fill out retval
			return net.AddrInfoStatusSystemError, 0, results, nil
		}

		results[num] = ai
		num++
		if int(num) == len(results) {
			break
		}
	}

	return net.AddrInfoStatusOk, num, results, nil
}
