// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"log"

	"app/context"
	"syscall/zx"
	"syscall/zx/mxerror"

	"fidl/fuchsia/net"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/network/ipv4"
	"github.com/google/netstack/tcpip/network/ipv6"
	"github.com/google/netstack/tcpip/transport/tcp"
	"github.com/google/netstack/tcpip/transport/udp"
)

type socketProviderImpl struct{}

func sockProto(typ net.SocketType, protocol net.SocketProtocol) (tcpip.TransportProtocolNumber, error) {
	switch typ {
	case net.SocketTypeStream:
		switch protocol {
		case net.SocketProtocolIp, net.SocketProtocolTcp:
			return tcp.ProtocolNumber, nil
		default:
			return 0, mxerror.Errorf(zx.ErrNotSupported, "unsupported SOCK_STREAM protocol: %d", protocol)
		}
	case net.SocketTypeDgram:
		switch protocol {
		case net.SocketProtocolIp, net.SocketProtocolUdp:
			return udp.ProtocolNumber, nil
		case net.SocketProtocolIcmp:
			return ipv4.PingProtocolNumber, nil
		default:
			return 0, mxerror.Errorf(zx.ErrNotSupported, "unsupported SOCK_DGRAM protocol: %d", protocol)
		}
	}
	return 0, mxerror.Errorf(zx.ErrNotSupported, "unsupported protocol: %d/%d", typ, protocol)
}

func (sp *socketProviderImpl) OpenSocket(d net.SocketDomain, t net.SocketType, p net.SocketProtocol) (zx.Socket, int32, error) {
	var netProto tcpip.NetworkProtocolNumber
	switch d {
	case net.SocketDomainInet:
		netProto = ipv4.ProtocolNumber
	case net.SocketDomainInet6:
		netProto = ipv6.ProtocolNumber
	default:
		return zx.Socket(zx.HandleInvalid), int32(zx.ErrNotSupported), nil
	}

	transProto, err := sockProto(t, p)
	if err != nil {
		return zx.Socket(zx.HandleInvalid), int32(errStatus(err)), nil
	}

	s, err := ns.socketServer.opSocket(netProto, transProto)
	if err != nil {
		return zx.Socket(zx.HandleInvalid), int32(errStatus(err)), nil
	}
	return s, 0, nil
}

func netStringToString(ns *net.String) (string, net.AddrInfoStatus) {
	v := ns.Val[:]
	if len(v) < int(ns.Len) {
		return "", net.AddrInfoStatusBufferOverflow
	}
	return string(v[:ns.Len]), net.AddrInfoStatusOk
}

func (sp *socketProviderImpl) GetAddrInfo(n *net.String, s *net.String, hints *net.AddrInfoHints) (net.AddrInfoStatus, int32, *net.AddrInfo, *net.AddrInfo, *net.AddrInfo, *net.AddrInfo, error) {
	var node *string
	if n != nil {
		str, status := netStringToString(n)
		if status != net.AddrInfoStatusOk {
			return status, 0, nil, nil, nil, nil, nil
		}
		node = &str
	}
	var service *string
	if s != nil {
		str, status := netStringToString(s)
		if status != net.AddrInfoStatusOk {
			return status, 0, nil, nil, nil, nil, nil
		}
		service = &str
	}

	if hints == nil {
		hints = &net.AddrInfoHints{}
	}
	if hints.SockType == 0 {
		hints.SockType = SOCK_STREAM
	}
	if hints.Protocol == 0 {
		if hints.SockType == SOCK_STREAM {
			hints.Protocol = IPPROTO_TCP
		} else if hints.SockType == SOCK_DGRAM {
			hints.Protocol = IPPROTO_UDP
		}
	}

	transProto, err := sockProto(net.SocketType(hints.SockType), net.SocketProtocol(hints.Protocol))
	if err != nil {
		if debug {
			log.Printf("getaddrinfo: sockProto: %v", err)
		}
		return net.AddrInfoStatusSystemError, 0, nil, nil, nil, nil, nil
	}

	status, addrs, port := ns.socketServer.GetAddrInfo(node, service, transProto)
	if status != 0 {
		return status, 0, nil, nil, nil, nil, nil
	}
	if len(addrs) == 0 || len(addrs[0]) == 0 {
		return net.AddrInfoStatusNoName, 0, nil, nil, nil, nil, nil
	}

	// Reply up to 4 addresses.
	num := int32(0)
	results := make([]*net.AddrInfo, 4)
	values := make([]net.AddrInfo, 4)
	for i := 0; i < len(addrs) && i < 4; i++ {
		ai := &values[i]
		*ai = net.AddrInfo{
			Flags:    0,
			SockType: hints.SockType,
			Protocol: hints.Protocol,
			Port:     port,
		}

		switch len(addrs[i]) {
		case 4:
			if hints.Family != AF_UNSPEC && hints.Family != AF_INET {
				continue
			}
			ai.Family = AF_INET
			ai.Addr.Len = 4
			copy(ai.Addr.Val[:4], addrs[i])
		case 16:
			if hints.Family != AF_UNSPEC && hints.Family != AF_INET6 {
				continue
			}
			ai.Family = AF_INET6
			ai.Addr.Len = 16
			copy(ai.Addr.Val[:16], addrs[i])
		default:
			if debug {
				log.Printf("getaddrinfo: len(addr)=%d, wrong size", len(addrs[i]))
			}
			// TODO: failing to resolve is a valid reply. fill out retval
			return net.AddrInfoStatusSystemError, 0, nil, nil, nil, nil, nil
		}

		results[num] = ai
		num++
	}

	return 0, num, results[0], results[1], results[2], results[3], nil
}

var socketProvider *net.LegacySocketProviderService

// AddLegacySocketProvider registers the legacy socket provider with the
// application context, allowing it to respond to FIDL queries.
func AddLegacySocketProvider(ctx *context.Context) error {
	if socketProvider != nil {
		return fmt.Errorf("AddLegacySocketProvider must be called only once")
	}
	socketProvider = &net.LegacySocketProviderService{}
	ctx.OutgoingService.AddService(net.LegacySocketProviderName, func(c zx.Channel) error {
		_, err := socketProvider.Add(&socketProviderImpl{}, c, nil)
		return err
	})

	return nil
}
